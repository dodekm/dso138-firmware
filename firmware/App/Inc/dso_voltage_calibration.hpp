#ifndef DSO_VOLTAGE_CALIBRATION_HPP
#define DSO_VOLTAGE_CALIBRATION_HPP

#include <cstddef>
#include <cstdint>

namespace dso {

enum class CalibrationKind : std::uint8_t { Unavailable, Nominal, Measured };

struct VoltageCalibration final {
    std::int32_t zero_code_q16;
    std::uint32_t microvolts_per_code_q16; // Mantissa; effective Q16 gain is multiplied by 2^gain_shift.
    CalibrationKind kind;
    std::uint8_t gain_shift{};
};
static_assert(sizeof(VoltageCalibration) == 12U && alignof(VoltageCalibration) == 4U);
static_assert(offsetof(VoltageCalibration, gain_shift) == 9U);

[[nodiscard]] bool calibration_valid(VoltageCalibration calibration) noexcept;
// Nearest integer microvolt; half ties are rounded away from zero. No default
// board gain or voltage reference is inferred. Failure leaves output unchanged.
[[nodiscard]] bool code_to_microvolts(std::uint16_t code, VoltageCalibration calibration,
                                     std::int64_t& microvolts) noexcept;

} // namespace dso

#endif // DSO_VOLTAGE_CALIBRATION_HPP
