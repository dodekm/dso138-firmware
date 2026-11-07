#ifndef DSO_BINARY_ENDIAN_HPP
#define DSO_BINARY_ENDIAN_HPP

#include <cstdint>

namespace dso {
namespace binary {

// Fixed-width little-endian primitives for already-bounded byte ranges.
// Callers own buffer validation; these helpers do not assume alignment or host byte order.
inline std::uint16_t read_u16_le(const std::uint8_t* bytes) noexcept
{
    return static_cast<std::uint16_t>(std::uint16_t{bytes[0]} |
                                      (std::uint16_t{bytes[1]} << 8U));
}
inline std::uint32_t read_u32_le(const std::uint8_t* bytes) noexcept
{
    return std::uint32_t{bytes[0]} | (std::uint32_t{bytes[1]} << 8U) |
        (std::uint32_t{bytes[2]} << 16U) | (std::uint32_t{bytes[3]} << 24U);
}
inline void write_u16_le(std::uint8_t* bytes, std::uint16_t value) noexcept
{
    bytes[0] = static_cast<std::uint8_t>(value);
    bytes[1] = static_cast<std::uint8_t>(value >> 8U);
}
inline void write_u32_le(std::uint8_t* bytes, std::uint32_t value) noexcept
{
    for (unsigned i{}; i < 4U; ++i) {
        bytes[i] = static_cast<std::uint8_t>(value >> (8U * i));
    }
}
inline std::uint64_t read_u64_le(const std::uint8_t* bytes) noexcept
{
    return std::uint64_t{read_u32_le(bytes)} |
        (std::uint64_t{read_u32_le(bytes + 4U)} << 32U);
}
inline void write_u64_le(std::uint8_t* bytes, std::uint64_t value) noexcept
{
    write_u32_le(bytes, static_cast<std::uint32_t>(value));
    write_u32_le(bytes + 4U, static_cast<std::uint32_t>(value >> 32U));
}

} // namespace binary
} // namespace dso
#endif
