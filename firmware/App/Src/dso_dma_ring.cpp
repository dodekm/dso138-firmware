#include "dso_dma_ring.hpp"

namespace dso {
namespace {
constexpr std::uint32_t HalfRange{0x80000000U};
static_assert(HalfRange % DmaHalfSamples == 0U,
              "DMA half-range threshold must divide evenly into sample periods");
}

bool valid_dma_ring_config(const DmaRingConfig& config) noexcept
{
    // Reject at the exact integer threshold first. Since DmaHalfSamples divides
    // HalfRange, the following 32-bit product is exactly equivalent to the
    // original widened product < HalfRange test and cannot overflow.
    if (config.sample_period_cycles == 0U ||
        config.sample_period_cycles >= HalfRange / DmaHalfSamples) { return false; }
    const auto half_cycles = config.sample_period_cycles * DmaHalfSamples;
    return
        config.safety_cycles >= config.sample_period_cycles &&
        config.safety_cycles < half_cycles && config.max_poll_cycles != 0U &&
        config.max_poll_cycles < half_cycles - config.safety_cycles &&
        config.max_poll_ms != 0U && config.max_poll_ms < HalfRange;
}

bool DmaRingMonitor::arm(DmaRingConfig config, const DmaObservation& initial,
                          std::uint32_t epoch) noexcept
{
    if (state_ == DmaRingState::Running || state_ == DmaRingState::Copying || epoch == 0U ||
        !valid_dma_ring_config(config) || !initial.enabled ||
        initial.remaining != DmaRingSamples || initial.flags != 0U) { return false; }
    config_ = config;
    half_cycles_ = config.sample_period_cycles * DmaHalfSamples;
    previous_ = initial;
    last_begin_cycles_ = initial.cycles;
    boundary_cycles_ = initial.cycles;
    epoch_ = epoch;
    sequence_ = 0U;
    expected_ = DmaHalf;
    fault_ = DmaRingFault::None;
    state_ = DmaRingState::Running;
    return true;
}

void DmaRingMonitor::stop() noexcept
{
    state_ = DmaRingState::Stopped;
}

void DmaRingMonitor::fail(DmaRingFault reason) noexcept
{
    if (state_ != DmaRingState::Fault) {
        fault_ = reason;
        state_ = DmaRingState::Fault;
    }
}

bool DmaRingMonitor::observe(const DmaObservation& value) noexcept
{
    if ((value.flags & static_cast<std::uint8_t>(~(DmaHalf | DmaComplete | DmaError))) != 0U) {
        fail(DmaRingFault::InvalidObservation); return false;
    }
    if ((value.flags & DmaError) != 0U) { fail(DmaRingFault::TransferError); return false; }
    if (!value.enabled) { fail(DmaRingFault::Disabled); return false; }
    if (value.remaining == 0U || value.remaining > DmaRingSamples) {
        fail(DmaRingFault::InvalidObservation); return false;
    }
    if ((value.flags & (DmaHalf | DmaComplete)) == (DmaHalf | DmaComplete)) {
        fail(DmaRingFault::CoalescedFlags); return false;
    }
    const auto elapsed = static_cast<std::uint32_t>(value.cycles - previous_.cycles);
    const auto elapsed_ms = static_cast<std::uint32_t>(value.milliseconds - previous_.milliseconds);
    if (elapsed > config_.max_poll_cycles || elapsed_ms > config_.max_poll_ms) {
        fail(DmaRingFault::PollDeadline); return false;
    }
    previous_ = value;
    return true;
}

bool DmaRingMonitor::inactive_half(std::uint16_t remaining) const noexcept
{
    return expected_ == DmaHalf ? remaining <= DmaHalfSamples : remaining > DmaHalfSamples;
}

DmaPoll DmaRingMonitor::begin(const DmaObservation& before, DmaLease& lease) noexcept
{
    if (state_ == DmaRingState::Fault) { return DmaPoll::Fault; }
    if (state_ != DmaRingState::Running) {
        fail(DmaRingFault::LeaseProtocol); return DmaPoll::Fault;
    }
    // Reuse the already-bracketed hardware observation; no additional clock or
    // peripheral read is introduced. Retain an interval that fails validation.
    const auto gap = static_cast<std::uint32_t>(before.cycles - last_begin_cycles_);
    if (gap > timing_.max_poll_gap_cycles) { timing_.max_poll_gap_cycles = gap; }
    last_begin_cycles_ = before.cycles;
    const auto prior_remaining = previous_.remaining;
    if (!observe(before)) { return DmaPoll::Fault; }
    if (before.flags == 0U) {
        // A stale/disabled timer cannot stay Running through repeated polls.
        const auto since_boundary = static_cast<std::uint32_t>(before.cycles - boundary_cycles_);
        if (before.remaining > prior_remaining) {
            fail(DmaRingFault::CounterRegression);
        } else if (since_boundary > half_cycles_ + config_.max_poll_cycles) {
            fail(DmaRingFault::ProgressDeadline);
        } else if (inactive_half(before.remaining)) {
            fail(DmaRingFault::UnexpectedFlag);
        }
        return state_ == DmaRingState::Fault ? DmaPoll::Fault : DmaPoll::Waiting;
    }
    if (before.flags != expected_) {
        fail(DmaRingFault::UnexpectedFlag); return DmaPoll::Fault;
    }
    if (!inactive_half(before.remaining)) {
        fail(DmaRingFault::WriterPhase); return DmaPoll::Fault;
    }
    const auto transfers = expected_ == DmaHalf ? before.remaining :
        static_cast<std::uint16_t>(before.remaining - DmaHalfSamples);
    const auto time_left = static_cast<std::uint32_t>(transfers) * config_.sample_period_cycles;
    if (time_left <= config_.safety_cycles) {
        fail(DmaRingFault::CopyDeadline); return DmaPoll::Fault;
    }
    ticket_++;
    if (ticket_ == 0U) { ticket_++; }
    lease_ = DmaLease{ticket_, epoch_, sequence_, time_left - config_.safety_cycles,
                     static_cast<std::uint16_t>(expected_ == DmaHalf ? 0U : DmaHalfSamples),
                     DmaHalfSamples, expected_};
    copy_start_ = before;
    state_ = DmaRingState::Copying;
    lease = lease_;
    return DmaPoll::Copy;
}

bool DmaRingMonitor::lease_matches(const DmaLease& value) const noexcept
{
    return value.ticket == lease_.ticket && value.epoch == lease_.epoch &&
        value.first_sequence == lease_.first_sequence && value.budget_cycles == lease_.budget_cycles &&
        value.offset == lease_.offset && value.count == lease_.count &&
        value.acknowledge_flag == lease_.acknowledge_flag;
}

bool DmaRingMonitor::commit(const DmaLease& lease, const DmaObservation& after) noexcept
{
    if (state_ == DmaRingState::Fault) { return false; }
    if (state_ != DmaRingState::Copying || !lease_matches(lease)) {
        fail(DmaRingFault::LeaseProtocol); return false;
    }
    const auto elapsed = static_cast<std::uint32_t>(after.cycles - copy_start_.cycles);
    if (elapsed > timing_.max_snapshot_cycles) { timing_.max_snapshot_cycles = elapsed; }
    if (!observe(after)) { return false; }
    if (after.flags != 0U || !inactive_half(after.remaining)) {
        fail(DmaRingFault::WriterPhase); return false;
    }
    if (after.remaining > copy_start_.remaining) {
        fail(DmaRingFault::CounterRegression); return false;
    }
    if (elapsed >= lease_.budget_cycles) {
        fail(DmaRingFault::CopyDeadline); return false;
    }
    sequence_ += DmaHalfSamples;
    boundary_cycles_ = after.cycles;
    expected_ = expected_ == DmaHalf ? DmaComplete : DmaHalf;
    state_ = DmaRingState::Running;
    return true;
}

} // namespace dso
