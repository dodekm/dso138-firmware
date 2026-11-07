#include "session.hpp"
#include "session_timing.hpp"

#include <utility>

namespace dso
{

using session_timing::kExchangeRetryIntervalMs;
using session_timing::kExchangeTimerPollIntervalMs;
using session_timing::kMaximumExchangeRetransmissions;

Session::Session(std::unique_ptr<ByteTransport> transport, QObject *parent)
    : QObject(parent), transport_(std::move(transport))
{
    qRegisterMetaType<Capture>();
    qRegisterMetaType<protocol::Settings>();
    qRegisterMetaType<protocol::SettingsAvailability>();

    exchangeTimer_.setInterval(kExchangeTimerPollIntervalMs);
    exchangeTimer_.setTimerType(Qt::PreciseTimer);
    connect(&exchangeTimer_, &QTimer::timeout, this, &Session::onExchangeTimer);
    settingsTimer_.setSingleShot(true);
    settingsTimer_.setTimerType(Qt::CoarseTimer);
    connect(&settingsTimer_, &QTimer::timeout, this, &Session::requestSettings);
    if (transport_)
    {
        connect(transport_.get(), &ByteTransport::readyRead, this, &Session::onReadyRead);
        connect(transport_.get(), &ByteTransport::lost, this, &Session::onTransportLost);
    }
}

void Session::setBusy(bool busy)
{
    if (busy_ == busy)
    {
        return;
    }
    busy_ = busy;
    emit busyChanged(busy_);
}

bool Session::cancellable() const noexcept
{
    return busy_ && state_ != State::ReadingSettings && state_ != State::ReadingLiveSettings &&
           state_ != State::WritingSettings;
}

void Session::connectPort(QString portName)
{
    disconnectPort();
    if (!transport_)
    {
        emit message(QStringLiteral("No byte transport is configured"));
        return;
    }
    if (!transport_->open(portName))
    {
        emit message(transport_->errorString());
        return;
    }

    ++generation_;
    transportOpen_ = true;
    state_ = State::Handshaking;
    setBusy(true);
    emit sessionInfo(portName);
    sendCommand(protocol::Opcode::Info);
}

void Session::disconnectPort()
{
    ++generation_;
    exchangeTimer_.stop();
    settingsTimer_.stop();
    closeTransportEndpoint();
    if (connected_)
    {
        connected_ = false;
        emit connectionChanged(false);
    }
    resetProtocolState();
    state_ = State::Disconnected;
    setBusy(false);
    resetInstrumentSettings();
}

void Session::resetProtocolState()
{
    settingsTimer_.stop();
    if (captureRun_.live)
        emit liveStateChanged(false, QStringLiteral("Disconnected"));
    captureRun_.live = false;
    captureRun_.trackHostInterval = false;
    recordPublishClock_.invalidate();
    liveSettingsClock_.invalidate();
    deferredAction_.reset();
    decoder_.clear();
    exchange_ = {};
    jobId_ = 0;
    frameTransfer_ = {};
    captureRun_.timeoutMs = 0;
    captureRun_.remaining = 0;
    captureRun_.total = 0;
    captureRun_.intervalMs = 0;
    captureRun_.completed = 0;
    captureRun_.acquisitionDurationMs = -1;
    cancelRequested_ = false;
    jobCompleteVerified_ = false;
    pendingFailure_.clear();
    captureClock_.invalidate();
    completionClock_.invalidate();
    downloadClock_.invalidate();
}

void Session::resetInstrumentSettings()
{
    instrument_.availability = protocol::SettingsAvailability::Unknown;
    instrument_.settings = {};
    emit instrumentSettingsChanged(instrument_.settings, instrument_.availability);
}

void Session::sendCommand(protocol::Opcode opcode, const QByteArray &payload)
{
    if (!transportOpen_ || exchange_.pending)
    {
        return;
    }
    const quint16 requestId = nextRequestId_++;
    if (nextRequestId_ == 0)
    {
        nextRequestId_ = 1;
    }
    const auto opcodeByte = static_cast<quint8>(opcode);
    const QByteArray frame = protocol::encode(opcodeByte, requestId, payload);
    if (frame.isEmpty())
    {
        failOperation(QStringLiteral("Could not encode protocol request"));
        return;
    }

    exchange_.pending = true;
    exchange_.id = requestId;
    exchange_.opcode = opcode;
    exchange_.bytes = frame;
    exchange_.retries = 0;
    exchangeClock_.restart();
    exchangeTimer_.start();

    if (transport_->write(exchange_.bytes) != exchange_.bytes.size())
    {
        // The transport loss handler closes the session. Keeping the exchange
        // intact here avoids starting a different command on an uncertain ID.
        onTransportLost(QStringLiteral("Could not write the complete protocol frame"));
    }
}

void Session::onReadyRead()
{
    if (!transportOpen_ || !exchange_.pending)
    {
        if (transportOpen_)
        {
            decoder_.append(transport_->readAll());
        }
        return;
    }
    decoder_.append(transport_->readAll());
    const auto responseOpcode = static_cast<quint8>(
        static_cast<quint8>(exchange_.opcode) | 0x80U);
    const auto reply = decoder_.take(exchange_.id, responseOpcode);
    if (!reply)
    {
        return;
    }

    exchangeTimer_.stop();
    exchange_ = {};
    handleReply(*reply);
}

void Session::onExchangeTimer()
{
    if (!exchange_.pending)
    {
        return;
    }
    const int nextRetryAt = (exchange_.retries + 1) * kExchangeRetryIntervalMs;
    if (exchangeClock_.elapsed() < nextRetryAt)
    {
        return;
    }
    if (exchange_.retries >= kMaximumExchangeRetransmissions)
    {
        // A command may have run even though its response was lost. Closing the
        // session discards its identity and lets firmware session cleanup revoke
        // a job/lease; no later command can be confused with this request.
        closeTransport(
            QStringLiteral(
                "Protocol response timed out; connection reset to clear the unresolved request"),
            true);
        return;
    }
    ++exchange_.retries;
    if (transport_->write(exchange_.bytes) != exchange_.bytes.size())
    {
        onTransportLost(QStringLiteral("Could not retransmit the complete protocol frame"));
    }
}

void Session::onTransportLost(const QString &reason)
{
    if (!transportOpen_)
    {
        return;
    }
    closeTransport(reason, true);
}

void Session::closeTransport(const QString &reason, bool report)
{
    ++generation_;
    exchangeTimer_.stop();
    closeTransportEndpoint();
    const bool wasConnected = connected_;
    connected_ = false;
    resetProtocolState();
    state_ = State::Disconnected;
    setBusy(false);
    if (wasConnected)
    {
        emit connectionChanged(false);
    }
    resetInstrumentSettings();
    if (report && !reason.isEmpty())
    {
        emit message(reason);
    }
}

void Session::closeTransportEndpoint()
{
    if (!transportOpen_)
    {
        return;
    }

    // The adapter may synchronously emit lost() while closing. Mark it closed
    // first so the reentrant handler cannot begin a second shutdown.
    transportOpen_ = false;
    transport_->close();
}
} // namespace dso
