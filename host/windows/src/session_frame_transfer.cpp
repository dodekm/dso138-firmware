#include "session.hpp"
#include "capture_model.hpp"
#include "session_wire.hpp"
#include "session_timing.hpp"
#include "instrument_codes.hpp"
#include "dso_adc_contract.hpp"
#include "dso_usb_protocol_contract.hpp"
#include "protocol_request_payload.hpp"

namespace dso
{

namespace read_sample_layout = usb_protocol_contract::read_samples;

using session_wire::read16;
using session_wire::read32;
using session_timing::kLiveSettingsPollMs;

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
    sendCommand(instrument_.supportsExtendedMetadata() ? protocol::Opcode::BeginFrameExtended
                                                       : protocol::Opcode::BeginFrame);
}

void Session::handleBegin(const protocol::Reply &reply)
{
    if ((reply.payload.size() == 48 || reply.payload.size() == 52) && quint8(reply.payload[0]) == 0)
    {
        frameTransfer_.exportToken = read32(reply.payload, 1);
    }
    const bool decoded = reply.payload.size() == 52
                             ? protocol::decodeExtendedMetadata(reply.payload,
                                                                frameTransfer_.metadata, nullptr)
                             : protocol::decodeMetadata(reply.payload, frameTransfer_.metadata, nullptr);
    if (!decoded)
    {
        if (frameTransfer_.exportToken != 0)
        {
            // The metadata body is unusable, but its complete fixed-size
            // prefix supplied a token that can still release the lease.
            failOperation(QStringLiteral("BEGIN_FRAME metadata is malformed"));
        }
        else
        {
            closeTransport(QStringLiteral("BEGIN_FRAME metadata is malformed; connection reset "
                                          "to clear a lease without a usable token"),
                           true);
        }
        return;
    }
    if (frameTransfer_.metadata.extendedSettingsValid)
        frameTransfer_.metadata.timebaseCatalogueId = instrument_.settings.catalogueId;
    QString validationError;
    if (!validateMetadata(session_wire::toCaptureMetadata(frameTransfer_.metadata), &validationError))
    {
        failOperation(
            QStringLiteral("Frame calibration or metadata is invalid: %1").arg(validationError));
        return;
    }
    frameTransfer_.exportToken = frameTransfer_.metadata.token;

    if (cancelRequested_)
    {
        beginCleanup();
        return;
    }
    if (jobId_ != 0)
    {
        state_ = State::VerifyLeasedFrame;
        sendCommand(protocol::Opcode::Status, protocol::detail::encodeU32Payload(jobId_));
        return;
    }
    prepareFrameBuffer();
}

void Session::prepareFrameBuffer()
{
    frameTransfer_.capture = {};
    frameTransfer_.capture.metadata = session_wire::toCaptureMetadata(frameTransfer_.metadata);
    frameTransfer_.capture.raw.resize(frameTransfer_.metadata.count);
    frameTransfer_.sampleOffset = 0;
    requestNextChunk();
}

void Session::requestNextChunk()
{
    if (cancelRequested_)
    {
        beginCleanup();
        return;
    }
    if (frameTransfer_.sampleOffset >= frameTransfer_.metadata.count)
    {
        requestEndFrame();
        return;
    }

    const quint8 count = quint8(std::min<int>(
        protocol::MaximumSamplesPerRead,
        frameTransfer_.metadata.count - frameTransfer_.sampleOffset));
    state_ = State::ReadSamples;
    sendCommand(protocol::Opcode::ReadSamples,
                protocol::detail::encodeReadSamplesPayload(
                    frameTransfer_.exportToken, frameTransfer_.sampleOffset, count));
}

