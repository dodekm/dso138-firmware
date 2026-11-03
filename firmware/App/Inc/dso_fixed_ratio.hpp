#ifndef DSO_FIXED_RATIO_HPP
#define DSO_FIXED_RATIO_HPP

#include <cstdint>
#include "dso_u64_division.hpp"

namespace dso::detail {

struct MillionRatio final { std::uint64_t quotient; std::uint64_t remainder; };

#if defined(__GNUC__)
#define DSO_RATIO_ALWAYS_INLINE inline __attribute__((always_inline))
#define DSO_RATIO_NOINLINE __attribute__((noinline)) inline
#elif defined(_MSC_VER)
#define DSO_RATIO_ALWAYS_INLINE __forceinline
#define DSO_RATIO_NOINLINE __declspec(noinline) inline
#else
#define DSO_RATIO_ALWAYS_INLINE inline
#define DSO_RATIO_NOINLINE inline
#endif

// Exact floor(numerator * 1,000,000 / denominator), retaining the remainder.
// All callers provide a nonzero denominator and a quotient within uint64_t.
// Timing input domains guarantee denominator < UINT64_MAX/2.
DSO_RATIO_NOINLINE MillionRatio million_ratio_wide(
    std::uint64_t numerator, std::uint64_t denominator) noexcept
{
    constexpr std::uint64_t Scale{1000000U};
    static_assert(Scale <= UINT32_MAX);
    const auto divided = divide_u64(numerator, denominator);
    const auto whole = divided.quotient;
    const auto fractional = divided.remainder;
    std::uint32_t fractional_quotient{};
    std::uint64_t remainder{};
    // The processed scale prefix never exceeds 1,000,000. Since fractional
    // is below denominator, its quotient fits uint32_t at every iteration;
    // remainder additions remain below twice the denominator.
    for (unsigned bit{20U}; bit != 0U; --bit) {
        fractional_quotient *= 2U;
        remainder *= 2U;
        if (remainder >= denominator) { remainder -= denominator; ++fractional_quotient; }
        if ((Scale & (std::uint64_t{1U} << (bit - 1U))) != 0U) {
            remainder += fractional;
            if (remainder >= denominator) { remainder -= denominator; ++fractional_quotient; }
        }
    }
    return {whole * Scale + fractional_quotient, remainder};
}

// Keep the common product-safe case in the caller: projection does not pay
// a helper call for every point. Only overflow uses the shared wider loop.
DSO_RATIO_ALWAYS_INLINE MillionRatio million_ratio(
    std::uint64_t numerator, std::uint64_t denominator) noexcept
{
    constexpr std::uint64_t Scale{1000000U};
    static_assert(Scale <= UINT32_MAX);
    if (numerator <= UINT64_MAX / Scale) {
        const auto product = numerator * Scale;
        const auto divided = divide_u64(product, denominator);
        return {divided.quotient, divided.remainder};
    }
    return million_ratio_wide(numerator, denominator);
}

#undef DSO_RATIO_ALWAYS_INLINE
#undef DSO_RATIO_NOINLINE

} // namespace dso::detail

#endif
