#ifndef DSO_TIMEBASE_HPP
#define DSO_TIMEBASE_HPP

#include <cstdint>

namespace dso {

struct TimebasePlan final {
    std::uint16_t prescaler; /* Timer PSC register: divisor minus one. */
    std::uint16_t period;    /* Timer ARR register: divisor minus one. */
    std::uint32_t timer_hz;
    std::uint32_t divider;   /* (PSC + 1) * (ARR + 1), never zero. */
    std::uint32_t adc_hz;
    std::uint16_t trigger_cycles_x2;
};

// Bounds applied when a captured TimebasePlan is consumed as record metadata.
// The 1 MHz lower bound is analysis policy; the 72 MHz upper bound matches the
// current STM32F103 timer clock profile. The generic planner remains unrestricted
// by these record-consumer limits.
inline constexpr std::uint32_t MinimumRecordTimerHz{1000000U};
inline constexpr std::uint32_t MaximumRecordTimerHz{72000000U};

[[nodiscard]] constexpr bool valid_record_timing(const TimebasePlan& timing) noexcept
{
    if (timing.timer_hz < MinimumRecordTimerHz || timing.timer_hz > MaximumRecordTimerHz ||
        timing.divider == 0U) {
        return false;
    }
    const auto prescaler = std::uint64_t{timing.prescaler} + 1U;
    const auto period = std::uint64_t{timing.period} + 1U;
    return prescaler * period == timing.divider;
}

/*
 * Plan a periodic regular ADC trigger using 16-bit PSC and ARR registers.
 * The exact nominal sample rate is timer_hz / divider, a rational number.
 * This computes register values; it does not measure the actual clock or prove
 * ADC settling, trigger routing, DMA service time, or board signal accuracy.
 *
 * adc_hz must be 600 kHz..14 MHz (STM32F103x8/xB DS5319 Rev 20, table 47).
 * sampling_cycles_x2 must be one of 3,15,27,57,83,111,143,479 (RM0008 SMPR).
 * The project conservatively budgets sample + 12.5 conversion + 3 guard
 * ADCCLK cycles between external triggers. The guard extends DS5319's minimum
 * 17-cycle trigger period; it is a project policy, not a general ST formula.
 * requested_hz must itself fit that budget, even if rounding could produce a
 * slower realizable timer rate. An impossible request returns false.
 *
 * Among representable divisors, choose the smallest total divider that does
 * not exceed the requested rate and satisfies the ADC budget. On ties, choose
 * the smallest prescaler. The 2^32 divider (PSC=ARR=65535) cannot be represented
 * by the output's uint32_t divider field and is excluded.
 *
 * Return false for invalid inputs or an unrepresentable plan. On failure *out
 * is unchanged. All calculations are integer-only; no dynamic allocation.
 */
[[nodiscard]] bool plan_timebase(std::uint32_t timer_hz,
                                 std::uint32_t adc_hz,
                                 std::uint16_t sampling_cycles_x2,
                                 std::uint32_t requested_hz,
                                 TimebasePlan *out) noexcept;

} // namespace dso

#endif
