#ifndef DSO_NOMINAL_FRONT_END_HPP
#define DSO_NOMINAL_FRONT_END_HPP

#include "dso_physical_settings.hpp"
#include "dso_voltage_calibration.hpp"
#include <cstdint>

namespace dso {

// Candidate classic DSO138 nominal transfers for explicitly assumed R11=150 ohm
// U2C gain-8 or R11=1.5 kohm gain-2 networks. Both assume Vref+=3.3 V,
// AV-=-5 V, R9=1 kohm, R10=3 kohm, the documented SEN1 attenuator and SEN2
// x1/x2/x5 dividers. This is not board part-number auto-detection, fitted-value
// verification, or measured calibration. gain_shift extends the Q16 mantissa
// for gain-2 high ranges. Both resolvers remain separate and unused by default;
// selecting one requires an explicit board-topology assumption.
[[nodiscard]] VoltageCalibration classic_gain8_nominal_calibration(
    const PhysicalSettings& physical) noexcept;
[[nodiscard]] VoltageCalibration classic_gain2_nominal_calibration(
    const PhysicalSettings& physical) noexcept;

} // namespace dso

#endif
