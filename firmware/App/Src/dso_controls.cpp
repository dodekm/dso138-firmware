#include "dso_controls.hpp"
#include "dso_adc_codes.hpp"
#include "dso_u32_counter.hpp"

#include <cstddef>

namespace dso {
namespace {

constexpr bool bands_valid(const SenseBands &bands) noexcept
{
    return bands.low_max < bands.middle_min &&
           bands.middle_min <= bands.middle_max &&
           bands.middle_max < bands.high_min && bands.high_min <= AdcMax;
}

constexpr bool timing_valid(std::uint32_t settle,
                             std::uint32_t maxgap) noexcept
{
    return settle > 0U && settle < counter_contract::U32HalfRange &&
           maxgap > 0U && maxgap < counter_contract::U32HalfRange;
}

constexpr bool poll_discontinuous(std::uint32_t elapsed,
                                   std::uint32_t maxgap) noexcept
{
    return elapsed >= counter_contract::U32HalfRange || elapsed > maxgap;
}

constexpr std::uint8_t button_bit(std::size_t index) noexcept
{
    return static_cast<std::uint8_t>(std::uint32_t{1U} << index);
}

} // namespace

SwitchPosition decode_switch(std::uint16_t raw, const SenseBands &bands) noexcept
{
    if (!bands_valid(bands) || raw > AdcMax) {
        return SwitchPosition::Unknown;
    }
    if (raw <= bands.low_max) {
        return SwitchPosition::Low;
    }
    if (raw >= bands.middle_min && raw <= bands.middle_max) {
        return SwitchPosition::Middle;
    }
    if (raw >= bands.high_min) {
        return SwitchPosition::High;
    }
    return SwitchPosition::Unknown;
}

SwitchFilter::SwitchFilter(SenseBands bands, std::uint32_t settle_ms,
                           std::uint32_t maxgap_ms) noexcept
    : bands_{bands}, settle_ms_{settle_ms}, maxgap_ms_{maxgap_ms},
      valid_{bands_valid(bands) && timing_valid(settle_ms, maxgap_ms)}
{
}

SwitchPosition SwitchFilter::update(std::uint16_t raw,
                                     std::uint32_t now_ms) noexcept
{
    if (!valid_) {
        return SwitchPosition::Unknown;
    }
    const auto decoded = decode_switch(raw, bands_);
    const auto interval = static_cast<std::uint32_t>(now_ms - last_poll_);
    const bool discontinuity = !observed_ || poll_discontinuous(interval, maxgap_ms_);
    observed_ = true;
    last_poll_ = now_ms;

    if (discontinuity || decoded == SwitchPosition::Unknown || decoded != stable_) {
        stable_ = SwitchPosition::Unknown;
    }
    if (discontinuity || decoded != candidate_ || decoded == SwitchPosition::Unknown) {
        candidate_ = decoded;
        candidate_since_ = now_ms;
        return stable_;
    }
    if (stable_ == decoded) {
        return stable_;
    }
    const auto age = static_cast<std::uint32_t>(now_ms - candidate_since_);
    if (age >= counter_contract::U32HalfRange) {
        candidate_since_ = now_ms;
    } else if (age >= settle_ms_) {
        stable_ = decoded;
    }
    return stable_;
}

DebouncedButtons::DebouncedButtons(std::uint32_t debounce_ms,
                                   std::uint32_t maxgap_ms) noexcept
    : debounce_ms_{debounce_ms}, maxgap_ms_{maxgap_ms},
      valid_{timing_valid(debounce_ms, maxgap_ms)}
{
}

std::uint8_t DebouncedButtons::held_mask() const noexcept
{
    std::uint8_t mask{0U};
    for (std::size_t index{0U}; index < buttons_.size(); ++index) {
        if (buttons_[index].stable) {
            mask = static_cast<std::uint8_t>(mask | button_bit(index));
        }
    }
    return mask;
}

void DebouncedButtons::require_release() noexcept
{
    observed_ = false;
    release_required_ = true;
    // Reuse the ordinary per-key release qualification. No pre-blackout
    // candidate, stable value or elapsed interval can satisfy this barrier.
    for (auto& button : buttons_) { button = ButtonState{true, true, 0U}; }
}

ButtonEvents DebouncedButtons::update(std::uint8_t pressed_mask,
                                      std::uint32_t now_ms) noexcept
{
    if (!valid_) {
        return ButtonEvents{0U, 0U, 0U, true};
    }
    pressed_mask = static_cast<std::uint8_t>(pressed_mask & All);
    const auto interval = static_cast<std::uint32_t>(now_ms - last_poll_);
    const bool discontinuity = !observed_ || poll_discontinuous(interval, maxgap_ms_);
    observed_ = true;
    last_poll_ = now_ms;
    ButtonEvents events{0U, 0U, 0U, discontinuity};

    for (std::size_t index{0U}; index < buttons_.size(); ++index) {
        auto &button = buttons_[index];
        const auto bit = button_bit(index);
        const bool raw = (pressed_mask & bit) != 0U;
        // Even a too-short press restarts this key's required release. A new
        // polling discontinuity also invalidates prior release evidence.
        if (release_required_ && (raw || discontinuity)) { button.stable = true; }
        if (discontinuity || raw != button.candidate) {
            button.candidate = raw;
            button.candidate_since = now_ms;
            continue;
        }
        if (button.candidate == button.stable) {
            continue;
        }
        const auto age = static_cast<std::uint32_t>(now_ms - button.candidate_since);
        if (age >= counter_contract::U32HalfRange) {
            button.candidate_since = now_ms;
        } else if (age >= debounce_ms_) {
            button.stable = button.candidate;
            if (button.stable) {
                events.pressed = static_cast<std::uint8_t>(events.pressed | bit);
            } else {
                events.released = static_cast<std::uint8_t>(events.released | bit);
            }
        }
    }
    events.held = held_mask();
    if (release_required_) {
        // A key may have re-pressed while another key was qualifying release.
        // Require current raw AND debounced states to be released together.
        if (events.held == 0U && pressed_mask == 0U) {
            release_required_ = false;
            return ButtonEvents{0U, 0U, 0U, true};
        }
        return ButtonEvents{0U, 0U, All, true};
    }
    return events;
}

} // namespace dso
