#include "session.hpp"
#include "session_wire.hpp"
#include "session_timing.hpp"
#include "dso_usb_protocol_contract.hpp"

namespace dso
{

using session_wire::read16;
using session_wire::read32;
using session_timing::kCaptureReadinessMs;
using session_timing::kFrameAvailabilityRetryMs;
using session_timing::kLiveStartBusyRetryDelayMs;
namespace info_layout = usb_protocol_contract::info;
namespace capture_job_layout = usb_protocol_contract::capture_job;

// Validate the shared reply envelope before applying state-specific transitions.
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
        captureRun_.live && captureRun_.completed == 0 && !liveStartRetry_.busyRetryUsed &&
        reply.payload.size() == 1)
    {
        liveStartRetry_.busyRetryUsed = true;
        liveStartRetry_.settingsReads = 0;
        liveStartRetry_.revision = instrument_.settings.revision;
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
        if (reply.payload.size() == 1 && completionClock_.isValid() &&
            completionClock_.elapsed() < kCaptureReadinessMs && !cancelRequested_)
        {
            downloadClock_.invalidate();
            scheduleStateTimer(kFrameAvailabilityRetryMs);
            return;
        }
        failOperation(QStringLiteral("No exportable held frame is available"));
        return;
    }
    if (state_ == State::EndFrame && reply.status != protocol::Status::Ok)
    {
        if (reply.status == protocol::Status::Stale && reply.payload.size() == 1)
        {
            // END_FRAME returns Stale when the device no longer owns this
            // token (for example, after lease expiry or a local action).
            frameTransfer_.exportToken = 0;
            failOperation(QStringLiteral("Frame lease expired before it could be released"));
        }
        else
        {
            closeTransport(QStringLiteral("END_FRAME did not confirm release; connection reset "
                                          "to clear the uncertain frame lease"),
                           true);
        }
        return;
    }
    if (state_ == State::CancelCapture && reply.status != protocol::Status::Ok)
    {
        const bool ownershipEnded =
            reply.payload.size() == 1 &&
            (reply.status == protocol::Status::Cancelled ||
             reply.status == protocol::Status::Stale || reply.status == protocol::Status::Timeout);
        if (ownershipEnded)
        {
            jobId_ = 0;
            completeOperation(pendingFailure_.isEmpty() ? QStringLiteral("Operation cancelled")
                                                        : pendingFailure_);
        }
        else
        {
            closeTransport(QStringLiteral("CANCEL_CAPTURE did not confirm job cleanup; "
                                          "connection reset to clear the uncertain job"),
                           true);
        }
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
        if (reply.payload.size() != capture_job_layout::ReplyPayloadBytes)
        {
            closeTransport(QStringLiteral("Capture acknowledgement is malformed; connection "
                                          "reset to clear any accepted job"),
                           true);
            return;
        }
        jobId_ = read32(reply.payload, capture_job_layout::ReplyJobIdOffset);
        if (jobId_ == 0)
        {
            closeTransport(QStringLiteral("Capture acknowledgement contains an invalid job ID; "
                                          "connection reset to clear any accepted job"),
                           true);
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
    if (reply.status != protocol::Status::Ok || p.size() != info_layout::ReplyPayloadBytes ||
        quint8(p[info_layout::ReplyProtocolVersionOffset]) != protocol::ProtocolVersion ||
        read16(p, info_layout::ReplyCaptureCapacityOffset) != protocol::CaptureCapacity ||
        quint8(p[info_layout::ReplyRawAdcResolutionBitsOffset]) != protocol::RawAdcResolutionBits ||
        quint8(p[info_layout::ReplyMaximumSamplesPerReadOffset]) != protocol::MaximumSamplesPerRead ||
        read16(p, info_layout::ReplyReservedOffset) != 0)
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


} // namespace dso
