#ifndef DSO_BRINGUP_PUBLICATION_HPP
#define DSO_BRINGUP_PUBLICATION_HPP

#include "dso_board_bringup.hpp"
#include "dso_measurements.hpp"

#include <cstdint>
#include <limits>

namespace dso {

inline constexpr std::uint32_t BringupSummaryValidMask{1U << 31U};
inline constexpr std::uint32_t BringupSummary1024SamplesMask{1U << 30U};
inline constexpr std::uint32_t BringupSummaryClippedMask{1U << 29U};
inline constexpr std::uint32_t BringupSummaryMinimumMask{0x00000fffU};
inline constexpr unsigned BringupSummaryMaximumShift{12U};

struct BringupFrameSummary final {
    std::uint32_t code_flags{};
    std::uint32_t frequency_millihz{};
    std::uint32_t physical_generation{};
    std::uint32_t acquisition_epoch{};
};

// External SWD publication formatter. The pointer must be the complete result
// returned by ScopeController::measurements(); it is not a validator for an
// arbitrary MeasurementResult supplied by an untrusted caller.
inline BringupFrameSummary summarize_bringup_frame(const MeasurementResult* result) noexcept
{
    BringupFrameSummary summary{};
    if (result == nullptr) { return summary; }

    summary.physical_generation = result->physical_generation;
    summary.acquisition_epoch = result->acquisition_epoch;
    summary.code_flags = static_cast<std::uint32_t>(result->minimum_code) |
        (static_cast<std::uint32_t>(result->maximum_code) << 12U) |
        (static_cast<std::uint32_t>(result->frequency_status) << 24U) |
        (static_cast<std::uint32_t>(result->calibration_kind) << 27U) |
        (result->adc_clipped ? BringupSummaryClippedMask : 0U) |
        (result->count == 1024U ? BringupSummary1024SamplesMask : 0U);

    if (result->frequency_millihz <= std::numeric_limits<std::uint32_t>::max() &&
        (result->frequency_status != FrequencyStatus::Valid || result->frequency_microhertz != 0U)) {
        summary.code_flags |= BringupSummaryValidMask;
        summary.frequency_millihz = static_cast<std::uint32_t>(result->frequency_millihz);
    }
    return summary;
}

} // namespace dso
#endif
