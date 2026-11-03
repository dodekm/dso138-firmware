#ifndef DSO_U64_DIVISION_HPP
#define DSO_U64_DIVISION_HPP
#include <cstdint>
#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace dso::detail {
struct U64QuotientRemainder final {
    std::uint64_t quotient;
    std::uint64_t remainder;
};

// value must be nonzero.
inline unsigned bit_width_u64(std::uint64_t value) noexcept
{
#if defined(__GNUC__)
    return 64U - static_cast<unsigned>(__builtin_clzll(value));
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64))
    unsigned long highest{};
    static_cast<void>(_BitScanReverse64(&highest, value));
    return static_cast<unsigned>(highest) + 1U;
#elif defined(_MSC_VER)
    unsigned long highest{};
    const auto upper = static_cast<std::uint32_t>(value >> 32U);
    if (_BitScanReverse(&highest, upper) != 0U) {
        return 33U + static_cast<unsigned>(highest);
    }
    const auto lower = static_cast<std::uint32_t>(value);
    static_cast<void>(_BitScanReverse(&highest, lower));
    return 1U + static_cast<unsigned>(highest);
#else
    unsigned width{};
    while (value != 0U) { ++width; value >>= 1U; }
    return width;
#endif
}

// Preconditions: denominator is nonzero. The full-range unsigned quotient and
// remainder are exact. Aligning the divisor to the numerator's highest bit
// bounds the loop to the quotient's bit width (plus its leading comparison).
// Keep one shared body in each image to limit flash; inline preserves ODR safety
// for this header-only helper across translation units.
#if defined(__GNUC__)
__attribute__((noinline)) inline U64QuotientRemainder divide_u64(
#elif defined(_MSC_VER)
__declspec(noinline) inline U64QuotientRemainder divide_u64(
#else
inline U64QuotientRemainder divide_u64(
#endif
    std::uint64_t numerator, std::uint64_t denominator) noexcept
{
    if (numerator < denominator) { return {0U, numerator}; }
    const auto shift = bit_width_u64(numerator) - bit_width_u64(denominator);
    auto aligned_denominator = denominator << shift;
    std::uint64_t quotient{};
    for (unsigned bit = shift + 1U; bit != 0U; --bit) {
        if (numerator >= aligned_denominator) {
            numerator -= aligned_denominator;
            quotient |= std::uint64_t{1U} << (bit - 1U);
        }
        aligned_denominator >>= 1U;
    }
    return {quotient, numerator};
}
} // namespace dso::detail
#endif