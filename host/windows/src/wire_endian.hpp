#pragma once

#include <QtGlobal>

#include "dso_binary_endian.hpp"

namespace dso::wire_detail
{

// Qt-facing adapter over the shared fixed-width byte conversion. Protocol
// fields are little-endian; callers validate bounds before forming pointers.
inline quint16 read16(const char *bytes) noexcept
{
    return static_cast<quint16>(::dso::binary::read_u16_le(
        reinterpret_cast<const std::uint8_t *>(bytes)));
}

inline quint32 read32(const char *bytes) noexcept
{
    return static_cast<quint32>(::dso::binary::read_u32_le(
        reinterpret_cast<const std::uint8_t *>(bytes)));
}

inline void write16(char *bytes, quint16 value) noexcept
{
    ::dso::binary::write_u16_le(reinterpret_cast<std::uint8_t *>(bytes),
                                static_cast<std::uint16_t>(value));
}

inline void write32(char *bytes, quint32 value) noexcept
{
    ::dso::binary::write_u32_le(reinterpret_cast<std::uint8_t *>(bytes),
                                static_cast<std::uint32_t>(value));
}

} // namespace dso::wire_detail
