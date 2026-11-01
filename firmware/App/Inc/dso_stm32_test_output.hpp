#ifndef DSO_STM32_TEST_OUTPUT_HPP
#define DSO_STM32_TEST_OUTPUT_HPP

#include <cstdint>

namespace dso {

// Fixed digital candidate: TIM3CLK=72MHz, PSC=1, ARR=35999, CCR2=18000.
// This identity is a caller's record token, not proof of the physical wiring.
struct Stm32TestOutputProfile final {
    std::uint64_t board_identity;
};

// These flags record qualification performed externally; the driver cannot
// establish wiring, output voltage/loading, or acceptable first-pulse behavior.
struct Stm32TestOutputQualification final {
    bool pinmap_verified;
    bool output_load_verified;
    bool waveform_profile_verified;
    bool start_stop_sequence_verified;
};

// Assertions supplied by a startup owner with independent knowledge of the
// boot path. This value is not evidence by itself and must never be inferred
// from a reset-cause register, BSS, or a retained token.
struct Stm32Tim3StartupContract final {
    bool device_wide_system_reset;
    bool direct_main_flash_boot;
    bool no_bootloader_or_earlier_tim3_owner;
    bool no_prior_tim3_configuration;
};

enum class Stm32TestOutputState : std::uint8_t { Unqualified, Running, Fault };
enum class Stm32TestOutputFault : std::uint8_t {
    None, Qualification, InvalidProfile, Execution, Clocks, Ownership,
    Configuration, Timing, Busy
};

// Single foreground owner; no heap, interrupts, DMA or adjustable waveform.
// The original start path requires GPIOA/GPIOB/AFIO/TIM3 clocks already on,
// none held in reset, and TIM3 stopped (CEN, CCER, DIER, SMCR and CR2 clear).
// Exclusive ownership is a caller precondition: the driver does not discover
// ownership from readback,
// and start may overwrite a stopped prior configuration with caller consent.
// The system-reset path separately accepts a complete startup contract and
// provisions gated TIM3 only after its reset image is checked. Neither path
// derives reset/boot provenance from BSS, reset-cause registers or tokens.
// Hardware use remains disabled by default; the opt-in bring-up profile passes
// explicit bench assumptions, which are not measured or proven by this API.
struct Stm32TestOutputApi final {
    bool (*start)(const Stm32TestOutputProfile&, Stm32TestOutputQualification) noexcept;
    bool (*shutdown)(Stm32TestOutputQualification) noexcept;
    Stm32TestOutputState (*state)() noexcept;
    Stm32TestOutputFault (*fault)() noexcept;
    bool (*output_may_be_active)() noexcept;
};

// Factory/getters/incomplete qualification have no peripheral IO. Revocation
// is RAM-only and retains possible-active history until qualified shutdown.
[[nodiscard]] const Stm32TestOutputApi& stm32_test_output_api() noexcept;

// Direct typed call paths use the same guarded implementations as the API.
// The first requires an already-clocked, stopped TIM3 and caller-owned access.
[[nodiscard]] bool start_stm32_test_output(
    const Stm32TestOutputProfile&, Stm32TestOutputQualification) noexcept;
// The startup owner supplies independent reset/direct-Flash/TIM3-history
// assertions. Incomplete qualification or contract has no peripheral effects.
[[nodiscard]] bool start_stm32_test_output_after_system_reset(
    const Stm32TestOutputProfile&, Stm32Tim3StartupContract,
    Stm32TestOutputQualification) noexcept;

} // namespace dso
#endif
