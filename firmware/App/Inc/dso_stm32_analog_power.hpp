#ifndef DSO_STM32_ANALOG_POWER_HPP
#define DSO_STM32_ANALOG_POWER_HPP

#include <cstdint>

namespace dso {

// Digital candidate only: TIM4CLK=72MHz, PSC=0, ARR=4095, PWM1 active high.
// The external board qualification must establish safe converter operation,
// startup partial pulses and the static-low shutdown/rail-collapse procedure.
struct Stm32AnalogPowerProfile final {
    std::uint64_t board_identity;
    std::uint16_t generator_compare;
    std::uint16_t initial_reference_compare;
};

struct Stm32AnalogPowerQualification final {
    bool pinmap_verified;
    bool input_supply_verified;
    bool switching_profile_verified;
    bool start_stop_sequence_verified;
};

enum class Stm32AnalogPowerState : std::uint8_t { Unqualified, CarrierRunning, Fault };
enum class Stm32AnalogPowerFault : std::uint8_t {
    None, Qualification, InvalidProfile, Execution, Clocks, Ownership,
    Configuration, Timing, Busy
};

// One foreground owner of TIM4 and PB8/PB9. No ISR, DMA, heap, rail inference or
// controller-to-voltage mapping. Factory/getters and incomplete qualification
// are RAM-only. Revocation never physically stops an already connected carrier;
// carrier_may_be_active remains true until an explicitly qualified shutdown.
struct Stm32AnalogPowerApi final {
    bool (*start)(const Stm32AnalogPowerProfile&, Stm32AnalogPowerQualification) noexcept;
    // Accepted means CCR3 PRELOAD queued, not active compare/analog settling.
    // An out-of-range request is rejected without IO or changing prior state.
    bool (*queue_reference)(std::uint16_t compare) noexcept;
    bool (*shutdown)(Stm32AnalogPowerQualification) noexcept;
    Stm32AnalogPowerState (*state)() noexcept;
    Stm32AnalogPowerFault (*fault)() noexcept;
    bool (*carrier_may_be_active)() noexcept;
};

// The immutable function table lives in Flash; this call has no peripheral IO.
// Start/shutdown require stopped waveform and control acquisition. Runtime
// reference writes never reset/retune/stop the shared carrier and may coexist
// with acquisition subject to a future measured analog-reference policy.
// No API reports healthy analog rails from timer state or elapsed time.
[[nodiscard]] const Stm32AnalogPowerApi& stm32_analog_power_api() noexcept;

} // namespace dso
#endif
