#include "dso_nominal_front_end.hpp"

namespace dso {
namespace {
constexpr std::uint32_t MinimumSensitivity{10000U};
constexpr std::uint32_t MaximumSensitivity{5000000U};
constexpr std::uint32_t RangeStep{10000U};
constexpr std::int32_t NominalZeroCodeQ16{135573463};
constexpr std::uint32_t LowRangeGainQ16{6600000U};
constexpr std::uint32_t HighRangeGainQ16{6666000U};
constexpr VoltageCalibration Unavailable{0,0U,CalibrationKind::Unavailable};

bool canonical_sensitivity(std::uint32_t value) noexcept
{
    return value >= MinimumSensitivity && value <= MaximumSensitivity && value % RangeStep == 0U &&
        (value / RangeStep == 1U || value / RangeStep == 2U || value / RangeStep == 5U ||
         value / RangeStep == 10U || value / RangeStep == 20U || value / RangeStep == 50U ||
         value / RangeStep == 100U || value / RangeStep == 200U || value / RangeStep == 500U);
}

bool supported_coupling(InputCoupling coupling) noexcept
{
    return coupling == InputCoupling::Dc || coupling == InputCoupling::Ac ||
        coupling == InputCoupling::Ground;
}
} // namespace

VoltageCalibration classic_gain8_nominal_calibration(const PhysicalSettings& physical) noexcept
{
    if (physical.generation == 0U || !canonical_sensitivity(physical.microvolts_per_div) ||
        !supported_coupling(physical.coupling)) { return Unavailable; }

    const auto decade_gain = physical.microvolts_per_div >= 1000000U ?
        HighRangeGainQ16 : LowRangeGainQ16;
    const auto gain = (physical.microvolts_per_div / RangeStep) * decade_gain;
    return {NominalZeroCodeQ16,gain,CalibrationKind::Nominal};
}

VoltageCalibration classic_gain2_nominal_calibration(const PhysicalSettings& physical) noexcept
{
    if (physical.generation == 0U || !canonical_sensitivity(physical.microvolts_per_div) ||
        !supported_coupling(physical.coupling)) { return Unavailable; }
    const auto range = physical.microvolts_per_div / RangeStep;
    const auto shift = static_cast<std::uint8_t>(range == 200U ? 1U : range == 500U ? 2U : 0U);
    const auto unit_gain = range >= 100U ? 26664000U : 26400000U;
    const auto mantissa = (range >> shift) * unit_gain;
    return {NominalZeroCodeQ16,mantissa,CalibrationKind::Nominal,shift};
}

} // namespace dso
