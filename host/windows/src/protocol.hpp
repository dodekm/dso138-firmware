#pragma once
#include "capture.hpp"
#include <QByteArray>
#include <QMetaType>
#include <QString>
#include <cstdint>
#include <optional>
#include <vector>

namespace dso::protocol
{
enum class Status : quint8
{
    Ok,
    Invalid,
    Busy,
    Stale,
    Unsupported,
    Timeout,
    Cancelled,
    Fault
};
struct Reply
{
    quint8 opcode{};
    quint16 id{};
    Status status{Status::Fault};
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
    quint8 gainShift{}, calibrationKind{}, coupling{}, triggerMode{}, triggerEdge{}, flags{};
    quint8 timebaseIndex{};
    quint8 timebaseCatalogueId{};
    quint16 triggerLevel{};
    bool extendedSettingsValid{};
};
struct Settings
{
    quint8 writableMask{};
    quint8 uiState{};
    quint8 flags{};
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
    void append(const QByteArray &bytes);
    std::optional<Reply> take(quint16 id, quint8 responseOpcode);
    void clear();

  private:
    QByteArray buffer_;
};
bool decodeMetadata(const QByteArray &payload, Metadata &out, QString *error = nullptr);
bool decodeExtendedMetadata(const QByteArray &payload, Metadata &out, QString *error = nullptr);
bool decodeSettings(const QByteArray &payload, Settings &out, QString *error = nullptr);
} // namespace dso::protocol

Q_DECLARE_METATYPE(dso::protocol::Settings)
Q_DECLARE_METATYPE(dso::protocol::SettingsAvailability)
