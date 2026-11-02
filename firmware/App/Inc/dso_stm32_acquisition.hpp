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
// Exclusive DMA1 channel1 IRQ owner: handles TE, leaves HT/TC to foreground.
void stm32_acquisition_dma_irq() noexcept;

} // namespace dso
#endif
