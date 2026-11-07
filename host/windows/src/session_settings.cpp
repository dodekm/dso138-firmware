#include "session.hpp"
#include "dso_adc_contract.hpp"
#include "dso_settings_contract.hpp"
#include "session_timing.hpp"
#include "instrument_codes.hpp"
#include "dso_timebase_contract.hpp"
#include "protocol_request_payload.hpp"

#include <algorithm>
#include <type_traits>
#include <utility>

namespace dso
{

using session_timing::kSettingsPollMs;
using session_timing::kLiveReadinessPollMs;
using session_timing::kMaximumLiveReadinessReads;

void Session::setTimebase(int index)
{
    if (index < 0 || !canWriteSettings(protocol::settings_writable::Timebase) ||
        instrument_.settings.catalogueId != timebase_contract::ClassicCatalogueId ||
        instrument_.settings.catalogueCount == 0)
    {
        return;
    }
    const int count = std::min<int>(timebase_contract::ClassicTimebaseCatalogueEntries,
                                    instrument_.settings.catalogueCount);
    if (index >= count)
    {
        return;
    }
    submitSettingsWrite(
        protocol::Opcode::SetSettings,
        protocol::detail::encodeSetSettingsPayload(
            instrument_.settings.revision, static_cast<quint8>(index),
            instrument_.settings.triggerMode));
}

void Session::setTriggerMode(int mode)
{
    if (!instrument_code::code_is_in_range(
            mode, instrument_code::TriggerMode::Normal,
            instrument_code::TriggerMode::Single) ||
        !canWriteSettings(protocol::settings_writable::TriggerMode))
    {
        return;
    }
    submitSettingsWrite(
        protocol::Opcode::SetSettings,
        protocol::detail::encodeSetSettingsPayload(
            instrument_.settings.revision, instrument_.settings.timebaseIndex,
            static_cast<quint8>(mode)));
}

void Session::setTrigger(int edge, int level, int positionPermille)
{
    if (!instrument_code::code_is_in_range(
            edge, instrument_code::TriggerEdge::Rising,
            instrument_code::TriggerEdge::Falling) ||
        level < 0 || level > adc_contract::MaximumCode ||
        positionPermille < 0 ||
        positionPermille > settings_contract::TriggerPositionPermilleMaximum ||
        !canWriteSettings(protocol::settings_writable::TriggerFields))
        return;
    submitSettingsWrite(
        protocol::Opcode::SetTrigger,
        protocol::detail::encodeSetTriggerPayload(
            instrument_.settings.revision, static_cast<quint8>(edge),
            static_cast<quint16>(level), static_cast<quint16>(positionPermille)));
}

void Session::holdLocalRun()
{
    if (canSubmitSettings() &&
        (instrument_.settings.writableMask & protocol::settings_writable::LocalRunPause) != 0 &&
        (instrument_.settings.flags & protocol::settings_flags::LocalRunPauseAdmissibleNow) != 0)
        submitSettingsWrite(
            protocol::Opcode::HoldLocalRun,
            protocol::detail::encodeHoldLocalRunPayload(instrument_.settings.revision));
}

bool Session::canSubmitSettings() const noexcept
{
    return connected_ && instrument_.availability == protocol::SettingsAvailability::Supported &&
           ((state_ == State::Idle && !busy_) || state_ == State::ReadingSettings);
}

bool Session::canWriteSettings(quint8 requiredWritableMask) const noexcept
{
    return canSubmitSettings() &&
           (instrument_.settings.writableMask & requiredWritableMask) == requiredWritableMask &&
           (instrument_.settings.flags & protocol::settings_flags::WriteAdmissibleNow) != 0 &&
           instrument_.settings.uiState == instrument_code::code(instrument_code::UiState::Hold);
}

bool Session::deferUntilSettingsRead(DeferredAction action)
{
    if (state_ != State::ReadingSettings)
        return false;
    if (deferredAction_)
    {
        emit message(QStringLiteral("An action is already waiting for instrument readback"));
        return true;
    }
    deferredAction_ = std::move(action);
    return true;
}

void Session::runDeferredAction(const DeferredAction &action)
{
    std::visit([this](const auto &deferred) {
        using Action = std::decay_t<decltype(deferred)>;
        if constexpr (std::is_same_v<Action, DeferredHeldDownload>)
        {
            downloadHeld();
        }
        else if constexpr (std::is_same_v<Action, DeferredLiveStart>)
        {
            startLive(deferred.timeoutMs);
        }
        else if constexpr (std::is_same_v<Action, DeferredCaptureSeries>)
        {
            beginCaptureSeries(deferred.count, deferred.timeoutMs, deferred.intervalMs);
        }
        else if constexpr (std::is_same_v<Action, DeferredSettingsWrite>)
        {
            const auto &write = deferred.write;
            if (canSubmitSettings() && write.revision == instrument_.settings.revision)
                startSettingsWrite(write);
            else
                emit message(QStringLiteral("Instrument settings changed before the write. Review the applied values."));
        }
        else
        {
            static_assert(std::is_same_v<Action, DeferredSettingsWrite>,
                          "Handle every DeferredAction alternative explicitly");
        }
    }, action);
}

void Session::submitSettingsWrite(protocol::Opcode opcode, QByteArray payload)
{
    const SettingsWrite write{opcode, instrument_.settings.revision, std::move(payload)};
    if (!deferUntilSettingsRead(DeferredSettingsWrite{write}))
        startSettingsWrite(write);
}

void Session::startSettingsWrite(const SettingsWrite &write)
{
    settingsTimer_.stop();
    state_ = State::WritingSettings;
    setBusy(true);
    sendCommand(write.opcode, write.payload);
}

void Session::requestSettings()
{
    if (!transportOpen_ || !connected_ || busy_ || state_ != State::Idle || exchange_.pending ||
        instrument_.availability == protocol::SettingsAvailability::Legacy)
    {
        return;
    }
    settingsTimer_.stop();
    state_ = State::ReadingSettings;
    sendCommand(protocol::Opcode::GetSettings);
}

void Session::scheduleSettingsPoll()
{
    if (transportOpen_ && connected_ && state_ == State::Idle &&
        (instrument_.availability == protocol::SettingsAvailability::Supported ||
         instrument_.availability == protocol::SettingsAvailability::Stale))
    {
        settingsTimer_.start(kSettingsPollMs);
    }
}

void Session::handleSettings(const protocol::Reply &reply)
{
    QString messageText;
    if (reply.status == protocol::Status::Unsupported)
    {
        instrument_.availability = protocol::SettingsAvailability::Legacy;
        messageText = QStringLiteral("Connected to legacy firmware; live settings readback is unavailable");
    }
    else if (reply.status != protocol::Status::Ok)
    {
        instrument_.availability = protocol::SettingsAvailability::Stale;
        messageText = QStringLiteral("Instrument settings readback failed with status %1; retrying")
                          .arg(quint8(reply.status));
    }
    else
    {
        protocol::Settings decoded;
        QString error;
        if (protocol::decodeSettings(reply.payload, decoded, &error))
        {
            acceptInstrumentSettings(decoded);
        }
        else
        {
            instrument_.availability = protocol::SettingsAvailability::Stale;
            messageText = QStringLiteral("Instrument settings response is invalid: %1").arg(error);
        }
    }
    auto action = std::exchange(deferredAction_, std::nullopt);
    const auto generation = generation_;
    state_ = State::Idle;
    emit instrumentSettingsChanged(instrument_.settings, instrument_.availability);
    if (!messageText.isEmpty())
        emit message(messageText);
    if (action && generation == generation_ && state_ == State::Idle && connected_)
        runDeferredAction(*action);
    scheduleSettingsPoll();
}

void Session::acceptInstrumentSettings(const protocol::Settings &settings)
{
    instrument_.settings = settings;
    instrument_.availability = protocol::SettingsAvailability::Supported;
}

void Session::handleLiveStartRetrySettings(const protocol::Reply &reply)
{
    if (cancelRequested_)
    {
        completeOperation(QStringLiteral("Operation cancelled"));
        return;
    }
    if (reply.status == protocol::Status::Unsupported)
    {
        instrument_.availability = protocol::SettingsAvailability::Legacy;
        emit instrumentSettingsChanged(instrument_.settings, instrument_.availability);
        completeOperation(QStringLiteral("Live settings readback capability was withdrawn"));
        return;
    }

    protocol::Settings refreshed;
    QString error;
    if (reply.status != protocol::Status::Ok ||
        !protocol::decodeSettings(reply.payload, refreshed, &error))
    {
        instrument_.availability = protocol::SettingsAvailability::Stale;
        emit instrumentSettingsChanged(instrument_.settings, instrument_.availability);
        completeOperation(reply.status == protocol::Status::Ok
                              ? QStringLiteral("Live retry settings readback is invalid: %1").arg(error)
                              : QStringLiteral("Live retry settings readback failed with status %1")
                                    .arg(quint8(reply.status)));
        return;
    }

    acceptInstrumentSettings(refreshed);
    emit instrumentSettingsChanged(instrument_.settings, instrument_.availability);
    if (state_ != State::ReadingLiveStartRetrySettings || !connected_ || !captureRun_.live ||
        cancelRequested_)
    {
        return;
    }

    if (refreshed.revision != liveStartRetry_.revision)
    {
        completeOperation(QStringLiteral(
            "Instrument settings changed while live start was waiting; review settings before retrying"));
        return;
    }
    if (refreshed.uiState != instrument_code::code(instrument_code::UiState::Hold) ||
        (refreshed.flags & protocol::settings_flags::ExtendedCapture) == 0)
    {
        completeOperation(QStringLiteral(
            "Live retry cancelled because HOLD or live capability changed"));
        return;
    }

    if ((refreshed.flags & protocol::settings_flags::PhysicalAndWriteReady) !=
        protocol::settings_flags::PhysicalAndWriteReady)
    {
        if (liveStartRetry_.settingsReads < kMaximumLiveReadinessReads)
        {
            state_ = State::ReadingLiveStartRetrySettings;
            scheduleStateTimer(kLiveReadinessPollMs);
            return;
        }
        completeOperation(QStringLiteral(
            "Live retry cancelled because HOLD or physical switches did not settle"));
        return;
    }

    startNextCapture();
}

void Session::handleSettingsWrite(const protocol::Reply &reply)
{
    if (reply.status == protocol::Status::Ok)
    {
        protocol::Settings applied;
        QString error;
        if (!protocol::decodeSettings(reply.payload, applied, &error))
        {
            instrument_.availability = protocol::SettingsAvailability::Stale;
            state_ = State::Idle;
            setBusy(false);
            emit instrumentSettingsChanged(instrument_.settings, instrument_.availability);
            emit message(QStringLiteral("Settings acknowledgement is invalid: %1").arg(error));
            settingsTimer_.start(0);
            return;
        }
        acceptInstrumentSettings(applied);
        state_ = State::Idle;
        setBusy(false);
        emit instrumentSettingsChanged(instrument_.settings, instrument_.availability);
        emit message(QStringLiteral("Device applied the acknowledged settings"));
        scheduleSettingsPoll();
        return;
    }

    // Rejections are terminal for this intent. Keep the last acknowledged
    // snapshot visible, mark it stale, and fetch current state without
    // resubmitting the write under a new request ID.
    const bool statusOnly = reply.payload.size() == 1;
    instrument_.availability = protocol::SettingsAvailability::Stale;
    state_ = State::Idle;
    setBusy(false);
    emit instrumentSettingsChanged(instrument_.settings, instrument_.availability);
    const QString reason = statusOnly
                               ? QStringLiteral("Settings write was rejected with status %1")
                                     .arg(quint8(reply.status))
                               : QStringLiteral("Settings rejection had an invalid response body");
    emit message(reason + QStringLiteral("; refreshing applied settings"));
    settingsTimer_.start(0);
}

void Session::handleLiveSettings(const protocol::Reply &reply)
{
    if (cancelRequested_)
    {
        completeOperation(QStringLiteral("Operation cancelled"));
        return;
    }
    if (reply.status == protocol::Status::Unsupported)
    {
        instrument_.availability = protocol::SettingsAvailability::Legacy;
        emit instrumentSettingsChanged(instrument_.settings, instrument_.availability);
        completeOperation(QStringLiteral("Live settings readback capability was withdrawn"));
        return;
    }

    protocol::Settings decoded;
    QString error;
    if (reply.status != protocol::Status::Ok || !protocol::decodeSettings(reply.payload, decoded, &error))
    {
        instrument_.availability = protocol::SettingsAvailability::Stale;
        emit instrumentSettingsChanged(instrument_.settings, instrument_.availability);
        liveSettingsClock_.restart();
        emit message(reply.status == protocol::Status::Ok
                         ? QStringLiteral("Between-record settings readback is invalid: %1").arg(error)
                         : QStringLiteral("Between-record settings readback failed with status %1")
                               .arg(quint8(reply.status)));
        startNextCapture();
        return;
    }
    acceptInstrumentSettings(decoded);
    liveSettingsClock_.restart();
    emit instrumentSettingsChanged(instrument_.settings, instrument_.availability);
    startNextCapture();
}

} // namespace dso
