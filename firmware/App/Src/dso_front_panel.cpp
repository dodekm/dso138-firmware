#include "dso_front_panel.hpp"
#include "dso_u32_counter.hpp"

namespace dso {
namespace {

constexpr std::uint16_t InvalidAdc{4096U};

std::uint32_t nominal_base(SwitchPosition position) noexcept
{
    switch (position) {
    case SwitchPosition::Low: return 1000000U;
    case SwitchPosition::Middle: return 100000U;
    case SwitchPosition::High: return 10000U;
    default: return 0U;
    }
}

std::uint32_t nominal_multiplier(SwitchPosition position) noexcept
{
    switch (position) {
    case SwitchPosition::Low: return 5U;
    case SwitchPosition::Middle: return 2U;
    case SwitchPosition::High: return 1U;
    default: return 0U;
    }
}

InputCoupling coupling_for(SwitchPosition position) noexcept
{
    switch (position) {
    case SwitchPosition::Low: return InputCoupling::Ground;
    case SwitchPosition::Middle: return InputCoupling::Ac;
    case SwitchPosition::High: return InputCoupling::Dc;
    default: return InputCoupling::Unknown;
    }
}

} // namespace

std::uint8_t normalize_panel_buttons(std::uint16_t gpio_b_idr) noexcept
{
    std::uint8_t mask{0U};
    if ((gpio_b_idr & 0x8000U) == 0U) { mask |= DebouncedButtons::Ok; }
    if ((gpio_b_idr & 0x4000U) == 0U) { mask |= DebouncedButtons::Plus; }
    if ((gpio_b_idr & 0x2000U) == 0U) { mask |= DebouncedButtons::Minus; }
    if ((gpio_b_idr & 0x1000U) == 0U) { mask |= DebouncedButtons::Select; }
    return mask;
}

FrontPanel::FrontPanel(const FrontPanelConfig& config) noexcept
    : sen1_{config.sen1_bands, config.switch_qualification_ms,
            config.maximum_poll_gap_ms},
      sen2_{config.sen2_bands, config.switch_qualification_ms,
            config.maximum_poll_gap_ms},
      coupling_{config.coupling_bands, config.switch_qualification_ms,
                config.maximum_poll_gap_ms},
      buttons_{config.button_debounce_ms, config.maximum_poll_gap_ms},
      analog_settling_ms_{config.analog_settling_ms},
      valid_settling_{config.analog_settling_ms < counter_contract::U32HalfRange}
{
}

PanelUpdate FrontPanel::update(const PanelObservation& observation) noexcept
{
    const auto key_events = buttons_.update(
        normalize_panel_buttons(observation.gpio_b_idr), observation.now_ms);
    const auto sen1 = sen1_.update(
        observation.senses_valid ? observation.sen1_adc : InvalidAdc,
        observation.now_ms);
    const auto sen2 = sen2_.update(
        observation.senses_valid ? observation.sen2_adc : InvalidAdc,
        observation.now_ms);
    const auto coupling = coupling_.update(
        observation.senses_valid ? observation.coupling_adc : InvalidAdc,
        observation.now_ms);
    bool accepted{false};

    if (!valid_settling_ || sen1 == SwitchPosition::Unknown ||
        sen2 == SwitchPosition::Unknown || coupling == SwitchPosition::Unknown) {
        ready_ = false;
        settling_ = false;
        current_ = PhysicalSettings{0U, 0U, InputCoupling::Unknown};
    } else if (!ready_) {
        if (!settling_) {
            settling_ = true;
            settling_since_ = observation.now_ms;
        }
        const auto age = static_cast<std::uint32_t>(observation.now_ms - settling_since_);
        if (age >= counter_contract::U32HalfRange) {
            settling_since_ = observation.now_ms;
        } else if (age >= analog_settling_ms_) {
            generation_ = next_panel_generation(generation_);
            current_ = PhysicalSettings{generation_, nominal_base(sen1) *
                nominal_multiplier(sen2), coupling_for(coupling)};
            ready_ = true;
            accepted = true;
        }
    }

    return PanelUpdate{current_, key_events, ready_, !ready_ || accepted, accepted};
}

void FrontPanel::resynchronize() noexcept
{
    sen1_.invalidate(); sen2_.invalidate(); coupling_.invalidate();
    buttons_.require_release();
    ready_ = false; settling_ = false;
    current_ = PhysicalSettings{0U, 0U, InputCoupling::Unknown};
}

} // namespace dso
