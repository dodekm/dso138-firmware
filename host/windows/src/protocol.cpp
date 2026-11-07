#include "protocol.hpp"
#include "dso_crc32.hpp"
#include "dso_adc_contract.hpp"
#include "dso_settings_contract.hpp"
#include "dso_usb_protocol_contract.hpp"
#include "instrument_codes.hpp"
#include "protocol_request_payload.hpp"
#include "wire_endian.hpp"

namespace dso::protocol
{
namespace
{
constexpr int kHeaderSize = usb_protocol_contract::HeaderBytes;
constexpr int kMaximumPayload = usb_protocol_contract::MaximumPayloadBytes;
constexpr int kMaximumBufferedBytes = 4096;
namespace metadata_layout = usb_protocol_contract::frame_metadata_body;
namespace settings_layout = usb_protocol_contract::settings_body;
namespace settings_request_layout = usb_protocol_contract::settings_request;
// Response lengths include their leading status byte; extension offsets are
// absolute offsets within that complete response payload.
constexpr int kBaseMetadataPayloadSize = metadata_layout::BaseResponseBytes;
constexpr int kExtendedMetadataPayloadSize = metadata_layout::ExtendedResponseBytes;
constexpr int kSettingsPayloadSize = settings_layout::ResponseBytes;
constexpr char kMagicBytes[]{static_cast<char>(usb_protocol_contract::MagicFirst),
                             static_cast<char>(usb_protocol_contract::MagicSecond)};

bool failWithError(QString *error, const QString &message)
{
    if (error)
    {
        *error = message;
    }
    return false;
}

void clearError(QString *error)
{
    if (error)
    {
        error->clear();
    }
}

QByteArray makeRevisionPayload(std::uint8_t payloadBytes, quint16 revision)
{
    QByteArray payload(static_cast<int>(payloadBytes), '\0');
    wire_detail::write16(payload.data() + settings_request_layout::RevisionOffset, revision);
    return payload;
}

} // namespace

quint32 crc32(const QByteArray &bytes)
{
    return checksum::crc32_ieee(
        reinterpret_cast<const std::uint8_t *>(bytes.constData()), bytes.size());
}

QByteArray encode(quint8 opcode, quint16 requestId, const QByteArray &payload)
{
    if (requestId == 0 || payload.size() > kMaximumPayload)
    {
        return {};
    }

    QByteArray frame(kHeaderSize, '\0');
    frame[usb_protocol_contract::MagicFirstOffset] = char(usb_protocol_contract::MagicFirst);
    frame[usb_protocol_contract::MagicSecondOffset] = char(usb_protocol_contract::MagicSecond);
    frame[usb_protocol_contract::VersionOffset] = char(ProtocolVersion);
    frame[usb_protocol_contract::OpcodeOffset] = char(opcode);
    wire_detail::write16(frame.data() + usb_protocol_contract::RequestIdOffset, requestId);
    wire_detail::write16(frame.data() + usb_protocol_contract::PayloadSizeOffset,
                         quint16(payload.size()));
    frame.append(payload);

    const QByteArray crcInput = frame.left(usb_protocol_contract::CrcHeaderBytes) +
                                frame.mid(usb_protocol_contract::PayloadOffset);
    wire_detail::write32(frame.data() + usb_protocol_contract::CrcOffset, crc32(crcInput));
    return frame;
}

namespace detail
{

QByteArray encodeU32Payload(quint32 value)
{
    QByteArray payload(sizeof(value), '\0');
    wire_detail::write32(payload.data(), value);
    return payload;
}

QByteArray encodeSetSettingsPayload(quint16 revision, quint8 timebaseIndex,
                                    quint8 triggerMode)
{
    namespace layout = settings_request_layout::set_settings;
    auto payload = makeRevisionPayload(layout::PayloadBytes, revision);
    payload[layout::TimebaseIndexOffset] = char(timebaseIndex);
    payload[layout::TriggerModeOffset] = char(triggerMode);
    return payload;
}

QByteArray encodeSetTriggerPayload(quint16 revision, quint8 edge,
                                   quint16 level, quint16 positionPermille)
{
    namespace layout = settings_request_layout::set_trigger;
    auto payload = makeRevisionPayload(layout::PayloadBytes, revision);
    payload[layout::EdgeOffset] = char(edge);
    wire_detail::write16(payload.data() + layout::LevelOffset, level);
    wire_detail::write16(payload.data() + layout::PositionOffset, positionPermille);
    return payload;
}

QByteArray encodeHoldLocalRunPayload(quint16 revision)
{
    return makeRevisionPayload(settings_request_layout::hold_local_run::PayloadBytes,
                               revision);
}

QByteArray encodeReadSamplesPayload(quint32 exportToken, quint16 sampleOffset,
                                    quint8 sampleCount)
{
    namespace layout = usb_protocol_contract::read_samples;
    QByteArray payload(layout::RequestBytes, '\0');
    wire_detail::write32(payload.data() + layout::RequestTokenOffset, exportToken);
    wire_detail::write16(payload.data() + layout::RequestSampleOffset, sampleOffset);
    payload[layout::RequestCountOffset] = char(sampleCount);
    return payload;
}

} // namespace detail

void Decoder::append(const QByteArray &bytes)
{
    if (bytes.size() >= kMaximumBufferedBytes)
    {
        buffer_ = bytes.right(kMaximumBufferedBytes);
        return;
    }
    const int excess = buffer_.size() + bytes.size() - kMaximumBufferedBytes;
    if (excess > 0)
    {
        buffer_.remove(0, excess);
    }
    buffer_.append(bytes);
}

void Decoder::clear()
{
    buffer_.clear();
}

std::optional<Reply> Decoder::take(quint16 requestId, quint8 responseOpcode)
{
    for (;;)
    {
        const int magic = buffer_.indexOf(QByteArray::fromRawData(
            kMagicBytes, usb_protocol_contract::MagicBytes));
        if (magic < 0)
        {
            if (buffer_.size() > 1)
            {
                buffer_ = buffer_.right(1);
            }
            return {};
        }
        if (magic > 0)
        {
            buffer_.remove(0, magic);
        }
        if (buffer_.size() < kHeaderSize)
        {
            return {};
        }

        const int payloadSize = wire_detail::read16(
            buffer_.constData() + usb_protocol_contract::PayloadSizeOffset);
        if (quint8(buffer_[usb_protocol_contract::VersionOffset]) != ProtocolVersion ||
            payloadSize > kMaximumPayload)
        {
            buffer_.remove(0, 1);
            continue;
        }
        const int frameSize = kHeaderSize + payloadSize;
        if (buffer_.size() < frameSize)
        {
            return {};
        }

        const QByteArray frame = buffer_.left(frameSize);
        const QByteArray crcInput = frame.left(usb_protocol_contract::CrcHeaderBytes) +
                                    frame.mid(usb_protocol_contract::PayloadOffset);
        if (crc32(crcInput) != wire_detail::read32(
                frame.constData() + usb_protocol_contract::CrcOffset))
        {
            buffer_.remove(0, 1);
            continue;
        }

        const quint16 frameId = wire_detail::read16(
            frame.constData() + usb_protocol_contract::RequestIdOffset);
        const quint8 opcode = quint8(frame[usb_protocol_contract::OpcodeOffset]);
        if (frameId != requestId || opcode != responseOpcode)
        {
            buffer_.remove(0, frameSize);
            continue;
        }
        if (payloadSize == 0)
        {
            buffer_.remove(0, frameSize);
            continue;
        }

        const quint8 status = quint8(frame[usb_protocol_contract::PayloadOffset]);
        if (status > quint8(Status::Fault))
        {
            buffer_.remove(0, frameSize);
            continue;
        }
        Reply reply{opcode, frameId, Status(status),
                    frame.mid(usb_protocol_contract::PayloadOffset)};
        buffer_.remove(0, frameSize);
        return reply;
    }
}

bool decodeMetadata(const QByteArray &payload, Metadata &metadata, QString *error)
{
    if (payload.size() != kBaseMetadataPayloadSize || quint8(payload[0]) != quint8(Status::Ok))
    {
        return failWithError(error, QStringLiteral("BEGIN_FRAME response length or status is invalid"));
    }

    const char *data = payload.constData() + metadata_layout::BodyInResponseOffset;
    metadata.token = wire_detail::read32(data + metadata_layout::TokenOffset);
    metadata.count = wire_detail::read16(data + metadata_layout::SampleCountOffset);
    metadata.pretrigger = wire_detail::read16(data + metadata_layout::PretriggerOffset);
    metadata.timerHz = wire_detail::read32(data + metadata_layout::TimerHzOffset);
    metadata.divider = wire_detail::read32(data + metadata_layout::TimerDividerOffset);
    metadata.acquisitionEpoch = wire_detail::read32(data + metadata_layout::AcquisitionEpochOffset);
    metadata.triggerSequence = wire_detail::read32(data + metadata_layout::TriggerSequenceOffset);
    metadata.physicalGeneration = wire_detail::read32(data + metadata_layout::PhysicalGenerationOffset);
    metadata.microvoltsPerDivision =
        wire_detail::read32(data + metadata_layout::MicrovoltsPerDivisionOffset);
    metadata.zeroCodeQ16 = qint32(wire_detail::read32(data + metadata_layout::ZeroCodeQ16Offset));
    metadata.gainQ16 = wire_detail::read32(data + metadata_layout::GainQ16Offset);
    metadata.gainShift = quint8(data[metadata_layout::GainShiftOffset]);
    metadata.calibrationKind = quint8(data[metadata_layout::CalibrationKindOffset]);
    metadata.coupling = quint8(data[metadata_layout::CouplingOffset]);
    metadata.triggerMode = quint8(data[metadata_layout::TriggerModeOffset]);
    metadata.triggerEdge = quint8(data[metadata_layout::TriggerEdgeOffset]);
    metadata.flags = quint8(data[metadata_layout::FlagsOffset]);

    if (quint8(data[metadata_layout::ReservedOffset]) != 0 || metadata.token == 0 || metadata.count == 0 ||
        metadata.count > CaptureCapacity || metadata.pretrigger >= metadata.count || metadata.timerHz == 0 ||
        metadata.divider == 0 ||
        !instrument_code::code_is_in_range(
            metadata.calibrationKind, instrument_code::CalibrationKind::Unavailable,
            instrument_code::CalibrationKind::Measured) ||
        !instrument_code::code_is_in_range(
            metadata.coupling, instrument_code::InputCoupling::Unknown,
            instrument_code::InputCoupling::Ground) ||
        !instrument_code::code_is_in_range(
            metadata.triggerMode, instrument_code::TriggerMode::Normal,
            instrument_code::TriggerMode::Single) ||
        !instrument_code::code_is_in_range(
            metadata.triggerEdge, instrument_code::TriggerEdge::Rising,
            instrument_code::TriggerEdge::Falling) ||
        (metadata.flags & instrument_code::capture_flags::ReservedMask) != 0)
    {
        return failWithError(error, QStringLiteral("BEGIN_FRAME metadata is outside protocol bounds"));
    }
    clearError(error);
    return true;
}

bool decodeExtendedMetadata(const QByteArray &payload, Metadata &metadata, QString *error)
{
    if (payload.size() != kExtendedMetadataPayloadSize || quint8(payload[0]) != quint8(Status::Ok))
    {
        return failWithError(error, QStringLiteral("BEGIN_FRAME_EXTENDED response length or status is invalid"));
    }
    if (!decodeMetadata(payload.left(kBaseMetadataPayloadSize), metadata, error))
    {
        return false;
    }
    metadata.timebaseIndex = quint8(payload[metadata_layout::ExtendedTimebaseIndexResponseOffset]);
    metadata.triggerLevel = wire_detail::read16(
        payload.constData() + metadata_layout::ExtendedTriggerLevelResponseOffset);
    metadata.extendedSettingsValid = true;
    if (metadata.triggerLevel > adc_contract::MaximumCode ||
        quint8(payload[metadata_layout::ExtendedReservedResponseOffset]) != 0)
    {
        metadata.extendedSettingsValid = false;
        return failWithError(error, QStringLiteral("BEGIN_FRAME_EXTENDED fields are outside protocol bounds"));
    }
    clearError(error);
    return true;
}

bool decodeSettings(const QByteArray &payload, Settings &settings, QString *error)
{
    // The wire reply contains the ordinary status byte followed by the fixed
    // 32-byte GET_SETTINGS body.
    if (payload.size() != kSettingsPayloadSize || quint8(payload[0]) != quint8(Status::Ok))
    {
        return failWithError(error, QStringLiteral("GET_SETTINGS response length or status is invalid"));
    }
    const char *data = payload.constData() + settings_layout::BodyInResponseOffset;
    if (quint8(data[settings_layout::SchemaOffset]) != 1)
    {
        return failWithError(error, QStringLiteral("GET_SETTINGS schema is unsupported"));
    }
    settings.writableMask = quint8(data[settings_layout::WritableMaskOffset]);
    settings.uiState = quint8(data[settings_layout::UiStateOffset]);
    settings.flags = quint8(data[settings_layout::FlagsOffset]);
    settings.timebaseIndex = quint8(data[settings_layout::TimebaseIndexOffset]);
    settings.catalogueCount = quint8(data[settings_layout::CatalogueCountOffset]);
    settings.triggerMode = quint8(data[settings_layout::TriggerModeOffset]);
    settings.triggerEdge = quint8(data[settings_layout::TriggerEdgeOffset]);
    settings.triggerLevel = wire_detail::read16(data + settings_layout::TriggerLevelOffset);
    settings.triggerPositionPermille =
        wire_detail::read16(data + settings_layout::TriggerPositionOffset);
    settings.microvoltsPerDivision =
        wire_detail::read32(data + settings_layout::MicrovoltsPerDivisionOffset);
    settings.timerHz = wire_detail::read32(data + settings_layout::TimerHzOffset);
    settings.divider = wire_detail::read32(data + settings_layout::TimerDividerOffset);
    settings.coupling = quint8(data[settings_layout::CouplingOffset]);
    settings.catalogueId = quint8(data[settings_layout::CatalogueIdOffset]);
    settings.revision = wire_detail::read16(data + settings_layout::RevisionOffset);
    settings.timebaseMicrosecondsPerDivision =
        wire_detail::read32(data + settings_layout::TimebaseMicrosecondsPerDivisionOffset);

    if ((settings.writableMask & settings_writable::ReservedMask) != 0 ||
        settings.uiState > instrument_code::code(instrument_code::UiState::Fault) ||
        (settings.flags & settings_flags::ReservedMask) != 0 || settings.catalogueCount == 0 ||
        settings.timebaseIndex >= settings.catalogueCount ||
        !instrument_code::code_is_in_range(
            settings.triggerMode, instrument_code::TriggerMode::Normal,
            instrument_code::TriggerMode::Single) ||
        !instrument_code::code_is_in_range(
            settings.triggerEdge, instrument_code::TriggerEdge::Rising,
            instrument_code::TriggerEdge::Falling) ||
        settings.triggerLevel > adc_contract::MaximumCode ||
        settings.triggerPositionPermille > settings_contract::TriggerPositionPermilleMaximum ||
        !instrument_code::code_is_in_range(
            settings.coupling, instrument_code::InputCoupling::Unknown,
            instrument_code::InputCoupling::Ground) ||
        ((settings.timerHz == 0) != (settings.divider == 0)) ||
        settings.timebaseMicrosecondsPerDivision == 0)
    {
        return failWithError(error, QStringLiteral("GET_SETTINGS fields are outside protocol bounds"));
    }
    clearError(error);
    return true;
}
} // namespace dso::protocol
