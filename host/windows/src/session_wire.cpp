#include "session_wire.hpp"
#include "wire_endian.hpp"

namespace dso::session_wire
{

quint16 read16(const QByteArray &bytes, qsizetype offset)
{
    return wire_detail::read16(bytes.constData() + offset);
}

quint32 read32(const QByteArray &bytes, qsizetype offset)
{
    return wire_detail::read32(bytes.constData() + offset);
}

Metadata toCaptureMetadata(const protocol::Metadata &source)
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

} // namespace dso::session_wire
