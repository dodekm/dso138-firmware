#ifndef DSO_CALIBRATION_CONTRACT_HPP
#define DSO_CALIBRATION_CONTRACT_HPP

#include "dso_adc_contract.hpp"

#include <cstdint>

namespace dso::calibration_contract {

inline constexpr std::int32_t MaximumZeroCodeQ16{
    static_cast<std::int32_t>(adc_contract::MaximumCode) * 65536};
inline constexpr std::uint8_t MaximumGainShift{2U};
inline constexpr std::uint32_t MinimumShiftedGainQ16{0x80000000U};

// Calibration kind is layer-owned; this validates the common numeric fields.
[[nodiscard]] constexpr bool profile_values_valid(std::int32_t zero_code_q16,
                                                   std::uint32_t gain_q16,
                                                   std::uint8_t gain_shift) noexcept
{
    return zero_code_q16 >= 0 && zero_code_q16 <= MaximumZeroCodeQ16 && gain_q16 != 0U &&
           gain_shift <= MaximumGainShift &&
           (gain_shift == 0U || gain_q16 >= MinimumShiftedGainQ16);
}

static_assert(MaximumZeroCodeQ16 == 268369920,
              "The persisted calibration zero must span the 12-bit ADC code range");

} // namespace dso::calibration_contract

#endif // DSO_CALIBRATION_CONTRACT_HPP
