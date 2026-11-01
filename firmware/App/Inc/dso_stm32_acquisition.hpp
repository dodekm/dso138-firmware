#ifndef DSO_STM32_ACQUISITION_HPP
#define DSO_STM32_ACQUISITION_HPP

#include "dso_acquisition.hpp"

namespace dso {

struct Stm32AcquisitionQualification final {
    bool analog_power_verified;
    bool adc_input_settling_verified;
};

// Dormant by default. Supplying true records an EXTERNAL qualification result,
// not a test performed by this function. Reject configuration while active.
// The default application never grants these permissions or starts hardware.
[[nodiscard]] bool qualify_stm32_acquisition(Stm32AcquisitionQualification value) noexcept;
[[nodiscard]] AcquisitionPort stm32_acquisition_port() noexcept;
// Two-halfword circular DMA for the separate slow/rolling transport. Both
// factories share ADC1/TIM2/DMA1_CH1, HAL handles and qualification; they are
// alternative owners, never simultaneous channels. Controller must stop the
// current backend before selecting the other. Runtime observations use only
// HAL milliseconds (cycles=0); bounded startup calibration still uses DWT.
[[nodiscard]] AcquisitionPort stm32_rolling_port() noexcept;
// Separate strict 16-halfword transport for the 25:1 software-filtered slow
// path. ADC timing is the RAW timer plan; output timing is owned by the rolling
// core. It shares the same hardware/qualification as the two factories above.
// The distinct factory is essential: raw500s/div and point20s/div have the same
// timer divider, so the plan alone cannot identify the intended DMA length.
[[nodiscard]] AcquisitionPort stm32_oversampled_rolling_port() noexcept;
// Exclusive DMA1 channel1 IRQ owner: handles TE, leaves HT/TC to foreground.
void stm32_acquisition_dma_irq() noexcept;

} // namespace dso
#endif
