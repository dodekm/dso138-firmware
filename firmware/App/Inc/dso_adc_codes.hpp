#ifndef DSO_ADC_CODES_HPP
#define DSO_ADC_CODES_HPP

#include "dso_adc_contract.hpp"

#include <cstdint>

namespace dso {

// STM32F103 ADC1/ADC2 produce unsigned 12-bit conversion codes.
inline constexpr std::uint16_t AdcMax{adc_contract::MaximumCode};

} // namespace dso

#endif // DSO_ADC_CODES_HPP
