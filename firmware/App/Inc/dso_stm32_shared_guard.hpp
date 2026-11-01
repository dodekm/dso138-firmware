#ifndef DSO_STM32_SHARED_GUARD_HPP
#define DSO_STM32_SHARED_GUARD_HPP

namespace dso {

// Shared out-of-line predicates; no state, writes or hardware qualification.
// Callers retain responsibility for permission checks and fault classification.
[[nodiscard]] bool stm32_foreground() noexcept;

// Snapshot APB1, APB2 and AHB clock enables, then inspect only clock-enabled
// TIM2/ADC1/ADC2/DMA1-channel1 owners. This remains the display/power contract.
[[nodiscard]] bool stm32_stopped_acquisition() noexcept;

// Separate FPEC precondition: rejects running TIM2/TIM1, enabled ADC1/ADC2,
// active TIM1 DMA requests and any active DMA1 channel. It snapshots RCC
// gates before peripheral reads, writes no peripheral, and must pass both the
// first preflight and the masked final preflight in the storage adapter.
[[nodiscard]] bool stm32_stopped_storage_producers() noexcept;

} // namespace dso
#endif
