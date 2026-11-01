#ifndef DSO_ROLLING_HPP
#define DSO_ROLLING_HPP

#include "dso_acquisition.hpp"
#include "dso_trigger.hpp"
#include "dso_decimator.hpp"
#include <array>

namespace dso {

inline constexpr std::uint16_t RollingCapacity{512U};
inline constexpr std::uint16_t RollingDmaSamples{2U};
inline constexpr std::uint16_t RollingOversampledDmaSamples{16U};
inline constexpr std::uint16_t RollingOversampledHalfSamples{8U};
using RollingProcessing = SampleProcessingKind;

// Current 72MHz/12MHz/7.5-cycle profile, 512 samples across ten divisions.
// Catalogue indices18..23 only. Failure leaves the supplied plan unchanged.
[[nodiscard]] bool plan_rolling_timebase(std::uint8_t index, TimebasePlan& plan) noexcept;
[[nodiscard]] bool plan_rolling_raw_timebase(std::uint8_t index, TimebasePlan& plan) noexcept;

struct RollingRequest final {
    PhysicalSettings physical;
    TimebasePlan timing;
    std::uint32_t epoch;
    std::uint32_t maximum_poll_ms{20U};
    std::uint32_t timing_margin_ms{3U}; // Start, tick quantization, conversion/read uncertainty.
    std::uint32_t maximum_copy_ms{2U};
    CaptureConfig capture{RollingCapacity, RollingCapacity - 1U, 2048U, 8U,
                          TriggerMode::Auto, TriggerEdge::Rising, 0U};
    RollingProcessing processing{RollingProcessing::Point};
};

enum class RollingState : std::uint8_t { Stopped, Running, Ready, Fault };
enum class RollingFault : std::uint8_t {
    None, PortFailure, Disabled, InvalidObservation, TransferError,
    CoalescedFlags, UnexpectedFlag, WriterPhase, PollDeadline,
    ClockDiscontinuity, EarlySample, ProgressDeadline, CopyDeadline,
    InvalidSample, PhysicalChange, CounterExhausted
};
struct RollingStatistics final {
    std::uint32_t epochs_started{};
    std::uint32_t samples_committed{};
    std::uint32_t history_overwrites{};
    std::uint32_t faults{};
    std::uint32_t frames_ready{};
    std::uint32_t raw_samples_committed{};
};

// Value descriptor of one exact CPU-history window. No borrowed DMA pointer.
// revision is the total committed samples in this epoch, not a callback count.
// first_sample is a64-bit ordinal; first_sequence is its modulo32-bit label.
struct RollingSnapshot final {
    PhysicalSettings physical{};
    TimebasePlan timing{};
    CaptureConfig capture{};
    std::uint64_t session{};
    std::uint64_t revision{};
    std::uint64_t first_sample{};
    std::uint64_t observed_elapsed_ms{};
    std::uint32_t epoch{};
    std::uint32_t first_sequence{};
    std::uint32_t trigger_sequence{};
    std::uint16_t count{};
    bool edge_triggered{};
    RollingProcessing processing{};
    bool source_clipped{};
    bool output_clamped{};
    TimebasePlan raw_timing{};
    std::uint16_t filter_delay_raw_samples{};
    std::uint16_t first_output_raw_samples{};
};

// One foreground owner. AcquisitionPort arms two point cells or sixteen raw
// cells for Filtered25, with coherent HT/TC/CNDTR evidence. The cycles field is unused: long sample
// intervals are tracked by a bounded rational phase plus wrap-safe milliseconds.
// CPU history is independent of DMA. At most one half (one/eight cells) is
// quarantined per poll; only a fully validated block enters the filter/history.
// No IRQ/callback-count publication is used.
//
// Auto continuously rolls without triggering. Normal/Single share the same
// 512-word history and freeze an exact pre/posttrigger record in Ready. Explicit
// application release/rearm is required. Hardware/sample timing remains unproven.
class RollingAcquisition final {
public:
    explicit RollingAcquisition(AcquisitionPort port) noexcept : port_{port} {}
    RollingAcquisition(const RollingAcquisition&) = delete;
    RollingAcquisition& operator=(const RollingAcquisition&) = delete;
    RollingAcquisition(RollingAcquisition&&) = delete;
    RollingAcquisition& operator=(RollingAcquisition&&) = delete;
    [[nodiscard]] bool start(const RollingRequest& request) noexcept;
    void poll() noexcept;
    void physical_update(const PanelUpdate& update) noexcept;
    // Inactive means no Running epoch was stopped; Stopped/Failed describe a
    // Running epoch's shutdown result. Holds Auto history/Ready as before.
    AcquisitionStopResult stop() noexcept;
    [[nodiscard]] bool release() noexcept; // Ready only; invalidates its descriptors.
    [[nodiscard]] bool snapshot(RollingSnapshot& out) const noexcept;
    // Failure leaves out unchanged. A caller may copy a descriptor incrementally
    // while poll appends; overwritten source ordinals reject rather than alias.
    [[nodiscard]] bool read(const RollingSnapshot& snapshot, std::uint16_t index,
                            std::uint16_t& out) const noexcept;
    [[nodiscard]] RollingState state() const noexcept { return state_; }
    [[nodiscard]] RollingFault fault() const noexcept { return fault_; }
    [[nodiscard]] std::uint64_t revision() const noexcept { return next_sample_; }
    // Foreground-only, one-consumer sticky indication. Arm/stop/release leave it pending.
    [[nodiscard]] bool take_accepted_edge() noexcept
    { const auto pending = accepted_edge_pending_; accepted_edge_pending_ = false; return pending; }
    [[nodiscard]] const RollingStatistics& statistics() const noexcept { return statistics_; }

private:
    [[nodiscard]] bool valid_port() const noexcept;
    [[nodiscard]] bool quiesce() noexcept;
#if defined(__GNUC__)
    // Rare terminal path layout hint only; not a runtime/WCET claim.
    void fail(RollingFault fault) noexcept __attribute__((cold));
#else
    void fail(RollingFault fault) noexcept;
#endif
    [[nodiscard]] bool update_clock(const DmaObservation& observation, std::uint32_t bound_ms,
                                    RollingFault overdue) noexcept;
    [[nodiscard]] bool observation_valid(const DmaObservation& observation) noexcept;
    [[nodiscard]] bool append(std::uint16_t sample, bool source_clipped = false, bool clamped = false) noexcept;
    [[nodiscard]] bool writer_phase(std::uint16_t remaining, std::uint8_t completed) const noexcept;
    const AcquisitionPort port_;
    RollingRequest request_{};
    RollingStatistics statistics_{};
    std::array<std::uint16_t, RollingCapacity> history_{};
    alignas(4) std::uint16_t dma_[RollingOversampledDmaSamples]{};
    std::array<std::uint16_t, RollingOversampledHalfSamples> scratch_{};
    SlowDecimator decimator_{};
    std::array<std::uint32_t, RollingCapacity / 32U> clipped_bits_{};
    std::array<std::uint32_t, RollingCapacity / 32U> clamped_bits_{};
    TimebasePlan raw_timing_{};
    ThresholdCrossing crossing_{};
    std::uint64_t session_{};
    std::uint64_t next_sample_{};
    std::uint64_t trigger_sample_{};
    std::uint64_t elapsed_ms_{};
    std::int64_t phase_units_{}; // ms*TIMCLK minus accepted*divider*1000; bounded each sample.
    std::uint32_t previous_ms_{};
    std::uint16_t count_{};
    std::uint16_t remaining_{};
    std::uint16_t clipped_count_{}, clamped_count_{};
    std::uint16_t dma_count_{RollingDmaSamples};
    std::uint8_t expected_{DmaHalf};
    RollingState state_{RollingState::Stopped};
    RollingFault fault_{RollingFault::None};
    bool triggered_{};
    bool accepted_edge_pending_{};
};

} // namespace dso
#endif
