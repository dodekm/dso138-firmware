#ifndef DSO_DMA_RING_HPP
#define DSO_DMA_RING_HPP

#include <cstdint>

namespace dso {

inline constexpr std::uint16_t DmaRingSamples{1024U};
inline constexpr std::uint16_t DmaHalfSamples{DmaRingSamples / 2U};
enum DmaFlags : std::uint8_t {
    DmaHalf = 1U, DmaComplete = 2U, DmaError = 4U
};

struct DmaObservation final {
    std::uint32_t cycles; // Before register reads for begin; after for commit.
    std::uint32_t milliseconds; // Coarse second counter; not an independent wall clock.
    std::uint16_t remaining; // DMA CNDTR; zero is an ambiguous reload transient.
    std::uint8_t flags; // Sticky HT/TC, plus any IRQ-latched TE.
    bool enabled;
};

struct DmaRingConfig final {
    std::uint32_t sample_period_cycles;
    std::uint32_t safety_cycles; // Includes snapshot/ack uncertainty and margin.
    std::uint32_t max_poll_cycles; // < half period minus safety.
    std::uint32_t max_poll_ms;
};

// Pure request validation shared by acquisition preflight and monitor arm.
[[nodiscard]] bool valid_dma_ring_config(const DmaRingConfig& config) noexcept;

enum class DmaRingState { Stopped, Running, Copying, Fault };
enum class DmaRingFault {
    None, Disabled, InvalidObservation, TransferError, CoalescedFlags,
    UnexpectedFlag, PollDeadline, ProgressDeadline, WriterPhase,
    CopyDeadline, CounterRegression, LeaseProtocol
};
enum class DmaPoll { Waiting, Copy, Fault };

struct DmaTimingStatistics final {
    // Cumulative maxima since construction. arm() starts a fresh interval, so
    // stopped/reconfiguration time is excluded without erasing earlier peaks.
    // First gap includes the bounded pre-trigger arm/start interval. Gaps are
    // begin-to-begin (including snapshot and sample processing), not the
    // observe-to-observe deadline guarded separately below. The final poll to
    // stop interval has no subsequent begin and is not represented.
    std::uint32_t max_poll_gap_cycles{};
    std::uint32_t max_snapshot_cycles{};
};

struct DmaLease final {
    std::uint32_t ticket;
    std::uint32_t epoch;
    std::uint32_t first_sequence;
    std::uint32_t budget_cycles;
    std::uint16_t offset;
    std::uint16_t count;
    std::uint8_t acknowledge_flag;
};

// Single foreground owner; ISR must not access this object. Hardware HT/TC
// flags must have exactly one owner and remain sticky until the lease owner
// acknowledges ONLY its expected flag immediately before copying. IRQs may
// latch TE independently. Volatile-copy the leased half to private scratch,
// then commit using a fresh coherent observation BEFORE using any scratch data.
// Any failed check invalidates scratch and the acquisition epoch.
//
// This proves the software handoff under the clock/peripheral/polling contract,
// not the absence of upstream silent ADC overruns or valid analog accuracy.
// No debug halts, clock changes, sleep that stops CYCCNT, or other flag clearers
// are permitted during an epoch. Full unobserved clock wraps are outside the
// clock contract; uncleared coalesced flags still detect overwritten halves.
class DmaRingMonitor final {
public:
    DmaRingMonitor() = default;
    DmaRingMonitor(const DmaRingMonitor&) = delete;
    DmaRingMonitor& operator=(const DmaRingMonitor&) = delete;

    // Arm after DMA is enabled but BEFORE starting the sample timer. Start it
    // within safety_cycles. Require clean flags and CNDTR=1024. Invalid args
    // leave existing state untouched; arming while Running or Copying is
    // rejected. Stop the monitor after producer quiescence before a new epoch.
    [[nodiscard]] bool arm(DmaRingConfig config, const DmaObservation& initial,
                           std::uint32_t epoch) noexcept;
    void stop() noexcept;
    [[nodiscard]] DmaPoll begin(const DmaObservation& before, DmaLease& lease) noexcept;
    [[nodiscard]] bool commit(const DmaLease& lease,
                              const DmaObservation& after) noexcept;
    void fail(DmaRingFault reason) noexcept;
    [[nodiscard]] DmaRingState state() const noexcept { return state_; }
    [[nodiscard]] DmaRingFault fault() const noexcept { return fault_; }
    [[nodiscard]] std::uint32_t next_sequence() const noexcept { return sequence_; }
    // Foreground-only observations under the existing live cycle-clock
    // contract. No independent wall time, clock validity, WCET or CPU load proof.
    [[nodiscard]] const DmaTimingStatistics& timing_statistics() const noexcept { return timing_; }

private:
    [[nodiscard]] bool observe(const DmaObservation& value) noexcept;
    [[nodiscard]] bool lease_matches(const DmaLease& value) const noexcept;
    [[nodiscard]] bool inactive_half(std::uint16_t remaining) const noexcept;
    DmaRingConfig config_{};
    DmaRingState state_{DmaRingState::Stopped};
    DmaRingFault fault_{DmaRingFault::None};
    DmaObservation previous_{};
    DmaObservation copy_start_{};
    DmaLease lease_{};
    std::uint32_t half_cycles_{};
    std::uint32_t boundary_cycles_{};
    std::uint32_t sequence_{};
    std::uint32_t epoch_{};
    std::uint32_t ticket_{}; // Never reset by arm; bounded modular lifetime.
    DmaTimingStatistics timing_{};
    std::uint32_t last_begin_cycles_{};
    std::uint8_t expected_{DmaHalf};
};

} // namespace dso
#endif
