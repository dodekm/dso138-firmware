#include "session.hpp"
#include "session_wire.hpp"
#include "session_timing.hpp"
#include "protocol_request_payload.hpp"
#include "dso_usb_protocol_contract.hpp"
#include "instrument_codes.hpp"
#include "instrument_labels.hpp"

#include <algorithm>

namespace dso
{

using session_wire::read16;
using session_wire::read32;
using session_timing::kCaptureReadinessMs;
using session_timing::kCaptureCompletionGraceMs;
using session_timing::kStatusPollMs;
namespace status_layout = usb_protocol_contract::status_reply;

void Session::downloadHeld()
{
    if (!connected_ || busy_)
    {
        return;
    }
    if (deferUntilSettingsRead(DeferredHeldDownload{}))
        return;
    setBusy(true);
    cancelRequested_ = false;
    pendingFailure_.clear();
    captureRun_.remaining = 1;
    captureRun_.total = 1;
    captureRun_.completed = 0;
    jobId_ = 0;
    jobCompleteVerified_ = false;
    captureRun_.acquisitionDurationMs = -1;
    // Cancellation may be acknowledged before the device finishes its HOLD
    // redraw. Give an explicit held download the same readiness bound.
    if (instrument_.availability == protocol::SettingsAvailability::Supported &&
        instrument_.settings.uiState == instrument_code::code(instrument_code::UiState::Hold) &&
        (instrument_.settings.flags & protocol::settings_flags::RunIntent) == 0)
        completionClock_.start();
    requestFrame();
}

void Session::captureSingle(int timeoutMs)
{
    beginCaptureSeries(1, timeoutMs, 0);
}

void Session::captureRepeated(int count, int timeoutMs, int intervalMs)
{
    if (count < 1 || count > 100 || intervalMs < 0)
    {
        emit message(QStringLiteral("Repeated capture count or interval is invalid"));
        return;
    }
    beginCaptureSeries(count, timeoutMs, intervalMs);
}

void Session::startLive(int timeoutMs)
{
    if (!connected_ || busy_)
        return;
    if (timeoutMs < 1 || timeoutMs > 600000)
    {
        emit message(QStringLiteral("Capture timeout must be from 1 to 600000 ms"));
        return;
    }
    if (deferUntilSettingsRead(DeferredLiveStart{timeoutMs}))
        return;
    if (instrument_.availability != protocol::SettingsAvailability::Supported ||
        (instrument_.settings.flags & protocol::settings_flags::ExtendedCapture) == 0)
    {
        emit message(QStringLiteral("Selected-mode live capture requires firmware capability support"));
        return;
    }
    if (instrument_.settings.uiState != instrument_code::code(instrument_code::UiState::Hold) ||
        (instrument_.settings.flags & protocol::settings_flags::WriteAdmissibleNow) == 0)
    {
        emit message(QStringLiteral("Wait for the scope to reach idle HOLD before starting live capture"));
        return;
    }
    if ((instrument_.settings.flags & protocol::settings_flags::PhysicalSettingsQualified) == 0)
    {
        emit message(QStringLiteral("Wait for the scope's physical switches to settle"));
        return;
    }
    captureRun_.live = true;
    liveStartRetry_.busyRetryUsed = false;
    liveStartRetry_.revision = instrument_.settings.revision;
    captureRun_.trackHostInterval = true;
    recordPublishClock_.invalidate();
    liveSettingsClock_.restart();
    captureRun_.remaining = 1;
    captureRun_.total = 0;
    captureRun_.intervalMs = 0;
    captureRun_.completed = 0;
    captureRun_.timeoutMs = timeoutMs;
    cancelRequested_ = false;
    pendingFailure_.clear();
    setBusy(true);
    const QString mode = QString::fromLatin1(
        instrument_label::trigger_mode_name(instrument_.settings.triggerMode));
    emit liveStateChanged(true, mode);
    startNextCapture();
}

void Session::stopLive()
{
    if (!captureRun_.live)
        return;
    cancelRequested_ = true;
    ++generation_;
    if (!exchange_.pending)
        beginCleanup();
}

void Session::beginCaptureSeries(int count, int timeoutMs, int intervalMs)
{
    if (!connected_ || busy_)
    {
        return;
    }
    if (timeoutMs < 1 || timeoutMs > 600000)
    {
        emit message(QStringLiteral("Capture timeout must be from 1 to 600000 ms"));
        return;
    }
    if (deferUntilSettingsRead(DeferredCaptureSeries{count, timeoutMs, intervalMs}))
        return;

    setBusy(true);
    cancelRequested_ = false;
    pendingFailure_.clear();
    captureRun_.remaining = count;
    captureRun_.total = count;
    captureRun_.intervalMs = intervalMs;
    captureRun_.completed = 0;
    captureRun_.live = false;
    captureRun_.trackHostInterval = count > 1;
    recordPublishClock_.invalidate();
    captureRun_.timeoutMs = timeoutMs;
    startNextCapture();
}

void Session::startNextCapture()
{
    if (!connected_ || cancelRequested_)
    {
        completeOperation(QStringLiteral("Operation cancelled"));
        return;
    }
    ++generation_;
    jobId_ = 0;
    frameTransfer_.exportToken = 0;
    jobCompleteVerified_ = false;
    completionClock_.invalidate();
    captureClock_.restart();
    captureRun_.acquisitionDurationMs = -1;
    state_ = State::CaptureAcknowledgement;
    sendCommand(captureRun_.live ? protocol::Opcode::CaptureSelected
                                   : protocol::Opcode::CaptureSingle,
                protocol::detail::encodeU32Payload(quint32(captureRun_.timeoutMs)));
}

void Session::pollCaptureStatus()
{
    if (!transportOpen_ || jobId_ == 0 || exchange_.pending)
    {
        return;
    }
    state_ = State::CaptureStatus;
    sendCommand(protocol::Opcode::Status, protocol::detail::encodeU32Payload(jobId_));
}

void Session::armNextStatusPoll(int delayMs)
{
    if (cancelRequested_)
    {
        beginCleanup();
        return;
    }
    scheduleStateTimer(delayMs);
}

void Session::handleStatus(const protocol::Reply &reply)
{
    using CaptureState = instrument_code::HostCaptureState;
    const QByteArray &p = reply.payload;
    if (p.size() != status_layout::PayloadBytes || jobId_ == 0 ||
        read32(p, status_layout::JobIdOffset) != jobId_)
    {
        failOperation(QStringLiteral("STATUS response is malformed or belongs to another job"));
        return;
    }

    const quint8 controllerFault = quint8(p[status_layout::ControllerFaultOffset]);
    const auto state = static_cast<CaptureState>(quint8(p[status_layout::HostStateOffset]));
    const quint8 frameAvailable = quint8(p[status_layout::FrameAvailableOffset]);
    const quint16 count = read16(p, status_layout::FrameCountOffset);
    const quint8 storageBusy = quint8(p[status_layout::StorageBusyOffset]);
    const bool knownState = state == CaptureState::None || state == CaptureState::Pending ||
                            state == CaptureState::Complete || state == CaptureState::Cancelled ||
                            state == CaptureState::Fault;
    if (controllerFault != 0 || !knownState || frameAvailable > 1 || storageBusy > 1 ||
        (frameAvailable && (count == 0 || count > protocol::CaptureCapacity)))
    {
        failOperation(QStringLiteral("STATUS contains an invalid controller state or frame count"));
        return;
    }
    if (state == CaptureState::None)
    {
        failOperation(QStringLiteral("Device no longer recognizes the capture job"));
        return;
    }
    if (state == CaptureState::Cancelled)
    {
        jobId_ = 0;
        failOperation(QStringLiteral("Local instrument activity cancelled the capture"));
        return;
    }
    if (state == CaptureState::Fault)
    {
        jobId_ = 0;
        failOperation(QStringLiteral("The device capture failed"));
        return;
    }

    if (cancelRequested_)
    {
        if (state == CaptureState::Complete)
        {
            jobCompleteVerified_ = true;
        }
        beginCleanup();
        return;
    }
    if (state == CaptureState::Pending)
    {
        if (captureClock_.elapsed() > captureRun_.timeoutMs + kCaptureCompletionGraceMs)
        {
            failOperation(QStringLiteral("Timed out waiting for capture completion"));
            return;
        }
        armNextStatusPoll(kStatusPollMs);
        return;
    }

    if (!jobCompleteVerified_)
    {
        captureRun_.acquisitionDurationMs = captureClock_.isValid() ? captureClock_.elapsed() : -1;
    }
    jobCompleteVerified_ = true;
    if (!completionClock_.isValid())
    {
        completionClock_.start();
    }
    if (state_ == State::VerifyLeasedFrame)
    {
        const bool stillMatches = p[1] == 0 && quint8(p[3]) == 0 && frameAvailable == 1 &&
                                  storageBusy == 0 && count == frameTransfer_.metadata.count;
        if (!stillMatches)
        {
            failOperation(QStringLiteral("Completed capture no longer matches the leased frame"));
            return;
        }
        jobId_ = 0;
        prepareFrameBuffer();
        return;
    }

    const bool ready = p[1] == 0 && quint8(p[3]) == 0 && frameAvailable == 1 && storageBusy == 0;
    if (!ready)
    {
        if (completionClock_.elapsed() >= kCaptureReadinessMs)
        {
            failOperation(
                QStringLiteral("Completed capture did not become exportable within 1 second"));
            return;
        }
        armNextStatusPoll(kStatusPollMs);
        return;
    }

    requestFrame();
}

void Session::handleCancel(const protocol::Reply &reply)
{
    if (reply.payload.size() != 1)
    {
        closeTransport(QStringLiteral("CANCEL_CAPTURE response is malformed"), true);
        return;
    }
    jobId_ = 0;
    completeOperation(pendingFailure_.isEmpty() ? QStringLiteral("Operation cancelled")
                                                : pendingFailure_);
}

void Session::cancel()
{
    if (!busy_)
    {
        return;
    }
    if (state_ == State::Handshaking)
    {
        closeTransport(QStringLiteral("Connection cancelled"), true);
        return;
    }
    cancelRequested_ = true;
    ++generation_;
    if (exchange_.pending)
    {
        // The current ID and exact bytes remain active until its response or
        // bounded retry exhaustion. Reply handling records any job/token first.
        return;
    }
    beginCleanup();
}

void Session::beginCleanup()
{
    if (exchange_.pending)
    {
        return;
    }
    if (frameTransfer_.exportToken != 0)
    {
        state_ = State::EndFrame;
        sendCommand(protocol::Opcode::EndFrame,
                    protocol::detail::encodeU32Payload(frameTransfer_.exportToken));
        return;
    }
    if (jobId_ != 0 && !jobCompleteVerified_)
    {
        state_ = State::CancelCapture;
        sendCommand(protocol::Opcode::CancelCapture, protocol::detail::encodeU32Payload(jobId_));
        return;
    }
    completeOperation(pendingFailure_.isEmpty()
                          ? (cancelRequested_ ? QStringLiteral("Operation cancelled") : QString())
                          : pendingFailure_);
}

void Session::failOperation(const QString &failure)
{
    if (pendingFailure_.isEmpty())
    {
        pendingFailure_ = failure;
    }
    cancelRequested_ = true;
    ++generation_;
    beginCleanup();
}

void Session::completeOperation(const QString &text)
{
    ++generation_;
    const bool wasLive = captureRun_.live;
    state_ = connected_ ? State::Idle : State::Disconnected;
    jobId_ = 0;
    frameTransfer_.exportToken = 0;
    captureRun_.remaining = 0;
    cancelRequested_ = false;
    jobCompleteVerified_ = false;
    pendingFailure_.clear();
    deferredAction_.reset();
    captureRun_.live = false;
    captureRun_.trackHostInterval = false;
    recordPublishClock_.invalidate();
    liveSettingsClock_.invalidate();
    captureClock_.invalidate();
    completionClock_.invalidate();
    downloadClock_.invalidate();
    setBusy(false);
    scheduleSettingsPoll();
    if (wasLive)
        emit liveStateChanged(false, text.isEmpty() ? QStringLiteral("Stopped")
                                                    : QStringLiteral("Stopped: %1").arg(text));
    if (!text.isEmpty())
    {
        emit message(text);
    }
}

void Session::scheduleStateTimer(int delayMs)
{
    const quint64 scheduledGeneration = generation_;
    QTimer::singleShot(delayMs, this, [this, scheduledGeneration] {
        if (scheduledGeneration == generation_)
        {
            onStateTimer();
        }
    });
}

void Session::onStateTimer()
{
    if (state_ == State::ReadingLiveStartRetrySettings && !exchange_.pending)
    {
        if (!connected_ || !captureRun_.live || cancelRequested_)
        {
            completeOperation(QStringLiteral("Operation cancelled"));
            return;
        }
        settingsTimer_.stop();
        ++liveStartRetry_.settingsReads;
        sendCommand(protocol::Opcode::GetSettings);
        return;
    }
    if (state_ == State::CaptureStatus && !exchange_.pending)
    {
        pollCaptureStatus();
        return;
    }
    if (state_ == State::SeriesGap)
    {
        if (cancelRequested_)
        {
            beginCleanup();
            return;
        }
        startNextCapture();
        return;
    }
    if (state_ == State::BeginFrame && completionClock_.isValid() && !exchange_.pending)
    {
        if (completionClock_.elapsed() >= kCaptureReadinessMs)
            failOperation(QStringLiteral("No exportable held frame is available"));
        else
            requestFrame();
    }
}

} // namespace dso
