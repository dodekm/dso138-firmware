#ifndef DSO_TRIGGER_LED_HPP
#define DSO_TRIGGER_LED_HPP

#include <cstdint>

namespace dso {

// Foreground pulse policy. The caller drains both owners' one-consumer event
// flags even while unhealthy, so discarded events cannot queue a later pulse.
class TriggerLedPulse final {
public:
    [[nodiscard]] bool service(std::uint32_t now_ms, bool accepted_edge,
                               bool healthy) noexcept
    {
        if (!healthy) { active_ = false; return false; }
        if (accepted_edge) {
            started_ms_ = now_ms;
            active_ = true;
        } else if (active_ && static_cast<std::uint32_t>(now_ms - started_ms_) >= 100U) {
            active_ = false;
        }
        return active_;
    }

    void cancel() noexcept { active_ = false; }

    [[nodiscard]] bool active() const noexcept { return active_; }

private:
    std::uint32_t started_ms_{};
    bool active_{};
};

// PA15 output is acquired only after the UID guarded board activation succeeds.
void initialize_stm32_trigger_led() noexcept;
void service_stm32_trigger_led(std::uint32_t now_ms, bool accepted_edge,
                               bool healthy) noexcept;
void cancel_stm32_trigger_led() noexcept;

} // namespace dso
#endif
