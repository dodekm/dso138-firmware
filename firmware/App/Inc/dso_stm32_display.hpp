#ifndef DSO_STM32_DISPLAY_HPP
#define DSO_STM32_DISPLAY_HPP

#include "dso_display.hpp"

namespace dso {

struct Stm32DisplayQualification final {
    bool pinmap_verified;
    bool supply_verified;
    bool gpio_electrical_verified;
    bool no_jtag_or_swo_verified;
};

enum class Stm32DisplayState : std::uint8_t { Dormant, Prepared, Fault };
enum class Stm32DisplayFault : std::uint8_t {
    None, Qualification, Execution, Clocks, Ownership, Configuration, Timing, Protocol
};

// Explicit foreground startup/recovery gate for the classic write-only LCD bus.
// Every qualification flag records an EXTERNAL result, not a test made here.
// Initial incomplete qualification revokes permission using RAM only. If LCD
// DMA has already been enabled, re-preparation first synchronously aborts its
// readers before revoking authority. No pins are silently reallocated.
//
// Full preparation requires stopped waveform/control ADCs, DMA1_CH1 and TIM2,
// unmasked foreground execution and SYSCLK/HCLK/PCLK2=72MHz. Caller must enable
// the BKP interface clock beforehand so retained PC13 ownership can be read.
// LSE/bypass, tamper and RTC pin outputs must already be inactive; this adapter
// never resets/rewrites backup state. NOJTAG with SWD retained is an external
// precondition because SWJ_CFG does not have reliable register readback.
//
// Only PB0..7,PB10..11 and PC13..15 become 2MHz push-pull outputs. A measured
// electrical profile must establish PC13..15 current/load/edge compliance.
// Factory/getters are always RAM-only. No preparation happens automatically.
[[nodiscard]] bool prepare_stm32_display(Stm32DisplayQualification value) noexcept;
[[nodiscard]] DisplayBus stm32_display_bus() noexcept;
// Explicit UID-bound bring-up only, after LCD initialization and before
// acquisition. Claims TIM1 and DMA1 channels2/3/5 without alternate GPIO outputs.
[[nodiscard]] bool enable_stm32_display_dma() noexcept;
[[nodiscard]] Stm32DisplayState stm32_display_state() noexcept;
[[nodiscard]] Stm32DisplayFault stm32_display_fault() noexcept;

// Single foreground owner; callbacks are not interrupt/reentrant interfaces.
// Reset and millisecond waits require stopped acquisition. Ordinary byte/span
// writes may coexist with acquisition, subject to measured service deadlines.
// RD stays high; controller-ID reads and direction turnaround are not provided.
// The existing display driver still requires independently verified identity.
// A callback fault latches until explicit full preparation and LCD reinitialize.

} // namespace dso
#endif
