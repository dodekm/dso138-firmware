#include "dso_timebase.hpp"

#include <limits>

namespace dso {
namespace {

constexpr std::uint32_t TimerFactorMax{65536U};
constexpr std::uint32_t AdcMinHz{600000U};
constexpr std::uint32_t AdcMaxHz{14000000U};
constexpr std::uint16_t ConversionAndGuardX2{31U};

constexpr bool sampling_time_valid(std::uint16_t cycles_x2) noexcept
{
    switch (cycles_x2) {
    case 3:
    case 15:
    case 27:
    case 57:
    case 83:
    case 111:
    case 143:
    case 479:
        return true;
    default:
        return false;
    }
}

/* Avoid the overflow-prone (numerator + denominator - 1) idiom. */
constexpr std::uint64_t divide_ceil(std::uint64_t numerator,
                                    std::uint64_t denominator) noexcept
{
    return numerator / denominator +
           ((numerator % denominator != 0U) ? std::uint64_t{1} : std::uint64_t{0});
}

} // namespace

bool plan_timebase(std::uint32_t timer_hz,
                   std::uint32_t adc_hz,
                   std::uint16_t sampling_cycles_x2,
                   std::uint32_t requested_hz,
                   TimebasePlan *out) noexcept
{
    if (out == nullptr || timer_hz == 0U || requested_hz == 0U ||
        adc_hz < AdcMinHz || adc_hz > AdcMaxHz ||
        !sampling_time_valid(sampling_cycles_x2)) {
        return false;
    }

    const auto budget_x2 = static_cast<std::uint16_t>(sampling_cycles_x2 +
                                                      ConversionAndGuardX2);

    /* Products are bounded by UINT32_MAX * 510, safely inside uint64_t. */
    if (static_cast<std::uint64_t>(requested_hz) * budget_x2 >
        static_cast<std::uint64_t>(adc_hz) * 2U) {
        return false;
    }

    auto required = divide_ceil(timer_hz, requested_hz);
    const auto adc_required = divide_ceil(static_cast<std::uint64_t>(timer_hz) *
                                          budget_x2,
                                          static_cast<std::uint64_t>(adc_hz) * 2U);
    if (adc_required > required) {
        required = adc_required;
    }

    /*
     * Enumerate PSC counts, not arbitrary rates: for a given count the least
     * legal ARR count is ceil(required / PSC). Every other ARR is worse.
     * Ascending PSC and a strict improvement preserve the lower-PSC tie rule.
     * Counts and products are widened before multiplication (max 2^32).
     */
    auto best_divider = std::numeric_limits<std::uint64_t>::max();
    std::uint32_t best_prescaler{0U};
    std::uint32_t best_period{0U};
    for (std::uint32_t prescaler_count{1U}; prescaler_count <= TimerFactorMax;
         ++prescaler_count) {
        const auto period_count = divide_ceil(required, prescaler_count);

        if (period_count > TimerFactorMax) {
            continue;
        }
        const auto divider = static_cast<std::uint64_t>(prescaler_count) *
                              period_count;
        if (divider > std::numeric_limits<std::uint32_t>::max() ||
            divider >= best_divider) {
            continue;
        }
        best_divider = divider;
        best_prescaler = prescaler_count - 1U;
        best_period = static_cast<std::uint32_t>(period_count - 1U);
        if (divider == required) {
            break; /* Exact bound reached; later PSC cannot improve it. */
        }
    }

    if (best_divider == std::numeric_limits<std::uint64_t>::max()) {
        return false;
    }

    const TimebasePlan result{
        static_cast<std::uint16_t>(best_prescaler),
        static_cast<std::uint16_t>(best_period),
        timer_hz,
        static_cast<std::uint32_t>(best_divider),
        adc_hz,
        budget_x2
    };
    *out = result;
    return true;
}

} // namespace dso
