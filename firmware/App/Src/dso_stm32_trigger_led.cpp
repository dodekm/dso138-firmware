#include "dso_trigger_led.hpp"
#include "main.h"

namespace dso {
namespace {
TriggerLedPulse pulse{};
bool initialized{};

void write_output(bool on) noexcept
{
    // D3 is connected to +3.3V through R28, so PA15 low sinks LED current.
    GPIOA->BSRR = on ? (1UL << (15U + 16U)) : (1UL << 15U);
}
}

void initialize_stm32_trigger_led() noexcept
{
    if (initialized) { return; }
    pulse.cancel();
    // Preload OFF before making the released JTDI pin an output. Preserve
    // PA8..PA14 and every other CRH field; MODE=10, CNF=00 is 2MHz push-pull.
    GPIOA->BSRR = 1UL << 15U;
    GPIOA->CRH = (GPIOA->CRH & 0x0fffffffUL) | 0x20000000UL;
    initialized = true;
}

void service_stm32_trigger_led(std::uint32_t now_ms, bool accepted_edge,
                               bool healthy) noexcept
{
    if (!initialized) { return; }
    const auto was_active = pulse.active();
    const auto is_active = pulse.service(now_ms, accepted_edge, healthy);
    if (is_active != was_active) { write_output(is_active); }
}

void cancel_stm32_trigger_led() noexcept
{
    const auto was_active = pulse.active();
    pulse.cancel();
    if (initialized && was_active) { write_output(false); }
}
} // namespace dso
