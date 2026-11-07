#pragma once

#include <QByteArray>
#include <QtGlobal>

namespace dso::protocol::detail
{

// Pack protocol-v1 request payloads. Session owns value and lifecycle policy.
QByteArray encodeU32Payload(quint32 value);
QByteArray encodeSetSettingsPayload(quint16 revision, quint8 timebaseIndex,
                                    quint8 triggerMode);
QByteArray encodeSetTriggerPayload(quint16 revision, quint8 edge,
                                   quint16 level, quint16 positionPermille);
QByteArray encodeHoldLocalRunPayload(quint16 revision);
QByteArray encodeReadSamplesPayload(quint32 exportToken, quint16 sampleOffset,
                                    quint8 sampleCount);

} // namespace dso::protocol::detail
