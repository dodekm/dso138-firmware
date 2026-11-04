#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QSerialPort>
#include <QTimer>
#include <functional>
#include <memory>

#include "capture.hpp"
#include "protocol.hpp"

namespace dso
{

// Minimal injectable byte transport. Tests can provide a deterministic in-memory
// implementation; the production adapter is a single explicitly selected COM port.
class ByteTransport : public QObject
{
    Q_OBJECT
  public:
    using QObject::QObject;
    virtual bool open(const QString &portName) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    virtual QByteArray readAll() = 0;
    virtual qint64 write(const QByteArray &bytes) = 0;
    virtual QString errorString() const
    {
        return QStringLiteral("Transport open failed");
    }

  signals:
    void readyRead();
    void lost(QString reason);
};

class Session final : public QObject
{
    Q_OBJECT
  public:
    explicit Session(QObject *parent = nullptr);
    explicit Session(std::unique_ptr<ByteTransport> transport, QObject *parent = nullptr);

    bool connected() const noexcept
    {
        return connected_;
    }
    bool busy() const noexcept
    {
        return busy_;
    }
    bool liveCaptureActive() const noexcept { return liveCapture_; }
    bool repeatedCaptureActive() const noexcept { return seriesTotal_ > 1; }
    bool cancellable() const noexcept;

  public slots:
    void connectPort(QString port);
    void disconnectPort();
    void downloadHeld();
    void captureSingle(int timeoutMs);
    void captureRepeated(int count, int timeoutMs, int intervalMs);
    void startLive(int timeoutMs);
    void stopLive();
    void setTimebase(int index);
    void setTriggerMode(int mode);
    void setTrigger(int edge, int level, int positionPermille);
    void holdLocalRun();
    void cancel();

  signals:
    void connectionChanged(bool connected);
    void busyChanged(bool busy);
    void message(QString text);
    void frameReady(dso::Capture frame);
    void captureTimes(qint64 acquisitionMs, qint64 downloadMs);
    void progress(int completed, int total);
    void sessionInfo(QString text);
    void instrumentSettingsChanged(dso::protocol::Settings settings,
                                  dso::protocol::SettingsAvailability availability);
    void liveStateChanged(bool active, QString description);

  private slots:
    void onReadyRead();
    void onTransportLost(const QString &reason);
    void onExchangeTimer();
    void onStateTimer();

  private:
    enum class State
    {
        Disconnected,
        Handshaking,
        Idle,
        ReadingSettings,
        ReadingLiveStartRetrySettings,
        ReadingLiveSettings,
        WritingSettings,
        CaptureAcknowledgement,
        CaptureStatus,
        BeginFrame,
        VerifyLeasedFrame,
        ReadSamples,
        EndFrame,
        CancelCapture,
        SeriesGap
    };

    struct Exchange
    {
        bool pending{};
        quint16 id{};
        quint8 opcode{};
        QByteArray bytes;
        int retries{};
    };
    struct SettingsWrite
    {
        quint8 opcode{};
        quint16 revision{};
        QByteArray values;
    };

    void setBusy(bool busy);
    void sendCommand(quint8 opcode, const QByteArray &payload = {});
    void handleReply(const protocol::Reply &reply);
    void handleInfo(const protocol::Reply &reply);
    void handleSettings(const protocol::Reply &reply);
    void handleLiveSettings(const protocol::Reply &reply);
    void handleLiveStartRetrySettings(const protocol::Reply &reply);
    void handleSettingsWrite(const protocol::Reply &reply);
    bool canSubmitSettings() const noexcept;
    bool deferUntilSettingsRead(std::function<void()> action);
    void submitSettingsWrite(quint8 opcode, QByteArray values = {});
    void startSettingsWrite(const SettingsWrite &write);
    void requestSettings();
    void scheduleSettingsPoll();
    void handleStatus(const protocol::Reply &reply);
    void handleBegin(const protocol::Reply &reply);
    void handleRead(const protocol::Reply &reply);
    void handleEnd(const protocol::Reply &reply);
    void handleCancel(const protocol::Reply &reply);

    void beginCaptureSeries(int count, int timeoutMs, int intervalMs);
    void startNextCapture();
    void pollCaptureStatus();
    void requestFrame();
    void prepareFrameBuffer();
    void requestNextChunk();
    void requestEndFrame();
    void publishFrame();
    void beginCleanup();
    void completeOperation(const QString &message = {});
    void failOperation(const QString &message);
    void closeTransport(const QString &reason, bool report);
    void resetProtocolState();
    void armNextStatusPoll(int delayMs);
    void scheduleStateTimer(int delayMs);

    std::unique_ptr<ByteTransport> transport_;
    QTimer exchangeTimer_;
    QTimer settingsTimer_;
    QElapsedTimer exchangeClock_;
    QElapsedTimer captureClock_;
    QElapsedTimer completionClock_;
    QElapsedTimer downloadClock_;
    QElapsedTimer recordPublishClock_;
    QElapsedTimer liveSettingsClock_;
    protocol::Decoder decoder_;
    Exchange exchange_;
    State state_{State::Disconnected};
    quint16 nextRequestId_{1};
    quint32 jobId_{};
    quint32 exportToken_{};
    protocol::Metadata metadata_{};
    protocol::Settings instrumentSettings_{};
    protocol::SettingsAvailability settingsAvailability_{protocol::SettingsAvailability::Unknown};
    Capture frame_{};
    quint16 sampleOffset_{};
    int captureTimeoutMs_{};
    int seriesRemaining_{};
    int seriesTotal_{};
    int seriesIntervalMs_{};
    int completedCaptures_{};
    qint64 acquisitionDurationMs_{-1};
    bool transportOpen_{};
    bool connected_{};
    bool busy_{};
    bool cancelRequested_{};
    bool jobCompleteVerified_{};
    bool liveCapture_{};
    bool liveStartBusyRetryUsed_{};
    int liveStartRetrySettingsReads_{};
    quint16 liveStartRetryRevision_{};
    bool trackHostInterval_{};
    bool extendedMetadataAvailable_{};
    std::function<void()> deferredAction_;
    QString pendingFailure_;
    quint64 generation_{};
};

} // namespace dso

Q_DECLARE_METATYPE(dso::Capture)
