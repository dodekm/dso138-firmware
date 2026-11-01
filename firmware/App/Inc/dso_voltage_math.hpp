#ifndef DSO_VOLTAGE_MATH_HPP
#define DSO_VOLTAGE_MATH_HPP

#include <cstdint>

namespace dso::detail {

// Round a signed Q16 code/gain product to integer microvolts. Callers validate
// gain_shift (0..2); the product magnitude is bounded below 2^60, so taking
// its signed magnitude and adding the half-denominator cannot overflow.
[[nodiscard]] inline std::int64_t round_gain_product(std::int64_t product,
                                                      std::uint8_t gain_shift) noexcept
{
    const auto magnitude = static_cast<std::uint64_t>(product < 0 ? -product : product);
    const auto denominator_bits = 32U - gain_shift;
    const auto half_denominator = std::uint64_t{1U} << (denominator_bits - 1U);
    const auto rounded = static_cast<std::int64_t>((magnitude + half_denominator) >> denominator_bits);
    return product < 0 ? -rounded : rounded;
}

} // namespace dso::detail

#endif
