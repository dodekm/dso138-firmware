#ifndef DSO_STM32_PANEL_HPP
#define DSO_STM32_PANEL_HPP

#include "dso_panel_sampler.hpp"

namespace dso {

/* Explicit startup gate for the generated classic-board ADC2/GPIO profile.
 * false revokes permission using RAM only: no hardware read/write or start.
 * true requires foreground execution with unmasked interrupts, ADC1 and ADC2
 * initialized and disabled, independent mode, 12 MHz ADC clock, PA1-PA3 analog
 * inputs and PB12-PB15 pulled-up inputs. It calibrates ADC2 through the shared
 * DWT/iteration-bounded helper, then leaves ADC2 disabled. No SysTick-dependent
 * wait is used. Call only before waveform DMA begins.
 *
 * The board pin map and electrical profile must be verified by the caller.
 * This function does not establish physical thresholds, analog settling or
 * hardware correctness. It never configures output pins or changes ADC1/DMA.
 */
[[nodiscard]] bool prepare_stm32_panel(bool pinmap_verified) noexcept;

/* Dormant callbacks until successful preparation. Single foreground owner;
 * no other code may use ADC2 or change its shared clock/independent mode while
 * sampling. Runtime poll inspects EOC once, never waits for a conversion.
 */
[[nodiscard]] PanelSamplerPort stm32_panel_port() noexcept;

} // namespace dso

#endif