void Session::handleRead(const protocol::Reply &reply)
{
    const QByteArray &p = reply.payload;
    if (p.size() < read_sample_layout::MinimumReplyBytes || frameTransfer_.exportToken == 0 ||
        read32(p, read_sample_layout::ReplyTokenOffset) != frameTransfer_.exportToken ||
        read16(p, read_sample_layout::ReplySampleOffset) != frameTransfer_.sampleOffset)
    {
        failOperation(QStringLiteral("READ_SAMPLES response does not match the requested range"));
        return;
    }
    const quint8 count = quint8(p[read_sample_layout::ReplyCountOffset]);
    const quint8 expected = quint8(std::min<int>(
        protocol::MaximumSamplesPerRead,
        frameTransfer_.metadata.count - frameTransfer_.sampleOffset));
    if (count == 0 || count != expected ||
        p.size() != read_sample_layout::MinimumReplyBytes + read_sample_layout::SampleBytes * count ||
        frameTransfer_.sampleOffset + count > frameTransfer_.metadata.count)
    {
        failOperation(QStringLiteral("READ_SAMPLES returned an invalid chunk length"));
        return;
    }
    for (quint8 i = 0; i < count; ++i)
    {
        const quint16 sample = read16(p, read_sample_layout::ReplySamplesOffset +
                                            read_sample_layout::SampleBytes * i);
        if (sample > adc_contract::MaximumCode)
        {
            failOperation(QStringLiteral("READ_SAMPLES returned a code outside 0..4095"));
            return;
        }
        frameTransfer_.capture.raw[frameTransfer_.sampleOffset + i] = sample;
    }
    frameTransfer_.sampleOffset += count;
    requestNextChunk();
}

void Session::requestEndFrame()
{
    if (frameTransfer_.exportToken == 0)
    {
        failOperation(QStringLiteral("Cannot release a frame without an export token"));
        return;
    }
    state_ = State::EndFrame;
    sendCommand(protocol::Opcode::EndFrame,
                protocol::detail::encodeU32Payload(frameTransfer_.exportToken));
}

void Session::handleEnd(const protocol::Reply &reply)
{
    if (reply.payload.size() != 1)
    {
        // The reply matched the request ID and passed CRC, but its body does
        // not establish whether the device released the lease. Resetting the
        // session makes firmware revoke any lease still owned by this host.
        closeTransport(QStringLiteral("END_FRAME response has an unexpected payload; "
                                      "connection reset to clear the uncertain frame lease"),
                       true);
        return;
    }
    frameTransfer_.exportToken = 0;
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
    if (!validateCapture(frameTransfer_.capture, &validationError))
    {
        failOperation(
            QStringLiteral("Downloaded frame failed validation: %1").arg(validationError));
        return;
    }

    const qint64 acquisitionMs = captureRun_.acquisitionDurationMs;
    const qint64 downloadMs = downloadClock_.isValid() ? downloadClock_.elapsed() : 0;
    if (captureRun_.trackHostInterval)
    {
        if (recordPublishClock_.isValid())
        {
            const qint64 elapsed = recordPublishClock_.elapsed();
            frameTransfer_.capture.metadata.hostIntervalBeforeMs = static_cast<std::uint32_t>(
                std::min<qint64>(elapsed, std::numeric_limits<std::uint32_t>::max()));
        }
        else
        {
            frameTransfer_.capture.metadata.hostIntervalBeforeMs = UnavailableHostIntervalMs;
        }
        recordPublishClock_.restart();
    }
    const Capture published = frameTransfer_.capture;
    ++captureRun_.completed;
    const bool liveSingle = captureRun_.live && frameTransfer_.capture.metadata.triggerMode ==
                            instrument_code::code(instrument_code::TriggerMode::Single);
    if (!captureRun_.live)
        --captureRun_.remaining;
    frameTransfer_.exportToken = 0;
    jobId_ = 0;
    downloadClock_.invalidate();

    if (captureRun_.live && !liveSingle && !cancelRequested_)
    {
        if (liveSettingsClock_.isValid() && liveSettingsClock_.elapsed() >= kLiveSettingsPollMs)
        {
            state_ = State::ReadingLiveSettings;
            sendCommand(protocol::Opcode::GetSettings);
        }
        else
        {
            state_ = State::SeriesGap;
            scheduleStateTimer(0);
        }
    }
    else if (!captureRun_.live && captureRun_.remaining > 0 && !cancelRequested_)
    {
        state_ = State::SeriesGap;
        scheduleStateTimer(captureRun_.intervalMs);
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
    if (!captureRun_.live && captureRun_.total > 0)
        emit progress(captureRun_.completed, captureRun_.total);
}

} // namespace dso
