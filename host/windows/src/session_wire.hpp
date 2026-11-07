#pragma once

#include "capture_model.hpp"
#include "protocol.hpp"

namespace dso::session_wire
{

// Read offsets are relative to the complete protocol payload, including its
// leading status byte. Call only after the handler validates the full shape.
quint16 read16(const QByteArray &bytes, qsizetype offset);
quint32 read32(const QByteArray &bytes, qsizetype offset);
// Translate decoded protocol metadata into the capture model's metadata value.
Metadata toCaptureMetadata(const protocol::Metadata &source);

} // namespace dso::session_wire
