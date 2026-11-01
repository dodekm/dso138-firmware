#ifndef DSO_STM32_ADC_HPP
#define DSO_STM32_ADC_HPP

#include "main.h"

namespace dso {
// Foreground-only calibration, BEFORE either ADC is acquiring. Both ADCs must
// be disabled, DMA/trigger requests off, handle unlocked, HCLK72/ADCCLK12 MHz.
// No SysTick-dependent polling: DWT deadlines AND fixed loop ceilings bound each
// wait. Direct CR2 calibration sequence follows RM0008 11.4. This is a correctness
// adaptation of HAL's tick-based waits, not a measured performance optimization.
// Success leaves the ADC disabled/READY; failure disables it and records an
// internal error. No injected conversions or common-clock changes are made.
[[nodiscard]] bool calibrate_stm32_adc(ADC_HandleTypeDef& adc) noexcept;
// Power a known stopped/calibrated ADC with a bounded >=1us settling wait.
// Call immediately before HAL start: verified ADON bypasses HAL ADC_Enable's
// tick-dependent failure loop. DWT must already be enabled by preparation.
[[nodiscard]] bool enable_stm32_adc_bounded(ADC_HandleTypeDef& adc) noexcept;
} // namespace dso
#endif
