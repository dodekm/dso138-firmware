#include "dso_decimator.hpp"

namespace dso {
namespace {
constexpr std::int32_t coefficient_sum() noexcept
{
    std::int32_t result{};
    for (const auto value : SlowDecimator::Coefficients) { result += value; }
    return result;
}
constexpr std::int32_t coefficient_absolute_sum() noexcept
{
    std::int32_t result{};
    for (const auto value : SlowDecimator::Coefficients) { result += value < 0 ? -value : value; }
    return result;
}
constexpr auto AbsoluteSum = coefficient_absolute_sum();
static_assert(SlowDecimator::Taps > 0U && SlowDecimator::Taps < 64U,
              "Clipping history mask requires fewer taps than uint64 width");
// Round nearest, ties away from zero, without implementation-defined signed
// right shifts. Centered inputs leave enough signed16-bit headroom for every
// possible12-bit input pattern, including adversarial alternating extremes.
std::int16_t rounded(std::int32_t value) noexcept
{
    const auto magnitude = value < 0 ? -value : value;
    const auto result = (magnitude + SlowDecimator::CoefficientScale / 2) / SlowDecimator::CoefficientScale;
    return static_cast<std::int16_t>(value < 0 ? -result : result);
}
static_assert(SlowDecimator::Taps == 63U && SlowDecimator::Taps % 2U == 1U,
              "Paired FIR requires the public odd 63-tap coefficient table");
constexpr bool coefficients_symmetric() noexcept
{
    for (std::uint16_t i{}; i < 31U; ++i) {
        if (SlowDecimator::Coefficients[i] != SlowDecimator::Coefficients[62U - i]) { return false; }
    }
    return SlowDecimator::Coefficients[31] % 2 == 0;
}
constexpr auto make_half_coefficients() noexcept
{
    std::array<std::int16_t, 32> result{};
    for (std::uint16_t i{}; i < 32U; ++i) { result[i] = SlowDecimator::Coefficients[i]; }
    result[31] = static_cast<std::int16_t>(result[31] / 2);
    return result;
}
static_assert(coefficients_symmetric(), "Full FIR coefficients must be symmetric and center must be even");
constexpr auto HalfCoefficients = make_half_coefficients();
static_assert(coefficient_sum() == SlowDecimator::CoefficientScale, "Exact unity DC gain");
static_assert(AbsoluteSum == 47156, "Recheck all state/accumulator bounds after coefficient changes");
// ceil(2048*L1+.5)=2948; ceil(2948*L1+.5)=4243. Intermediate
// convolution is <=2948*47156=139015888, below INT32_MAX.
static_assert(std::int64_t{2948} * AbsoluteSum < INT32_MAX - SlowDecimator::CoefficientScale / 2,
              "Second-stage accumulator cannot overflow");
static_assert((std::int64_t{2948} * AbsoluteSum + 32767) / 32768 + 1 < INT16_MAX,
              "Second-stage centered state cannot overflow");
}

void SlowDecimator::reset() noexcept
{
    first_ = Stage{};
    second_ = Stage{};
    primed_ = false;
    invalid_ = false;
}

bool SlowDecimator::Stage::push(std::int16_t sample, bool raw_clipped,
                               std::int16_t& output, bool& clipped_output) noexcept
{
    history[cursor] = sample;
    // Private clipping history is queried only for nonzero; bit0 is newest.
    clipped = ((clipped << 1U) | std::uint64_t{raw_clipped}) &
        ((std::uint64_t{1U} << Taps) - 1U);
    cursor = static_cast<std::uint8_t>(cursor + 1U);
    if (cursor == Taps) { cursor = 0U; }
    if (filled < Taps) { ++filled; }
    if (filled != Taps) { return false; }
    if (remaining != 0U) { --remaining; return false; }
    remaining = 4U;
    std::int32_t sum{};
    auto left = cursor;
    auto right = static_cast<std::uint8_t>(cursor == 0U ? Taps - 1U : cursor - 1U);
    // Iteration 31 pairs the center sample with itself; its half coefficient doubles exactly.
    for (std::uint16_t tap{}; tap < 32U; ++tap) {
        sum += (static_cast<std::int32_t>(history[left]) + history[right]) * HalfCoefficients[tap];
        left = static_cast<std::uint8_t>(left + 1U);
        if (left == Taps) { left = 0U; }
        right = static_cast<std::uint8_t>(right == 0U ? Taps - 1U : right - 1U);
    }
    output = rounded(sum);
    clipped_output = clipped != 0U;
    return true;
}

DecimationResult SlowDecimator::push(std::uint16_t sample) noexcept
{
    if (sample > 4095U) { invalid_ = true; }
    if (invalid_) { return {DecimationState::Invalid, 0U, false, false}; }
    const auto centered = static_cast<std::int16_t>(static_cast<std::int32_t>(sample) - 2048);
    std::int16_t intermediate{}, output{};
    bool intermediate_clipped{}, output_clipped{};
    const auto waiting = primed_ ? DecimationState::Accumulating : DecimationState::Warming;
    if (!first_.push(centered, sample == 0U || sample == 4095U, intermediate, intermediate_clipped) ||
        !second_.push(intermediate, intermediate_clipped, output, output_clipped)) {
        return {waiting, 0U, false, false};
    }
    primed_ = true;
    const auto code = static_cast<std::int32_t>(output) + 2048;
    const bool clamped = code < 0 || code > 4095;
    const auto bounded = code < 0 ? 0 : (code > 4095 ? 4095 : code);
    return {DecimationState::Output, static_cast<std::uint16_t>(bounded), output_clipped, clamped};
}

static_assert(sizeof(SlowDecimator) <= 304U, "Retain slow-mode SRAM headroom");
} // namespace dso
