#ifndef DSO_PHYSICAL_SETTINGS_HPP
#define DSO_PHYSICAL_SETTINGS_HPP

#include <array>
#include <cstdint>

namespace dso {

// Ordered scale keys are also the calibration bank's persistent DC-slot order.
// Changes require reviewing its record compatibility, even though keys are stored explicitly.
inline constexpr std::array<std::uint32_t, 9U> PhysicalScaleCatalogueMicrovoltsPerDivision{
    10000U, 20000U, 50000U, 100000U, 200000U, 500000U, 1000000U, 2000000U, 5000000U};
inline constexpr std::uint32_t PhysicalScaleStepMicrovoltsPerDivision{
    PhysicalScaleCatalogueMicrovoltsPerDivision.front()};
inline constexpr std::uint32_t MinimumPhysicalMicrovoltsPerDivision{
    PhysicalScaleCatalogueMicrovoltsPerDivision.front()};
inline constexpr std::uint32_t MaximumPhysicalMicrovoltsPerDivision{
    PhysicalScaleCatalogueMicrovoltsPerDivision.back()};

[[nodiscard]] constexpr bool physical_scale_supported(std::uint32_t microvolts_per_div) noexcept
{
    const auto step_index = microvolts_per_div / PhysicalScaleStepMicrovoltsPerDivision;
    return microvolts_per_div >= MinimumPhysicalMicrovoltsPerDivision &&
        microvolts_per_div <= MaximumPhysicalMicrovoltsPerDivision &&
        microvolts_per_div % PhysicalScaleStepMicrovoltsPerDivision == 0U &&
        (step_index == 1U || step_index == 2U || step_index == 5U || step_index == 10U ||
         step_index == 20U || step_index == 50U || step_index == 100U ||
         step_index == 200U || step_index == 500U);
}

[[nodiscard]] constexpr bool physical_scale_catalogue_matches_predicate() noexcept
{
    for (const auto scale : PhysicalScaleCatalogueMicrovoltsPerDivision) {
        if (!physical_scale_supported(scale)) { return false; }
    }
    for (std::uint32_t index{1U};
         index < PhysicalScaleCatalogueMicrovoltsPerDivision.size(); ++index) {
        if (PhysicalScaleCatalogueMicrovoltsPerDivision[index - 1U] >=
            PhysicalScaleCatalogueMicrovoltsPerDivision[index]) {
            return false;
        }
    }
    for (auto scale = MinimumPhysicalMicrovoltsPerDivision;
         scale <= MaximumPhysicalMicrovoltsPerDivision;
         scale += PhysicalScaleStepMicrovoltsPerDivision) {
        bool catalogued{};
        for (const auto candidate : PhysicalScaleCatalogueMicrovoltsPerDivision) {
            catalogued = catalogued || candidate == scale;
        }
        if (catalogued != physical_scale_supported(scale)) { return false; }
    }
    return true;
}

static_assert(physical_scale_catalogue_matches_predicate(),
              "Keep nominal scale eligibility equal to calibration slot keys");

enum class InputCoupling : std::uint8_t { Unknown, Dc, Ac, Ground };

/* Metadata is copied by value into a frozen acquisition. The scale is the
 * nominal front-panel label, not a calibrated ADC-code conversion factor.
 * generation=0, scale=0 and Unknown coupling mean the physical state is invalid.
 */
struct PhysicalSettings final {
    std::uint32_t generation;
    std::uint32_t microvolts_per_div;
    InputCoupling coupling;
};

// Compare the full physical identity used to validate captures and calibration
// sessions. Consumers keep readiness and invalidation policy local.
[[nodiscard]] constexpr bool same_physical_settings(
    const PhysicalSettings& a, const PhysicalSettings& b) noexcept
{
    return a.generation == b.generation &&
        a.microvolts_per_div == b.microvolts_per_div && a.coupling == b.coupling;
}

} // namespace dso

#endif // DSO_PHYSICAL_SETTINGS_HPP
