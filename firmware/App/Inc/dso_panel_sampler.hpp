#ifndef DSO_PANEL_SAMPLER_HPP
#define DSO_PANEL_SAMPLER_HPP

#include "dso_front_panel.hpp"

#include <cstdint>

namespace dso {

enum class PanelPollState : std::uint8_t { Pending, Ready, Fault };

struct PanelPollResult final {
    PanelPollState state;
    std::uint16_t value;
};

/* Each callback must return in bounded time. poll() never waits for ADC EOC.
 * start() selects one regular ADC2 channel without injected conversions.
 * abort() must be idempotent and must leave no old conversion for the next
 * start(). No callback changes the common ADC clock or waveform ADC sequence.
 */
struct PanelSamplerPort final {
    void* context;
    bool (*start)(void* context, std::uint8_t channel) noexcept;
    PanelPollResult (*poll)(void* context) noexcept;
    void (*abort)(void* context) noexcept;
    std::uint16_t (*read_gpio_b)(void* context) noexcept;
};

struct PanelSamplerConfig final {
    std::uint32_t conversion_timeout_ms;
    std::uint32_t maximum_scan_age_ms;
    std::uint32_t scan_interval_ms; // Minimum interval between scan STARTS.
};

enum class PanelSamplerState : std::uint8_t { Idle, Busy, Ready, Fault };
enum class PanelSamplerFault : std::uint8_t {
    None, InvalidPort, InvalidConfiguration, StartFailed, PollFailed,
    ConversionTimeout, ScanTooOld, EpochChanged, InvalidSample, ClockDiscontinuity
};

struct PanelSamplerUpdate final {
    PanelSamplerState state;
    PanelSamplerFault fault;
    bool observation_available;
    PanelObservation observation;
    std::uint32_t scan_epoch;
};

/* Allocation-free, foreground-only scan state machine. Construction is dormant;
 * start() enables it without touching hardware. Every update() invokes at most
 * ONE port callback. A healthy scan is: start1, poll1(0 or more Pending), start2,
 * poll2, start3, poll3, read_gpio_b. No next-channel start is hidden in a Ready
 * poll. Only a complete, current-epoch, non-expired scan publishes valid senses.
 *
 * Timeouts expire at elapsed >= their bound. All three configuration intervals
 * must be positive and <2^31 ms. Timestamp differences are modulo-2^32; an
 * observed interval >=2^31 is a clock discontinuity. The caller must satisfy the
 * same half-range/full-unobserved-wrap constraints as FrontPanel.
 *
 * Faults latch immediately and clear partial channels. If cleanup is needed,
 * abort() consumes one update's operation; a separate update reads GPIOB and publishes
 * an invalid observation with ZERO ADC fields. Subsequent fault updates provide
 * fresh GPIOB/invalid senses without retrying ADC. observation_available=false
 * means its GPIO field must not be consumed. A fault detected before a port
 * callback can abort on that same update; a poll/start failure cannot call abort
 * until a subsequent update. The fault field/state can still
 * invalidate acquisition immediately while cleanup/publication is pending.
 *
 * stop() aborts an outstanding conversion (at most one callback), disables the
 * sampler and discards partial state. It preserves a latched fault. reset()
 * additionally clears recoverable faults; a new explicit start() is required.
 * Controller-owned acquisitions must also be invalidated when stop/reset is
 * requested. No UI gesture, FrontPanel update, or capture rearm is performed.
 */
class PanelSampler final {
public:
    PanelSampler(PanelSamplerPort port, PanelSamplerConfig config) noexcept;
    PanelSampler(const PanelSampler&) = delete;
    PanelSampler& operator=(const PanelSampler&) = delete;
    PanelSampler(PanelSampler&&) = delete;
    PanelSampler& operator=(PanelSampler&&) = delete;

    [[nodiscard]] bool start() noexcept;
    void stop() noexcept;
    void reset() noexcept;
    [[nodiscard]] bool running() const noexcept { return enabled_; }
    [[nodiscard]] PanelSamplerUpdate update(std::uint32_t now_ms,
                                            std::uint32_t scan_epoch) noexcept;

private:
    enum class Phase : std::uint8_t { Idle, StartNext, Poll, ReadGpio,
                                      FaultAbort, FaultGpio };
    void discard_channels() noexcept;
    void fail(PanelSamplerFault reason) noexcept;
    [[nodiscard]] PanelSamplerUpdate result(PanelSamplerState state,
                                            std::uint32_t now_ms) const noexcept;
    const PanelSamplerPort port_;
    const PanelSamplerConfig config_;
    const PanelSamplerFault permanent_fault_;
    PanelSamplerFault fault_{PanelSamplerFault::None};
    Phase phase_{Phase::Idle};
    bool enabled_{false};
    bool needs_abort_{false};
    bool observed_time_{false};
    bool started_scan_{false};
    std::uint8_t channel_{1U};
    std::uint16_t channels_[3U]{};
    std::uint32_t last_update_{0U};
    std::uint32_t scan_started_{0U};
    std::uint32_t conversion_started_{0U};
    std::uint32_t epoch_{0U};
};

// Exclusive foreground input boundary. Owners outlive this object and all
// callbacks. Resume never starts a previously dormant sampler, clears a fault
// or reconstructs lost time. Call update only while !paused(); partial scans
// are discarded. Storage requires fresh FrontPanel qualification; preparation
// retains it under the normal polling-gap policy. Pauses must never overlap.
class PanelSuspension final {
public:
    PanelSuspension(PanelSampler& sampler, FrontPanel& panel) noexcept
        : sampler_{sampler}, panel_{panel} {}
    PanelSuspension(const PanelSuspension&) = delete;
    PanelSuspension& operator=(const PanelSuspension&) = delete;
    static void set_paused(void* context, bool paused) noexcept;
    // Brief synchronous acquisition preparation, with interrupts live. Abort
    // partial ADC2 scans without resetting physical/button qualification; normal
    // maximum-poll-gap checks still apply after resume. Never nest the owners.
    static void set_preparing(void* context, bool paused) noexcept;
    [[nodiscard]] bool paused() const noexcept { return paused_; }
private:
    void pause(bool paused, bool resynchronize) noexcept;
    PanelSampler& sampler_;
    FrontPanel& panel_;
    bool paused_{};
    bool resume_{};
};

} // namespace dso

#endif
