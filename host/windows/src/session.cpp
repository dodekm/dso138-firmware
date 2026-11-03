#include "session.hpp"

#include <QThread>
#include <QtEndian>
#include <algorithm>
#include <limits>
#include <utility>

namespace dso
{
namespace
{
constexpr int kRetryIntervalMs = 250;
constexpr int kMaximumRetries = 2;
constexpr int kCaptureReadinessMs = 1000;
constexpr int kStatusPollMs = 20;
constexpr int kSettingsPollMs = 350;
constexpr int kLiveSettingsPollMs = 500;
constexpr int kLiveStartBusyRetryDelayMs = 150;

quint16 read16(const QByteArray &bytes, qsizetype offset)
{
    return qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(bytes.constData() + offset));
}

quint32 read32(const QByteArray &bytes, qsizetype offset)
{
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(bytes.constData() + offset));
}

QByteArray littleEndian16(quint16 value)
{
    QByteArray bytes(2, '\0');
    qToLittleEndian(value, reinterpret_cast<uchar *>(bytes.data()));
    return bytes;
}

QByteArray littleEndian32(quint32 value)
{
    QByteArray bytes(4, '\0');
    qToLittleEndian(value, reinterpret_cast<uchar *>(bytes.data()));
    return bytes;
}

class SerialTransport final : public ByteTransport
{
  public:
    explicit SerialTransport(QObject *parent = nullptr) : ByteTransport(parent)
    {
        port_.setReadBufferSize(4096);
        connect(&port_, &QSerialPort::readyRead, this, [this] { emit readyRead(); });
        connect(&port_, &QSerialPort::errorOccurred, this,
                [this](QSerialPort::SerialPortError error) {
                    if (port_.isOpen() && error != QSerialPort::NoError &&
                        error != QSerialPort::TimeoutError)
                    {
                        emit lost(port_.errorString());
                    }
                });
    }

    bool open(const QString &portName) override
    {
        if (port_.isOpen())
        {
            port_.close();
        }
        port_.setPortName(portName);
        port_.setBaudRate(QSerialPort::Baud115200);
        const bool opened = port_.open(QIODevice::ReadWrite);
        return opened;
    }

    void close() override
    {
        if (port_.isOpen())
        {
            port_.close();
        }
    }

    bool isOpen() const override
    {
        return port_.isOpen();
    }
    QByteArray readAll() override
    {
        return port_.readAll();
    }

    qint64 write(const QByteArray &bytes) override
    {
        if (!port_.isOpen())
        {
            emit lost(QStringLiteral("Serial port is closed"));
            return -1;
        }
        const qint64 accepted = port_.write(bytes);
        if (accepted != bytes.size())
        {
            emit lost(
                port_.errorString().isEmpty()
                    ? QStringLiteral("Serial write did not accept the complete protocol frame")
                    : port_.errorString());
            return -1;
        }
        return accepted;
    }

    QString errorString() const override
    {
        return port_.errorString();
    }

  private:
    QSerialPort port_;
};

Metadata publicMetadata(const protocol::Metadata &source)
{
    Metadata result;
    result.token = source.token;
    result.count = source.count;
    result.pretrigger = source.pretrigger;
    result.timerHz = source.timerHz;
    result.divider = source.divider;
    result.acquisitionEpoch = source.acquisitionEpoch;
    result.triggerSequence = source.triggerSequence;
    result.physicalGeneration = source.physicalGeneration;
    result.microvoltsPerDivision = source.microvoltsPerDivision;
    result.zeroCodeQ16 = source.zeroCodeQ16;
    result.gainQ16 = source.gainQ16;
    result.gainShift = source.gainShift;
    result.calibrationKind = source.calibrationKind;
    result.coupling = source.coupling;
    result.triggerMode = source.triggerMode;
    result.triggerEdge = source.triggerEdge;
    result.flags = source.flags;
    result.timebaseIndex = source.timebaseIndex;
    result.timebaseCatalogueId = source.timebaseCatalogueId;
    result.triggerLevel = source.triggerLevel;
    result.extendedSettingsValid = source.extendedSettingsValid;
    return result;
}
} // namespace

Session::Session(QObject *parent) : Session(std::make_unique<SerialTransport>(), parent)
{
}

