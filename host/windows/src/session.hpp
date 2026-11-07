#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QtGlobal>
#include <memory>
#include <optional>
#include <variant>

#include "byte_transport.hpp"
#include "capture_model.hpp"
#include "protocol.hpp"

namespace dso
{

// Event-driven protocol coordinator. It owns one transport and advances the
// handshake, settings and capture workflows from transport signals and timers.
// Keep Session on its construction thread: its value-member QTimers do not
// migrate with the QObject, and transport calls are synchronous on this thread.
class Session final : public QObject
{
    Q_OBJECT
  public:
    // Convenience for clients linking the production serial adapter. The
    // session core itself depends only on the injected-transport overload.
    explicit Session(QObject *parent = nullptr);
    // The composition root supplies the production or test transport; Session
    // owns it and remains independent of any concrete I/O backend.
    // A null transport is tolerated; connectPort() reports it and stays idle.
    explicit Session(std::unique_ptr<ByteTransport> transport, QObject *parent = nullptr);

    bool connected() const noexcept
    {
        return connected_;
    }
    bool busy() const noexcept
    {
        return busy_;
    }
    bool liveCaptureActive() const noexcept { return captureRun_.live; }
    // Series context stays latched through the final frameReady callback so
    // that record can be labeled after the session returns to idle. Use busy()
    // to query whether an operation is currently active.
    bool repeatedCaptureActive() const noexcept { return captureRun_.total > 1; }
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

    // At most one command is outstanding. Retries reuse this exact encoded
    // frame and ID; retry exhaustion closes the connection to avoid ambiguity.
    struct Exchange
    {
        bool pending{};
        quint16 id{};
        protocol::Opcode opcode{protocol::Opcode::Info};
        QByteArray bytes;
        int retries{};
    };
    struct SettingsWrite
    {
        protocol::Opcode opcode{protocol::Opcode::SetSettings};
        // Kept separately to reject a deferred write after stale readback;
        // payload already contains this same revision for the wire.
        quint16 revision{};
        QByteArray payload;
    };
    // One of these bounded actions may wait for the current settings readback.
    struct DeferredHeldDownload final
    {};
    struct DeferredLiveStart final
    {
        int timeoutMs{};
    };
    struct DeferredCaptureSeries final
    {
        int count{};
        int timeoutMs{};
        int intervalMs{};
    };
    struct DeferredSettingsWrite final
    {
        SettingsWrite write{};
    };
    using DeferredAction = std::variant<DeferredHeldDownload, DeferredLiveStart,
                                        DeferredCaptureSeries, DeferredSettingsWrite>;
    struct InstrumentState final
    {
        protocol::Settings settings{};
        protocol::SettingsAvailability availability{protocol::SettingsAvailability::Unknown};

        [[nodiscard]] bool supportsExtendedMetadata() const noexcept
        {
            return (settings.flags & protocol::settings_flags::ExtendedCapture) != 0;
        }
    };
    // Bounded working record for the single active leased-frame transfer.
    struct FrameTransferState final
    {
        quint32 exportToken{};
        protocol::Metadata metadata{};
        Capture capture{};
        quint16 sampleOffset{};
    };
    // State shared by one-shot, repeated, and live capture workflows.
    struct CaptureRunState final
    {
        int timeoutMs{};
        int remaining{};
        int total{};
        int intervalMs{};
        int completed{};
        qint64 acquisitionDurationMs{-1};
        bool live{};
        bool trackHostInterval{};
    };
    // Bounds the one settings-refresh retry after a live capture is Busy.
    struct LiveStartRetryState final
    {
        bool busyRetryUsed{};
        int settingsReads{};
        quint16 revision{};
    };

    void setBusy(bool busy);
    void sendCommand(protocol::Opcode opcode, const QByteArray &payload = {});
    void handleReply(const protocol::Reply &reply);
    void handleInfo(const protocol::Reply &reply);
    void handleSettings(const protocol::Reply &reply);
    void acceptInstrumentSettings(const protocol::Settings &settings);
    void handleLiveSettings(const protocol::Reply &reply);
    void handleLiveStartRetrySettings(const protocol::Reply &reply);
    void handleSettingsWrite(const protocol::Reply &reply);
    bool canSubmitSettings() const noexcept;
    bool canWriteSettings(quint8 requiredWritableMask) const noexcept;
    bool deferUntilSettingsRead(DeferredAction action);
    void runDeferredAction(const DeferredAction &action);
    void submitSettingsWrite(protocol::Opcode opcode, QByteArray payload);
    void startSettingsWrite(const SettingsWrite &write);
    void requestSettings();
    void scheduleSettingsPoll();
    void handleStatus(const protocol::Reply &reply);
    void handleBegin(const protocol::Reply &reply);
    void handleRead(const protocol::Reply &reply);
    void handleEnd(const protocol::Reply &reply);
    void handleCancel(const protocol::Reply &reply);

    void resetInstrumentSettings();
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
    void closeTransportEndpoint();
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
    FrameTransferState frameTransfer_{};
    CaptureRunState captureRun_{};
    InstrumentState instrument_{};
    LiveStartRetryState liveStartRetry_{};
    bool transportOpen_{};
    bool connected_{};
    bool busy_{};
    bool cancelRequested_{};
    bool jobCompleteVerified_{};
    std::optional<DeferredAction> deferredAction_;
    QString pendingFailure_;
    quint64 generation_{};
};

} // namespace dso

Q_DECLARE_METATYPE(dso::Capture)
