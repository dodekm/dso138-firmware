#ifndef DSO_CRC32_HPP
#define DSO_CRC32_HPP

#include <cstdint>

namespace dso {
namespace checksum {

// CRC-32/IEEE (CRC-32/ISO-HDLC), reflected polynomial 0xEDB88320.
inline constexpr std::uint32_t Crc32IeeeInitial{0xffffffffU};
inline constexpr std::uint32_t Crc32IeeeFinalXor{0xffffffffU};

constexpr std::uint32_t crc32_ieee_update_byte(std::uint32_t crc,
                                               std::uint8_t byte) noexcept
{
    crc ^= byte;
    for (unsigned bit{}; bit < 8U; ++bit) {
        crc = (crc >> 1U) ^ ((crc & 1U) != 0U ? 0xedb88320U : 0U);
    }
    return crc;
}

constexpr std::uint32_t crc32_ieee_finalize(std::uint32_t crc) noexcept
{ return crc ^ Crc32IeeeFinalXor; }

// The caller supplies a valid byte range when count is nonzero. Keeping Count
// templated preserves the existing loop-counter width at each call site.
template <typename Count>
inline std::uint32_t crc32_ieee(const std::uint8_t* bytes, Count count) noexcept
{
    auto crc = Crc32IeeeInitial;
    for (Count i{}; i < count; ++i) {
        crc = crc32_ieee_update_byte(crc, bytes[i]);
    }
    return crc32_ieee_finalize(crc);
}

} // namespace checksum
} // namespace dso
#endif
