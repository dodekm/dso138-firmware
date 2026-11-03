#include "protocol.hpp"

#include <QtEndian>

namespace dso::protocol
{
namespace
{
constexpr int kHeaderSize = 12;
constexpr int kMaximumPayload = 52;
constexpr int kMaximumBufferedBytes = 4096;

quint16 read16(const char *bytes)
{
    return qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(bytes));
}

quint32 read32(const char *bytes)
{
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(bytes));
}
} // namespace

quint32 crc32(const QByteArray &bytes)
{
    quint32 crc = 0xffffffffU;
    for (unsigned char byte : bytes)
    {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit)
        {
            crc = (crc >> 1U) ^ ((crc & 1U) != 0 ? 0xedb88320U : 0U);
        }
    }
    return crc ^ 0xffffffffU;
}

QByteArray encode(quint8 opcode, quint16 requestId, const QByteArray &payload)
{
    if (requestId == 0 || payload.size() > kMaximumPayload)
    {
        return {};
    }

    QByteArray frame(kHeaderSize, '\0');
    frame[0] = 0x44;
    frame[1] = 0x53;
    frame[2] = 1;
    frame[3] = char(opcode);
    qToLittleEndian(requestId, reinterpret_cast<uchar *>(frame.data() + 4));
    qToLittleEndian<quint16>(quint16(payload.size()), reinterpret_cast<uchar *>(frame.data() + 6));
    frame.append(payload);

    const QByteArray crcInput = frame.left(8) + frame.mid(kHeaderSize);
    qToLittleEndian(crc32(crcInput), reinterpret_cast<uchar *>(frame.data() + 8));
    return frame;
}

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
        const int magic = buffer_.indexOf(QByteArray::fromHex("4453"));
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

        const int payloadSize = read16(buffer_.constData() + 6);
        if (quint8(buffer_[2]) != 1 || payloadSize > kMaximumPayload)
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
        const QByteArray crcInput = frame.left(8) + frame.mid(kHeaderSize);
        if (crc32(crcInput) != read32(frame.constData() + 8))
        {
            buffer_.remove(0, 1);
            continue;
        }

        const quint16 frameId = read16(frame.constData() + 4);
        const quint8 opcode = quint8(frame[3]);
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

        const quint8 status = quint8(frame[kHeaderSize]);
        if (status > quint8(Status::Fault))
        {
            buffer_.remove(0, frameSize);
            continue;
        }
        Reply reply{opcode, frameId, Status(status), frame.mid(kHeaderSize)};
        buffer_.remove(0, frameSize);
        return reply;
    }
}

bool decodeMetadata(const QByteArray &payload, Metadata &metadata, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error)
        {
            *error = message;
        }
        return false;
    };
    if (payload.size() != 48 || quint8(payload[0]) != quint8(Status::Ok))
    {
        return fail(QStringLiteral("BEGIN_FRAME response length or status is invalid"));
    }

    const char *data = payload.constData() + 1;
    metadata.token = read32(data);
    metadata.count = read16(data + 4);
    metadata.pretrigger = read16(data + 6);
    metadata.timerHz = read32(data + 8);
    metadata.divider = read32(data + 12);
    metadata.acquisitionEpoch = read32(data + 16);
    metadata.triggerSequence = read32(data + 20);
    metadata.physicalGeneration = read32(data + 24);
    metadata.microvoltsPerDivision = read32(data + 28);
    metadata.zeroCodeQ16 = qint32(read32(data + 32));
    metadata.gainQ16 = read32(data + 36);
    metadata.gainShift = quint8(data[40]);
    metadata.calibrationKind = quint8(data[41]);
    metadata.coupling = quint8(data[42]);
    metadata.triggerMode = quint8(data[43]);
    metadata.triggerEdge = quint8(data[44]);
    metadata.flags = quint8(data[45]);

    if (quint8(data[46]) != 0 || metadata.token == 0 || metadata.count == 0 ||
        metadata.count > 1024 || metadata.pretrigger >= metadata.count || metadata.timerHz == 0 ||
        metadata.divider == 0 || metadata.calibrationKind > 2 || metadata.coupling > 3 ||
        metadata.triggerMode > 2 || metadata.triggerEdge > 1 || (metadata.flags & 0xf7U) != 0)
    {
        return fail(QStringLiteral("BEGIN_FRAME metadata is outside protocol bounds"));
    }
    if (error)
    {
        error->clear();
    }
    return true;
}

bool decodeExtendedMetadata(const QByteArray &payload, Metadata &metadata, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error)
        {
            *error = message;
        }
        return false;
    };
    if (payload.size() != 52 || quint8(payload[0]) != quint8(Status::Ok))
    {
        return fail(QStringLiteral("BEGIN_FRAME_EXTENDED response length or status is invalid"));
    }
    if (!decodeMetadata(payload.left(48), metadata, error))
    {
        return false;
    }
    metadata.timebaseIndex = quint8(payload[48]);
    metadata.triggerLevel = read16(payload.constData() + 49);
    metadata.extendedSettingsValid = true;
    if (metadata.triggerLevel > 4095 || quint8(payload[51]) != 0)
    {
        metadata.extendedSettingsValid = false;
        return fail(QStringLiteral("BEGIN_FRAME_EXTENDED fields are outside protocol bounds"));
    }
    if (error)
    {
        error->clear();
    }
    return true;
}

bool decodeSettings(const QByteArray &payload, Settings &settings, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error)
        {
            *error = message;
        }
        return false;
    };
    // The wire reply contains the ordinary status byte followed by the fixed
    // 32-byte GET_SETTINGS body.
    if (payload.size() != 33 || quint8(payload[0]) != quint8(Status::Ok))
    {
        return fail(QStringLiteral("GET_SETTINGS response length or status is invalid"));
    }
    const char *data = payload.constData() + 1;
    if (quint8(data[0]) != 1)
    {
        return fail(QStringLiteral("GET_SETTINGS schema is unsupported"));
    }
    settings.writableMask = quint8(data[1]);
    settings.uiState = quint8(data[2]);
    settings.flags = quint8(data[3]);
    settings.timebaseIndex = quint8(data[4]);
    settings.catalogueCount = quint8(data[5]);
    settings.triggerMode = quint8(data[6]);
    settings.triggerEdge = quint8(data[7]);
    settings.triggerLevel = read16(data + 8);
    settings.triggerPositionPermille = read16(data + 10);
    settings.microvoltsPerDivision = read32(data + 12);
    settings.timerHz = read32(data + 16);
    settings.divider = read32(data + 20);
    settings.coupling = quint8(data[24]);
    settings.catalogueId = quint8(data[25]);
    settings.revision = read16(data + 26);
    settings.timebaseMicrosecondsPerDivision = read32(data + 28);

    if ((settings.writableMask & 0xc0U) != 0 || settings.uiState > 6 ||
        (settings.flags & 0xe0U) != 0 || settings.catalogueCount == 0 ||
        settings.timebaseIndex >= settings.catalogueCount || settings.triggerMode > 2 ||
        settings.triggerEdge > 1 || settings.triggerLevel > 4095 ||
        settings.triggerPositionPermille > 1000 || settings.coupling > 3 ||
        ((settings.timerHz == 0) != (settings.divider == 0)) ||
        settings.timebaseMicrosecondsPerDivision == 0)
    {
        return fail(QStringLiteral("GET_SETTINGS fields are outside protocol bounds"));
    }
    if (error)
    {
        error->clear();
    }
    return true;
}
} // namespace dso::protocol
