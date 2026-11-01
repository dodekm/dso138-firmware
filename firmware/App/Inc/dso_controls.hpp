#ifndef DSO_CONTROLS_HPP
#define DSO_CONTROLS_HPP

#include <array>
#include <cstdint>

namespace dso {

enum class SwitchPosition : std::uint8_t { Unknown, Low, Middle, High };

/* Board-characterized ADC bands; Low/High do not name physical positions. */
struct SenseBands final {
    std::uint16_t low_max;
    std::uint16_t middle_min;
    std::uint16_t middle_max;
    std::uint16_t high_min;
};

/*
 * Legal bands: low_max < middle_min <= middle_max < high_min <= 4095.
 * Unclassified guard-band values, ADC values >4095 and invalid bands return
 * Unknown. No default electrical thresholds are assumed for any board.
 */
[[nodiscard]] SwitchPosition decode_switch(std::uint16_t raw,
                                            const SenseBands &bands) noexcept;

/*
 * Foreground-owned, with no shared ISR state. Every decoded change, ambiguous
 * reading or polling discontinuity immediately invalidates a known position.
 * A new position requires consistent observations spanning settle_ms, with no
 * poll interval exceeding maxgap_ms. The first observation starts qualification.
 *
 * Timing settings must be positive and <2^31 ms. Intervals use modulo-2^32
 * timestamps; an interval >=2^31 is discontinuous. The caller must poll within
 * this half range: complete unobserved timestamp wraps cannot be detected.
 * Invalid settings or bands permanently return Unknown. Settings are immutable.
 */
class SwitchFilter final {
public:
    explicit SwitchFilter(SenseBands bands, std::uint32_t settle_ms = 20U,
                          std::uint32_t maxgap_ms = 50U) noexcept;

    [[nodiscard]] SwitchPosition update(std::uint16_t raw,
                                         std::uint32_t now_ms) noexcept;
    // Forget observations without changing the immutable electrical profile.
    void invalidate() noexcept { observed_ = false; stable_ = SwitchPosition::Unknown; }

private:
    const SenseBands bands_;
    const std::uint32_t settle_ms_;
    const std::uint32_t maxgap_ms_;
    const bool valid_;
    SwitchPosition stable_{SwitchPosition::Unknown};
    SwitchPosition candidate_{SwitchPosition::Unknown};
    std::uint32_t candidate_since_{0U};
    std::uint32_t last_poll_{0U};
    bool observed_{false};
};

struct ButtonEvents final {
    std::uint8_t pressed;
    std::uint8_t released;
    std::uint8_t held; /* Last debounced state, not the instantaneous input. */
    bool discontinuity;
};

/*
 * Four independent active-high logical inputs, after board GPIO polarity
 * normalization. Upper mask bits are ignored. No repeat or long-press policy.
 * A held button at startup generates a press only after fresh qualification.
 * Press/release events occur once when an individual candidate survives the
 * debounce interval. Other buttons do not restart that interval.
 *
 * Positive timings <2^31 ms and timestamp constraints match SwitchFilter.
 * A discontinuity resets all candidates, emits no edges on that poll and
 * preserves the last debounced held state. ButtonEvents::discontinuity is true
 * on initial observation, overdue/backward/half-range polls and invalid timings.
 * Gesture controllers must reset hold/repeat timing when that flag is true.
 * The next ordinary poll clears the flag; candidates still require debounce.
 * Invalid timing settings always return {0,0,0,true}.
 */
class DebouncedButtons final {
public:
    static constexpr std::uint8_t Ok{0x01U};
    static constexpr std::uint8_t Plus{0x02U};
    static constexpr std::uint8_t Minus{0x04U};
    static constexpr std::uint8_t Select{0x08U};
    static constexpr std::uint8_t All{0x0fU};

    explicit DebouncedButtons(std::uint32_t debounce_ms = 20U,
                              std::uint32_t maxgap_ms = 50U) noexcept;

    [[nodiscard]] ButtonEvents update(std::uint8_t pressed_mask,
                                       std::uint32_t now_ms) noexcept;
    // An externally known observation blackout may be invisible to now_ms.
    // Suppress all edges until ALL buttons have freshly debounced released.
    // Until then update returns {0,0,All,true}; the terminating update returns
    // {0,0,0,true}. Startup behavior is unchanged unless explicitly requested.
    void require_release() noexcept;

private:
    struct ButtonState final {
        bool candidate{false};
        bool stable{false};
        std::uint32_t candidate_since{0U};
    };

    [[nodiscard]] std::uint8_t held_mask() const noexcept;

    const std::uint32_t debounce_ms_;
    const std::uint32_t maxgap_ms_;
    const bool valid_;
    std::array<ButtonState, 4U> buttons_{};
    std::uint32_t last_poll_{0U};
    bool observed_{false};
    bool release_required_{false};
};

} // namespace dso

#endif
