#pragma once
#include "dso_adc_contract.hpp"
#include "dso_usb_protocol_contract.hpp"

#include <QByteArray>
#include <QMetaType>
#include <QString>
#include <QtGlobal>
#include <optional>

namespace dso::protocol
{
using Opcode = usb_protocol_contract::Opcode;

inline constexpr quint8 ProtocolVersion{usb_protocol_contract::Version};
inline constexpr quint16 CaptureCapacity{usb_protocol_contract::CaptureSampleCapacity};
inline constexpr quint8 RawAdcResolutionBits{adc_contract::ResolutionBits};
inline constexpr quint8 MaximumSamplesPerRead{usb_protocol_contract::MaximumSamplesPerRead};

namespace settings_writable = usb_protocol_contract::settings_writable;
namespace settings_flags = usb_protocol_contract::settings_flags;

using Status = usb_protocol_contract::Status;
struct Reply
{
    quint8 opcode{};
    quint16 id{};
    Status status{Status::Fault};
    // Includes the leading status byte; status is its decoded convenience field.
    QByteArray payload;
};
struct Metadata
{
    quint32 token{};
    quint16 count{}, pretrigger{};
    quint32 timerHz{}, divider{}, acquisitionEpoch{}, triggerSequence{}, physicalGeneration{},
        microvoltsPerDivision{};
    qint32 zeroCodeQ16{};
    quint32 gainQ16{};
    quint8 gainShift{}, calibrationKind{}, coupling{}, triggerMode{}, triggerEdge{};
    quint8 flags{}; // See instrument_code::capture_flags.
    quint8 timebaseIndex{};
    quint8 timebaseCatalogueId{};
    quint16 triggerLevel{};
    bool extendedSettingsValid{};
};
struct Settings
{
    quint8 writableMask{};
    quint8 uiState{};
    quint8 flags{}; // Use settings_flags; Metadata::flags has different meanings.
    quint8 timebaseIndex{};
    quint8 catalogueCount{};
    quint8 triggerMode{};
    quint8 triggerEdge{};
    quint16 triggerLevel{};
    quint16 triggerPositionPermille{};
    quint32 microvoltsPerDivision{};
    quint32 timerHz{};
    quint32 divider{};
    quint8 coupling{};
    quint8 catalogueId{};
    quint16 revision{};
    quint32 timebaseMicrosecondsPerDivision{};
};
enum class SettingsAvailability : quint8 { Unknown, Supported, Legacy, Stale };
quint32 crc32(const QByteArray &bytes);
QByteArray encode(quint8 opcode, quint16 id, const QByteArray &payload = {});
class Decoder final
{
  public:
    // Buffers fragmented stream input within a fixed bound, discarding oldest
    // bytes on overflow so a lost/corrupt frame cannot grow memory unbounded.
    void append(const QByteArray &bytes);
    // Resynchronizes and consumes malformed or unrelated complete frames.
    // Returns one valid reply matching both fields; an incomplete tail remains buffered.
    std::optional<Reply> take(quint16 id, quint8 responseOpcode);
    // Drops every buffered fragment when the session is reset.
    void clear();

  private:
    QByteArray buffer_;
};
// Decoders set error on failure when supplied and clear it on success.
bool decodeMetadata(const QByteArray &payload, Metadata &out, QString *error = nullptr);
bool decodeExtendedMetadata(const QByteArray &payload, Metadata &out, QString *error = nullptr);
bool decodeSettings(const QByteArray &payload, Settings &out, QString *error = nullptr);
} // namespace dso::protocol

Q_DECLARE_METATYPE(dso::protocol::Settings)
Q_DECLARE_METATYPE(dso::protocol::SettingsAvailability)