Session::Session(std::unique_ptr<ByteTransport> transport, QObject *parent)
    : QObject(parent), transport_(std::move(transport))
{
    qRegisterMetaType<Capture>();
    qRegisterMetaType<protocol::Settings>();
    qRegisterMetaType<protocol::SettingsAvailability>();

    exchangeTimer_.setInterval(10);
    exchangeTimer_.setTimerType(Qt::PreciseTimer);
    connect(&exchangeTimer_, &QTimer::timeout, this, &Session::onExchangeTimer);
    settingsTimer_.setSingleShot(true);
    settingsTimer_.setTimerType(Qt::CoarseTimer);
    connect(&settingsTimer_, &QTimer::timeout, this, &Session::requestSettings);
    connect(transport_.get(), &ByteTransport::readyRead, this, &Session::onReadyRead);
    connect(transport_.get(), &ByteTransport::lost, this, &Session::onTransportLost);
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

bool Session::settingsReadInProgress() const noexcept
{
    return state_ == State::ReadingSettings;
}

bool Session::cancellable() const noexcept
{
    return busy_ && state_ != State::ReadingSettings && state_ != State::ReadingLiveSettings &&
           state_ != State::WritingSettings;
}

void Session::connectPort(QString portName)
{
    disconnectPort();
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
    sendCommand(1);
}

void Session::disconnectPort()
{
    ++generation_;
    exchangeTimer_.stop();
    settingsTimer_.stop();
    if (transportOpen_)
    {
        transportOpen_ = false;
        transport_->close();
    }
    if (connected_)
    {
        connected_ = false;
        emit connectionChanged(false);
    }
    resetProtocolState();
    state_ = State::Disconnected;
    setBusy(false);
    settingsAvailability_ = protocol::SettingsAvailability::Unknown;
    instrumentSettings_ = {};
    emit instrumentSettingsChanged(instrumentSettings_, settingsAvailability_);
}

void Session::resetProtocolState()
{
    settingsTimer_.stop();
    if (liveCapture_)
        emit liveStateChanged(false, QStringLiteral("Disconnected"));
    liveCapture_ = false;
    trackHostInterval_ = false;
    extendedMetadataAvailable_ = false;
    recordPublishClock_.invalidate();
    liveSettingsClock_.invalidate();
    queuedSettingsWrite_.reset();
    decoder_.clear();
    exchange_ = {};
    jobId_ = 0;
    exportToken_ = 0;
    metadata_ = {};
    frame_ = {};
    sampleOffset_ = 0;
    captureTimeoutMs_ = 0;
    seriesRemaining_ = 0;
    seriesTotal_ = 0;
    seriesIntervalMs_ = 0;
    completedCaptures_ = 0;
    acquisitionDurationMs_ = -1;
    cancelRequested_ = false;
    jobCompleteVerified_ = false;
    pendingFailure_.clear();
    captureClock_.invalidate();
    completionClock_.invalidate();
}

void Session::downloadHeld()
{
    if (!connected_ || busy_)
    {
        return;
    }
    setBusy(true);
    cancelRequested_ = false;
    pendingFailure_.clear();
    seriesRemaining_ = 1;
    seriesTotal_ = 1;
    completedCaptures_ = 0;
    jobId_ = 0;
    jobCompleteVerified_ = false;
    acquisitionDurationMs_ = -1;
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
    if (settingsAvailability_ != protocol::SettingsAvailability::Supported ||
        (instrumentSettings_.flags & 0x08U) == 0)
    {
        emit message(QStringLiteral("Selected-mode live capture requires firmware capability support"));
        return;
    }
    if (instrumentSettings_.uiState != 0 || (instrumentSettings_.flags & 0x04U) == 0)
    {
        emit message(QStringLiteral("Wait for the scope to reach idle HOLD before starting live capture"));
        return;
    }
    if ((instrumentSettings_.flags & 0x02U) == 0)
    {
        emit message(QStringLiteral("Wait for the scope's physical switches to settle"));
        return;
    }
    liveCapture_ = true;
    liveStartBusyRetryUsed_ = false;
    liveStartRetryRevision_ = instrumentSettings_.revision;
    trackHostInterval_ = true;
    recordPublishClock_.invalidate();
    liveSettingsClock_.restart();
    seriesRemaining_ = 1;
    seriesTotal_ = 0;
    seriesIntervalMs_ = 0;
    completedCaptures_ = 0;
    captureTimeoutMs_ = timeoutMs;
    cancelRequested_ = false;
    pendingFailure_.clear();
    setBusy(true);
    const QString mode = instrumentSettings_.triggerMode == 0   ? QStringLiteral("Normal")
                         : instrumentSettings_.triggerMode == 1 ? QStringLiteral("Auto")
                                                                : QStringLiteral("Single");
    emit liveStateChanged(true, mode);
    startNextCapture();
}

void Session::stopLive()
{
    if (!liveCapture_)
        return;
    cancelRequested_ = true;
    ++generation_;
    if (!exchange_.pending)
        beginCleanup();
}

void Session::setTimebase(int index)
{
    if (index < 0 || !canSubmitSettings() ||
        (instrumentSettings_.writableMask & 0x01U) == 0 || (instrumentSettings_.flags & 0x04U) == 0 ||
        instrumentSettings_.uiState != 0 ||
        instrumentSettings_.catalogueId != 1 || instrumentSettings_.catalogueCount == 0)
    {
        return;
    }
    const int count = std::min<int>(18, instrumentSettings_.catalogueCount);
    if (index >= count)
    {
        return;
    }
    QByteArray values;
    values.append(char(index));
    values.append(char(instrumentSettings_.triggerMode));
    submitSettingsWrite(9, std::move(values));
}

void Session::setTriggerMode(int mode)
{
    if (mode < 0 || mode > 2 || !canSubmitSettings() ||
        (instrumentSettings_.writableMask & 0x02U) == 0 || (instrumentSettings_.flags & 0x04U) == 0 ||
        instrumentSettings_.uiState != 0)
    {
        return;
    }
    QByteArray values;
    values.append(char(instrumentSettings_.timebaseIndex));
    values.append(char(mode));
    submitSettingsWrite(9, std::move(values));
}

void Session::setTrigger(int edge, int level, int positionPermille)
{
    if (edge < 0 || edge > 1 || level < 0 || level > 4095 ||
        positionPermille < 0 || positionPermille > 1000 || !canSubmitSettings() ||
        (instrumentSettings_.writableMask & 0x1cU) != 0x1cU ||
        (instrumentSettings_.flags & 0x04U) == 0 || instrumentSettings_.uiState != 0)
        return;
    QByteArray values(1, char(edge));
    values += littleEndian16(static_cast<quint16>(level));
    values += littleEndian16(static_cast<quint16>(positionPermille));
    submitSettingsWrite(12, std::move(values));
}

void Session::holdLocalRun()
{
    if (canSubmitSettings() && (instrumentSettings_.writableMask & 0x20U) != 0 &&
        (instrumentSettings_.flags & 0x10U) != 0)
        submitSettingsWrite(13);
}

bool Session::canSubmitSettings() const noexcept
{
    return connected_ && settingsAvailability_ == protocol::SettingsAvailability::Supported &&
           ((state_ == State::Idle && !busy_) || state_ == State::ReadingSettings);
}

void Session::submitSettingsWrite(quint8 opcode, QByteArray values)
{
    const SettingsWrite write{opcode, instrumentSettings_.revision, std::move(values)};
    if (state_ == State::ReadingSettings)
    {
        if (queuedSettingsWrite_)
        {
            emit message(QStringLiteral("Wait for the previous settings change to finish"));
            return;
        }
        queuedSettingsWrite_ = write;
        return;
    }
    startSettingsWrite(write);
}

void Session::startSettingsWrite(const SettingsWrite &write)
{
    settingsTimer_.stop();
    state_ = State::WritingSettings;
    setBusy(true);
    sendCommand(write.opcode, littleEndian16(write.revision) + write.values);
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

    setBusy(true);
    cancelRequested_ = false;
    pendingFailure_.clear();
    seriesRemaining_ = count;
    seriesTotal_ = count;
    seriesIntervalMs_ = intervalMs;
    completedCaptures_ = 0;
    liveCapture_ = false;
    trackHostInterval_ = count > 1;
    recordPublishClock_.invalidate();
    captureTimeoutMs_ = timeoutMs;
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
    exportToken_ = 0;
    jobCompleteVerified_ = false;
    completionClock_.invalidate();
    captureClock_.restart();
    acquisitionDurationMs_ = -1;
    state_ = State::CaptureAcknowledgement;
    sendCommand(liveCapture_ ? 0x0a : 6, littleEndian32(quint32(captureTimeoutMs_)));
}

void Session::sendCommand(quint8 opcode, const QByteArray &payload)
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
    const QByteArray frame = protocol::encode(opcode, requestId, payload);
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
    const auto reply = decoder_.take(exchange_.id, quint8(exchange_.opcode | 0x80U));
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
    const int nextRetryAt = (exchange_.retries + 1) * kRetryIntervalMs;
    if (exchangeClock_.elapsed() < nextRetryAt)
    {
        return;
    }
    if (exchange_.retries >= kMaximumRetries)
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

void Session::handleReply(const protocol::Reply &reply)
{
    if (reply.payload.isEmpty() || quint8(reply.payload[0]) != quint8(reply.status))
    {
        failOperation(QStringLiteral("Response omitted or corrupted its status byte"));
        return;
    }
    if (state_ == State::Handshaking)
    {
        handleInfo(reply);
        return;
    }
    if (state_ == State::ReadingSettings)
    {
        handleSettings(reply);
        return;
    }
    if (state_ == State::ReadingLiveStartRetrySettings)
    {
        handleLiveStartRetrySettings(reply);
        return;
    }
    if (state_ == State::ReadingLiveSettings)
    {
        handleLiveSettings(reply);
        return;
    }
    if (state_ == State::WritingSettings)
    {
        handleSettingsWrite(reply);
        return;
    }

    if (state_ == State::CaptureAcknowledgement && reply.status == protocol::Status::Busy &&
        liveCapture_ && completedCaptures_ == 0 && !liveStartBusyRetryUsed_ &&
        reply.payload.size() == 1)
    {
        liveStartBusyRetryUsed_ = true;
        liveStartRetrySettingsReads_ = 0;
        liveStartRetryRevision_ = instrumentSettings_.revision;
        state_ = State::ReadingLiveStartRetrySettings;
        emit message(QStringLiteral(
            "Live capture is waiting for foreground work; refreshing settings before one retry"));
        scheduleStateTimer(kLiveStartBusyRetryDelayMs);
        return;
    }
    if (state_ == State::CaptureAcknowledgement && reply.status == protocol::Status::Busy)
    {
        failOperation(QStringLiteral("Capture request is busy; put the scope in HOLD and wait for "
                                     "foreground work to finish"));
        return;
    }

    if (reply.status == protocol::Status::Timeout &&
        (state_ == State::CaptureStatus || state_ == State::VerifyLeasedFrame))
    {
        jobId_ = 0; // Firmware has already retired the expired owned job.
        failOperation(QStringLiteral("The device capture timed out"));
        return;
    }
    if (reply.status == protocol::Status::Busy && state_ == State::BeginFrame)
    {
        if (jobId_ != 0 && completionClock_.isValid() &&
            completionClock_.elapsed() < kCaptureReadinessMs && !cancelRequested_)
        {
            scheduleStateTimer(15);
            return;
        }
        failOperation(QStringLiteral("No exportable held frame is available"));
        return;
    }
    if (state_ == State::EndFrame && reply.status != protocol::Status::Ok)
    {
        // The lease may already have been revoked by expiry or a local action.
        // Treat it as released so cleanup cannot loop on a stale token.
        exportToken_ = 0;
        failOperation(QStringLiteral("Device could not release the frame lease"));
        return;
    }
    if (state_ == State::CancelCapture && reply.status != protocol::Status::Ok)
    {
        jobId_ = 0;
        completeOperation(pendingFailure_.isEmpty() ? QStringLiteral("Operation cancelled")
                                                    : pendingFailure_);
        return;
    }
    if (reply.status != protocol::Status::Ok)
    {
        failOperation(QStringLiteral("Device returned status %1 for opcode %2")
                          .arg(quint8(reply.status))
                          .arg(reply.opcode & 0x7f));
        return;
    }

    switch (state_)
    {
    case State::Handshaking:
        handleInfo(reply);
        break;
    case State::ReadingSettings:
        handleSettings(reply);
        break;
    case State::ReadingLiveSettings:
        handleLiveSettings(reply);
        break;
    case State::WritingSettings:
        handleSettingsWrite(reply);
        break;
    case State::CaptureAcknowledgement:
        if (reply.payload.size() != 5)
        {
            failOperation(QStringLiteral("CAPTURE_SINGLE response must contain one job ID"));
            return;
        }
        jobId_ = read32(reply.payload, 1);
        if (jobId_ == 0)
        {
            failOperation(QStringLiteral("Device returned an invalid zero capture job ID"));
            return;
        }
        if (cancelRequested_)
        {
            beginCleanup();
            return;
        }
        pollCaptureStatus();
        break;
    case State::CaptureStatus:
    case State::VerifyLeasedFrame:
        handleStatus(reply);
        break;
    case State::BeginFrame:
        handleBegin(reply);
        break;
    case State::ReadSamples:
        handleRead(reply);
        break;
    case State::EndFrame:
        handleEnd(reply);
        break;
    case State::CancelCapture:
        handleCancel(reply);
        break;
    case State::Idle:
    case State::SeriesGap:
    case State::Disconnected:
        failOperation(QStringLiteral("Received a response outside an active operation"));
        break;
    }
}

void Session::handleInfo(const protocol::Reply &reply)
{
    const QByteArray &p = reply.payload;
    if (reply.status != protocol::Status::Ok || p.size() != 8 || quint8(p[1]) != 1 ||
        read16(p, 2) != 1024 || quint8(p[4]) != 12 || quint8(p[5]) != 22 || read16(p, 6) != 0)
    {
        closeTransport(QStringLiteral("Device INFO does not match waveform protocol v1"), true);
        return;
    }

    state_ = State::Idle;
    connected_ = true;
    setBusy(false);
    emit connectionChanged(true);
    emit message(QStringLiteral("Connected to DSO waveform protocol v1; reading instrument settings…"));
    requestSettings();
}

void Session::requestSettings()
{
    if (!transportOpen_ || !connected_ || busy_ || state_ != State::Idle || exchange_.pending ||
        settingsAvailability_ == protocol::SettingsAvailability::Legacy)
    {
        return;
    }
    settingsTimer_.stop();
    state_ = State::ReadingSettings;
    setBusy(true);
    sendCommand(8);
}

void Session::scheduleSettingsPoll()
{
    if (transportOpen_ && connected_ && state_ == State::Idle &&
        (settingsAvailability_ == protocol::SettingsAvailability::Supported ||
         settingsAvailability_ == protocol::SettingsAvailability::Stale))
    {
        settingsTimer_.start(kSettingsPollMs);
    }
}

void Session::handleSettings(const protocol::Reply &reply)
{
    const auto queued = std::exchange(queuedSettingsWrite_, std::nullopt);
    if (reply.status == protocol::Status::Unsupported)
    {
        settingsAvailability_ = protocol::SettingsAvailability::Legacy;
        state_ = State::Idle;
        setBusy(false);
        emit instrumentSettingsChanged(instrumentSettings_, settingsAvailability_);
        emit message(QStringLiteral("Connected to legacy firmware; live settings readback is unavailable"));
        return;
    }

    if (reply.status != protocol::Status::Ok)
    {
        settingsAvailability_ = protocol::SettingsAvailability::Stale;
        state_ = State::Idle;
        setBusy(false);
        emit instrumentSettingsChanged(instrumentSettings_, settingsAvailability_);
        emit message(QStringLiteral("Instrument settings readback failed with status %1; retrying")
                         .arg(quint8(reply.status)));
        scheduleSettingsPoll();
        return;
    }

    protocol::Settings decoded;
    QString error;
    if (!protocol::decodeSettings(reply.payload, decoded, &error))
    {
        settingsAvailability_ = protocol::SettingsAvailability::Stale;
        state_ = State::Idle;
        setBusy(false);
        emit instrumentSettingsChanged(instrumentSettings_, settingsAvailability_);
        emit message(QStringLiteral("Instrument settings response is invalid: %1").arg(error));
        scheduleSettingsPoll();
        return;
    }
    instrumentSettings_ = decoded;
    settingsAvailability_ = protocol::SettingsAvailability::Supported;
    extendedMetadataAvailable_ = (decoded.flags & 0x08U) != 0;
    state_ = State::Idle;
    setBusy(false);
    emit instrumentSettingsChanged(instrumentSettings_, settingsAvailability_);
    if (queued && queued->revision == decoded.revision && state_ == State::Idle && connected_)
    {
        startSettingsWrite(*queued);
        return;
    }
    if (queued)
        emit message(QStringLiteral("Instrument settings changed before the write. Review the applied values."));
    scheduleSettingsPoll();
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
        settingsAvailability_ = protocol::SettingsAvailability::Legacy;
        emit instrumentSettingsChanged(instrumentSettings_, settingsAvailability_);
        completeOperation(QStringLiteral("Live settings readback capability was withdrawn"));
        return;
    }

    protocol::Settings refreshed;
    QString error;
    if (reply.status != protocol::Status::Ok ||
        !protocol::decodeSettings(reply.payload, refreshed, &error))
    {
        settingsAvailability_ = protocol::SettingsAvailability::Stale;
        emit instrumentSettingsChanged(instrumentSettings_, settingsAvailability_);
        completeOperation(reply.status == protocol::Status::Ok
                              ? QStringLiteral("Live retry settings readback is invalid: %1").arg(error)
                              : QStringLiteral("Live retry settings readback failed with status %1")
                                    .arg(quint8(reply.status)));
        return;
    }

    instrumentSettings_ = refreshed;
    settingsAvailability_ = protocol::SettingsAvailability::Supported;
    extendedMetadataAvailable_ = (refreshed.flags & 0x08U) != 0;
    emit instrumentSettingsChanged(instrumentSettings_, settingsAvailability_);
    if (state_ != State::ReadingLiveStartRetrySettings || !connected_ || !liveCapture_ ||
        cancelRequested_)
    {
        return;
    }

    if (refreshed.revision != liveStartRetryRevision_)
    {
        completeOperation(QStringLiteral(
            "Instrument settings changed while live start was waiting; review settings before retrying"));
        return;
    }
    if (refreshed.uiState != 0 || (refreshed.flags & 0x08U) == 0)
    {
        completeOperation(QStringLiteral(
            "Live retry cancelled because HOLD or live capability changed"));
        return;
    }

    if ((refreshed.flags & 0x06U) != 0x06U)
    {
        if (liveStartRetrySettingsReads_ < 7)
        {
            state_ = State::ReadingLiveStartRetrySettings;
            scheduleStateTimer(100);
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
            settingsAvailability_ = protocol::SettingsAvailability::Stale;
            state_ = State::Idle;
            setBusy(false);
            emit instrumentSettingsChanged(instrumentSettings_, settingsAvailability_);
            emit message(QStringLiteral("Settings acknowledgement is invalid: %1").arg(error));
            settingsTimer_.start(0);
            return;
        }
        instrumentSettings_ = applied;
        settingsAvailability_ = protocol::SettingsAvailability::Supported;
        extendedMetadataAvailable_ = (applied.flags & 0x08U) != 0;
        state_ = State::Idle;
        setBusy(false);
        emit instrumentSettingsChanged(instrumentSettings_, settingsAvailability_);
        emit message(QStringLiteral("Device applied the acknowledged settings"));
        scheduleSettingsPoll();
        return;
    }

    // Rejections are terminal for this intent. Keep the last acknowledged
    // snapshot visible, mark it stale, and fetch current state without
    // resubmitting the write under a new request ID.
    const bool statusOnly = reply.payload.size() == 1;
    settingsAvailability_ = protocol::SettingsAvailability::Stale;
    state_ = State::Idle;
    setBusy(false);
    emit instrumentSettingsChanged(instrumentSettings_, settingsAvailability_);
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
        settingsAvailability_ = protocol::SettingsAvailability::Legacy;
        emit instrumentSettingsChanged(instrumentSettings_, settingsAvailability_);
        completeOperation(QStringLiteral("Live settings readback capability was withdrawn"));
        return;
    }

    protocol::Settings decoded;
    QString error;
    if (reply.status != protocol::Status::Ok || !protocol::decodeSettings(reply.payload, decoded, &error))
    {
        settingsAvailability_ = protocol::SettingsAvailability::Stale;
        emit instrumentSettingsChanged(instrumentSettings_, settingsAvailability_);
        liveSettingsClock_.restart();
        emit message(reply.status == protocol::Status::Ok
                         ? QStringLiteral("Between-record settings readback is invalid: %1").arg(error)
                         : QStringLiteral("Between-record settings readback failed with status %1")
                               .arg(quint8(reply.status)));
        startNextCapture();
        return;
    }
    instrumentSettings_ = decoded;
    settingsAvailability_ = protocol::SettingsAvailability::Supported;
    extendedMetadataAvailable_ = (decoded.flags & 0x08U) != 0;
    liveSettingsClock_.restart();
    emit instrumentSettingsChanged(instrumentSettings_, settingsAvailability_);
    startNextCapture();
}

