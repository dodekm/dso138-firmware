#ifndef DSO_FRONT_PANEL_HPP
#define DSO_FRONT_PANEL_HPP

#include "dso_controls.hpp"

#include <cstdint>

namespace dso {

enum class InputCoupling : std::uint8_t { Unknown, Dc, Ac, Ground };

struct FrontPanelConfig final {
    SenseBands sen1_bands;
    SenseBands sen2_bands;
    SenseBands coupling_bands;
    std::uint32_t switch_qualification_ms;
    std::uint32_t maximum_poll_gap_ms;
    std::uint32_t analog_settling_ms;
    std::uint32_t button_debounce_ms;
};

/* One completed foreground scan, not a sequence of independently published
 * channel readings. Channel 1 = SEN2, channel 2 = SEN1, channel 3 = coupling.
 * The board adapter must bound scan duration/age and set senses_valid=false on
 * ADC failure, stale/missing channels or a scan spanning an acquisition change.
 * gpio_b_idr is one GPIOB snapshot. now_ms is scan completion time. Physical
 * movement before/between observations cannot be ruled out by this interface.
 */
struct PanelObservation final {
    std::uint16_t sen2_adc;
    std::uint16_t sen1_adc;
    std::uint16_t coupling_adc;
    std::uint16_t gpio_b_idr;
    std::uint32_t now_ms;
    bool senses_valid;
};

/* Metadata is copied by value into a frozen acquisition. The scale is the
 * nominal front-panel label, not a calibrated ADC-code conversion factor.
 * generation=0, scale=0 and Unknown coupling mean the physical state is invalid.
 */
struct PhysicalSettings final {
    std::uint32_t generation;
    std::uint32_t microvolts_per_div;
    InputCoupling coupling;
};

struct PanelUpdate final {
    PhysicalSettings settings;
    ButtonEvents buttons;
    bool ready;
    bool invalidate_acquisition;
    bool configuration_changed;
};

/* Nonzero generation sequence: 0 -> 1 and UINT32_MAX -> 1. Equality identifies
 * one bounded observation epoch; ordering comparisons are not meaningful.
 * A consumer cannot retain an identity across a complete sequence wrap.
 */
[[nodiscard]] constexpr std::uint32_t next_panel_generation(
    std::uint32_t previous) noexcept
{
    return previous == UINT32_MAX ? 1U : previous + 1U;
}

/* PB15=OK, PB14=Plus, PB13=Minus, PB12=Select; all active-low.
 * PB0-PB11 are ignored and never become logical button bits.
 */
[[nodiscard]] std::uint8_t normalize_panel_buttons(std::uint16_t gpio_b_idr) noexcept;

/* Foreground-only, allocation-free owner of all qualification state. Board
 * bands/timings must be explicit. Switch/debounce/max-gap timings must be
 * positive and <2^31 ms; analog settling may be zero but must be <2^31 ms.
 * Polling constraints match SwitchFilter, including unobserved full-wrap limits.
 *
 * Any unknown/differing switch or overdue/backward poll immediately makes
 * ready=false. After all three positions qualify, a separate analog settling
 * interval must pass with valid observations. Requalification, including return
 * to the same position after noise, creates a new generation. Invalid outputs
 * never expose the old range/coupling as current valid settings.
 *
 * invalidate_acquisition is level-sensitive while invalid, and true on the
 * first acceptance of every generation. configuration_changed is a one-update
 * acceptance event, even when the new nominal values equal the previous ones.
 * The application cancels partial acquisitions before using accepted settings;
 * frozen records retain their copied old settings. Neither this model nor its
 * button events automatically rearm Single or change acquisition parameters.
 */
class FrontPanel final {
public:
    explicit FrontPanel(const FrontPanelConfig& config) noexcept;
    FrontPanel(const FrontPanel&) = delete;
    FrontPanel& operator=(const FrontPanel&) = delete;
    FrontPanel(FrontPanel&&) = delete;
    FrontPanel& operator=(FrontPanel&&) = delete;

    [[nodiscard]] PanelUpdate update(const PanelObservation& observation) noexcept;
    // Explicit blackout boundary, independent of elapsed timestamp evidence.
    // Preserve generation sequence; require fresh switches, settling and an
    // all-buttons release before permitting gestures. Performs no port I/O.
    void resynchronize() noexcept;

private:
    SwitchFilter sen1_;
    SwitchFilter sen2_;
    SwitchFilter coupling_;
    DebouncedButtons buttons_;
    const std::uint32_t analog_settling_ms_;
    const bool valid_settling_;
    bool ready_{false};
    bool settling_{false};
    std::uint32_t settling_since_{0U};
    std::uint32_t generation_{0U};
    PhysicalSettings current_{0U, 0U, InputCoupling::Unknown};
};

} // namespace dso

#endif