void Session::pollCaptureStatus()
{
    if (!transportOpen_ || jobId_ == 0 || exchange_.pending)
    {
        return;
    }
    state_ = State::CaptureStatus;
    sendCommand(2, littleEndian32(jobId_));
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
    const QByteArray &p = reply.payload;
    if (p.size() != 13 || jobId_ == 0 || read32(p, 5) != jobId_)
    {
        failOperation(QStringLiteral("STATUS response is malformed or belongs to another job"));
        return;
    }

    const quint8 controllerFault = quint8(p[2]);
    const quint8 state = quint8(p[4]);
    const quint8 frameAvailable = quint8(p[9]);
    const quint16 count = read16(p, 10);
    const quint8 storageBusy = quint8(p[12]);
    if (controllerFault != 0 || state > 4 || frameAvailable > 1 || storageBusy > 1 ||
        (frameAvailable && (count == 0 || count > 1024)))
    {
        failOperation(QStringLiteral("STATUS contains an invalid controller state or frame count"));
        return;
    }
    if (state == 0)
    {
        failOperation(QStringLiteral("Device no longer recognizes the capture job"));
        return;
    }
    if (state == 3)
    {
        jobId_ = 0;
        failOperation(QStringLiteral("Local instrument activity cancelled the capture"));
        return;
    }
    if (state == 4)
    {
        jobId_ = 0;
        failOperation(QStringLiteral("The device capture failed"));
        return;
    }

    if (cancelRequested_)
    {
        if (state == 2)
        {
            jobCompleteVerified_ = true;
        }
        beginCleanup();
        return;
    }
    if (state == 1)
    {
        if (captureClock_.elapsed() > captureTimeoutMs_ + 1500)
        {
            failOperation(QStringLiteral("Timed out waiting for capture completion"));
            return;
        }
        armNextStatusPoll(kStatusPollMs);
        return;
    }

    if (!jobCompleteVerified_)
    {
        acquisitionDurationMs_ = captureClock_.isValid() ? captureClock_.elapsed() : -1;
    }
    jobCompleteVerified_ = true;
    if (!completionClock_.isValid())
    {
        completionClock_.start();
    }
    if (state_ == State::VerifyLeasedFrame)
    {
        const bool stillMatches = p[1] == 0 && quint8(p[3]) == 0 && frameAvailable == 1 &&
                                  storageBusy == 0 && count == metadata_.count;
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

void Session::requestFrame()
{
    if (!transportOpen_ || exchange_.pending)
    {
        return;
    }
    state_ = State::BeginFrame;
    if (!downloadClock_.isValid())
    {
        downloadClock_.start();
    }
    sendCommand(extendedMetadataAvailable_ ? 0x0b : 3);
}

void Session::handleBegin(const protocol::Reply &reply)
{
    if ((reply.payload.size() == 48 || reply.payload.size() == 52) && quint8(reply.payload[0]) == 0)
    {
        exportToken_ = read32(reply.payload, 1);
    }
    const bool decoded = reply.payload.size() == 52
                             ? protocol::decodeExtendedMetadata(reply.payload, metadata_, nullptr)
                             : protocol::decodeMetadata(reply.payload, metadata_, nullptr);
    if (!decoded)
    {
        failOperation(QStringLiteral("BEGIN_FRAME metadata is invalid"));
        return;
    }
    if (metadata_.extendedSettingsValid)
        metadata_.timebaseCatalogueId = instrumentSettings_.catalogueId;
    QString validationError;
    if (!validateMetadata(publicMetadata(metadata_), &validationError))
    {
        failOperation(
            QStringLiteral("Frame calibration or metadata is invalid: %1").arg(validationError));
        return;
    }
    exportToken_ = metadata_.token;

    if (cancelRequested_)
    {
        beginCleanup();
        return;
    }
    if (jobId_ != 0)
    {
        state_ = State::VerifyLeasedFrame;
        sendCommand(2, littleEndian32(jobId_));
        return;
    }
    prepareFrameBuffer();
}

void Session::prepareFrameBuffer()
{
    frame_ = {};
    frame_.metadata = publicMetadata(metadata_);
    frame_.raw.resize(metadata_.count);
    sampleOffset_ = 0;
    requestNextChunk();
}

void Session::requestNextChunk()
{
    if (cancelRequested_)
    {
        beginCleanup();
        return;
    }
    if (sampleOffset_ >= metadata_.count)
    {
        requestEndFrame();
        return;
    }

    const quint8 count = quint8(std::min<int>(22, metadata_.count - sampleOffset_));
    QByteArray payload = littleEndian32(exportToken_);
    payload += littleEndian16(sampleOffset_);
    payload.append(char(count));
    state_ = State::ReadSamples;
    sendCommand(4, payload);
}

void Session::handleRead(const protocol::Reply &reply)
{
    const QByteArray &p = reply.payload;
    if (p.size() < 8 || exportToken_ == 0 || read32(p, 1) != exportToken_ ||
        read16(p, 5) != sampleOffset_)
    {
        failOperation(QStringLiteral("READ_SAMPLES response does not match the requested range"));
        return;
    }
    const quint8 count = quint8(p[7]);
    const quint8 expected = quint8(std::min<int>(22, metadata_.count - sampleOffset_));
    if (count == 0 || count != expected || p.size() != 8 + 2 * count ||
        sampleOffset_ + count > metadata_.count)
    {
        failOperation(QStringLiteral("READ_SAMPLES returned an invalid chunk length"));
        return;
    }
    for (quint8 i = 0; i < count; ++i)
    {
        const quint16 sample = read16(p, 8 + 2 * i);
        if (sample > 4095)
        {
            failOperation(QStringLiteral("READ_SAMPLES returned a code outside 0..4095"));
            return;
        }
        frame_.raw[sampleOffset_ + i] = sample;
    }
    sampleOffset_ += count;
    requestNextChunk();
}

void Session::requestEndFrame()
{
    if (exportToken_ == 0)
    {
        failOperation(QStringLiteral("Cannot release a frame without an export token"));
        return;
    }
    state_ = State::EndFrame;
    sendCommand(5, littleEndian32(exportToken_));
}

void Session::handleEnd(const protocol::Reply &reply)
{
    if (reply.payload.size() != 1)
    {
        exportToken_ = 0;
        failOperation(QStringLiteral("END_FRAME response has an unexpected payload"));
        return;
    }
    exportToken_ = 0;
    if (cancelRequested_ || !pendingFailure_.isEmpty())
    {
        beginCleanup();
        return;
    }
    publishFrame();
}

void Session::publishFrame()
{
    QString validationError;
    if (!validateCapture(frame_, &validationError))
    {
        failOperation(
            QStringLiteral("Downloaded frame failed validation: %1").arg(validationError));
        return;
    }

    const qint64 acquisitionMs = acquisitionDurationMs_;
    const qint64 downloadMs = downloadClock_.isValid() ? downloadClock_.elapsed() : 0;
    if (trackHostInterval_)
    {
        if (recordPublishClock_.isValid())
        {
            const qint64 elapsed = recordPublishClock_.elapsed();
            frame_.metadata.hostIntervalBeforeMs = static_cast<std::uint32_t>(
                std::min<qint64>(elapsed, std::numeric_limits<std::uint32_t>::max()));
        }
        else
        {
            frame_.metadata.hostIntervalBeforeMs = 0xffffffffU;
        }
        recordPublishClock_.restart();
    }
    const Capture published = frame_;
    ++completedCaptures_;
    const bool liveSingle = liveCapture_ && frame_.metadata.triggerMode == 2;
    if (!liveCapture_)
        --seriesRemaining_;
    exportToken_ = 0;
    jobId_ = 0;
    downloadClock_.invalidate();

    if (liveCapture_ && !liveSingle && !cancelRequested_)
    {
        if (liveSettingsClock_.isValid() && liveSettingsClock_.elapsed() >= kLiveSettingsPollMs)
        {
            state_ = State::ReadingLiveSettings;
            sendCommand(8);
        }
        else
        {
            state_ = State::SeriesGap;
            scheduleStateTimer(0);
        }
    }
    else if (!liveCapture_ && seriesRemaining_ > 0 && !cancelRequested_)
    {
        state_ = State::SeriesGap;
        scheduleStateTimer(seriesIntervalMs_);
    }
    else
    {
        completeOperation(liveSingle ? QStringLiteral("Single-mode live capture completed")
                                     : QString());
    }
    // Signals can invoke cancel() synchronously. Set the next state first so a
    // cancellation from a frame/progress handler invalidates the scheduled gap.
    emit frameReady(published);
    emit captureTimes(acquisitionMs, downloadMs);
    if (!liveCapture_ && seriesTotal_ > 0)
        emit progress(completedCaptures_, seriesTotal_);
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
    if (exportToken_ != 0)
    {
        state_ = State::EndFrame;
        sendCommand(5, littleEndian32(exportToken_));
        return;
    }
    if (jobId_ != 0 && !jobCompleteVerified_)
    {
        state_ = State::CancelCapture;
        sendCommand(7, littleEndian32(jobId_));
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
    const bool wasLive = liveCapture_;
    state_ = connected_ ? State::Idle : State::Disconnected;
    jobId_ = 0;
    exportToken_ = 0;
    seriesRemaining_ = 0;
    cancelRequested_ = false;
    jobCompleteVerified_ = false;
    pendingFailure_.clear();
    liveCapture_ = false;
    trackHostInterval_ = false;
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
        if (!connected_ || !liveCapture_ || cancelRequested_)
        {
            completeOperation(QStringLiteral("Operation cancelled"));
            return;
        }
        settingsTimer_.stop();
        ++liveStartRetrySettingsReads_;
        sendCommand(8);
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
    if (state_ == State::BeginFrame && jobId_ != 0 && !exchange_.pending)
    {
        requestFrame();
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
    const bool wasOpen = transportOpen_;
    transportOpen_ = false;
    if (wasOpen)
    {
        transport_->close();
    }
    const bool wasConnected = connected_;
    connected_ = false;
    resetProtocolState();
    state_ = State::Disconnected;
    setBusy(false);
    if (wasConnected)
    {
        emit connectionChanged(false);
    }
    settingsAvailability_ = protocol::SettingsAvailability::Unknown;
    instrumentSettings_ = {};
    emit instrumentSettingsChanged(instrumentSettings_, settingsAvailability_);
    if (report && !reason.isEmpty())
    {
        emit message(reason);
    }
}
} // namespace dso
