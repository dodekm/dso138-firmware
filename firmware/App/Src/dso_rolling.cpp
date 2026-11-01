#include "dso_rolling.hpp"
#include "dso_scaling.hpp"
#include "dso_timebase_presets.hpp"

namespace dso {
namespace {
void increment(std::uint32_t& value) noexcept { if (value != UINT32_MAX) { ++value; } }

bool physical_valid(const PhysicalSettings& physical) noexcept
{
    const auto range = physical.microvolts_per_div;
    return physical.generation != 0U &&
        (physical.coupling == InputCoupling::Dc || physical.coupling == InputCoupling::Ac || physical.coupling == InputCoupling::Ground) &&
        (range == 10000U || range == 20000U || range == 50000U || range == 100000U || range == 200000U ||
         range == 500000U || range == 1000000U || range == 2000000U || range == 5000000U);
}

bool timing_valid(const TimebasePlan& plan) noexcept
{
    constexpr std::uint32_t BaseDivider{14062500U};
    const auto divider = plan.divider;
    return plan.timer_hz == 72000000U && plan.adc_hz == 12000000U && plan.trigger_cycles_x2 == 46U &&
        (std::uint64_t{plan.prescaler} + 1U) * (std::uint64_t{plan.period} + 1U) == divider &&
        (divider == BaseDivider || divider == BaseDivider * 2U || divider == BaseDivider * 5U ||
         divider == BaseDivider * 10U || divider == BaseDivider * 20U || divider == BaseDivider * 50U);
}

bool request_valid(const RollingRequest& request) noexcept
{
    const auto& capture = request.capture;
    const bool filtered = request.processing == RollingProcessing::Filtered25;
    const auto half_period = std::uint64_t{request.timing.divider} * (filtered ? RollingOversampledHalfSamples : 1U) /
        (filtered ? SlowDecimator::Factor : 1U);
    return request.epoch != 0U && physical_valid(request.physical) && timing_valid(request.timing) &&
        (filtered || request.processing == RollingProcessing::Point) &&
        request.maximum_poll_ms > 0U && request.maximum_poll_ms <= 50U &&
        request.timing_margin_ms >= 2U && request.timing_margin_ms <= 10U &&
        request.maximum_copy_ms > 0U && request.maximum_copy_ms < request.timing_margin_ms &&
        std::uint64_t{request.maximum_poll_ms + request.timing_margin_ms + request.maximum_copy_ms + 2U} * request.timing.timer_hz <
            half_period * 1000U &&
        capture.length == RollingCapacity && capture.pretrigger < RollingCapacity && capture.level <= AdcMax &&
        capture.hysteresis <= capture.level && capture.hysteresis <= AdcMax - capture.level &&
        (capture.mode == TriggerMode::Auto || capture.mode == TriggerMode::Normal || capture.mode == TriggerMode::Single) &&
        (capture.edge == TriggerEdge::Rising || capture.edge == TriggerEdge::Falling);
}

bool exact_plan(std::uint32_t divider, TimebasePlan& plan) noexcept
{
    for (std::uint32_t p{1U}; p <= 65536U; ++p) {
        if (divider % p != 0U || divider / p > 65536U) { continue; }
        plan = {static_cast<std::uint16_t>(p - 1U), static_cast<std::uint16_t>(divider / p - 1U),
                72000000U, divider, 12000000U, 46U};
        return true;
    }
    return false;
}
}

bool plan_rolling_timebase(std::uint8_t index, TimebasePlan& plan) noexcept
{
    static_assert(ScopeTimebaseCount == 24U && RollingCapacity == 512U &&
                  sizeof(detail::RollingPresets)/sizeof(detail::RollingPresets[0]) == 6U,
                  "Timer presets require the current fixed rolling profile");
    if (index < 18U || index >= ScopeTimebaseCount) { return false; }
    const auto pair = detail::RollingPresets[index-18U];
    const auto divider = (std::uint32_t{pair.prescaler}+1U) * (std::uint32_t{pair.period}+1U);
    const TimebasePlan candidate{pair.prescaler,pair.period,72000000U,divider,12000000U,46U};
    if (!timing_valid(candidate)) { return false; }
    plan = candidate;
    return true;
}

bool plan_rolling_raw_timebase(std::uint8_t index, TimebasePlan& plan) noexcept
{
    TimebasePlan output{};
    return plan_rolling_timebase(index, output) && exact_plan(output.divider / SlowDecimator::Factor, plan);
}

bool RollingAcquisition::valid_port() const noexcept
{
    return port_.prepare != nullptr && port_.arm_dma != nullptr && port_.start_trigger != nullptr &&
        port_.stop != nullptr && port_.observe != nullptr && port_.acknowledge != nullptr;
}

bool RollingAcquisition::quiesce() noexcept
{
    // Only a validated start can enter Running; every caller is cleanup for
    // that epoch or its validated pre-arm path.
    return port_.stop(port_.context);
}

void RollingAcquisition::fail(RollingFault fault) noexcept
{
    static_cast<void>(quiesce());
    count_ = 0U;
    fault_ = fault;
    state_ = RollingState::Fault;
    increment(statistics_.faults);
}

bool RollingAcquisition::start(const RollingRequest& request) noexcept
{
    if (state_ == RollingState::Running || state_ == RollingState::Ready || !valid_port() || !request_valid(request)) { return false; }
    if (session_ == UINT64_MAX) {
        if (state_ != RollingState::Fault) { fail(RollingFault::CounterExhausted); }
        return false;
    }
    static_cast<void>(quiesce());
    request_ = request;
    ++session_;
    next_sample_ = 0U; trigger_sample_ = 0U; elapsed_ms_ = 0U; phase_units_ = 0;
    count_ = 0U; remaining_ = 0U; expected_ = DmaHalf; triggered_ = false;
    clipped_count_ = 0U; clamped_count_ = 0U;
    clipped_bits_.fill(0U); clamped_bits_.fill(0U);
    decimator_.reset();
    const bool filtered = request.processing == RollingProcessing::Filtered25;
    dma_count_ = filtered ? RollingOversampledDmaSamples : RollingDmaSamples;
    raw_timing_ = request.timing;
    if (filtered && !exact_plan(request.timing.divider / SlowDecimator::Factor, raw_timing_)) { return false; }
    crossing_.reset();
    state_ = RollingState::Stopped;
    fault_ = RollingFault::None;
    if (!port_.prepare(port_.context, raw_timing_) ||
        !port_.arm_dma(port_.context, dma_, dma_count_)) { fail(RollingFault::PortFailure); return false; }
    const auto initial = port_.observe(port_.context, true);
    if (!initial.enabled || initial.flags != 0U || initial.remaining != dma_count_) { fail(RollingFault::PortFailure); return false; }
    if (!port_.start_trigger(port_.context)) { fail(RollingFault::PortFailure); return false; }
    const auto started = port_.observe(port_.context, false);
    // Anchor to the observation AFTER start. Reserving one margin millisecond
    // covers tick quantization; conversion latency must fit the same envelope.
    if (!started.enabled || started.flags != 0U || started.remaining != dma_count_ ||
        static_cast<std::uint32_t>(started.milliseconds - initial.milliseconds) >= request.timing_margin_ms) {
        fail(RollingFault::PortFailure); return false;
    }
    previous_ms_ = started.milliseconds;
    state_ = RollingState::Running;
    increment(statistics_.epochs_started);
    return true;
}

bool RollingAcquisition::update_clock(const DmaObservation& observation, std::uint32_t bound_ms, RollingFault overdue) noexcept
{
    const auto delta = static_cast<std::uint32_t>(observation.milliseconds - previous_ms_);
    if (delta >= 0x80000000U) { fail(RollingFault::ClockDiscontinuity); return false; }
    if (delta > bound_ms) { fail(overdue); return false; }
    if (elapsed_ms_ > UINT64_MAX - delta) { fail(RollingFault::CounterExhausted); return false; }
    elapsed_ms_ += delta;
    previous_ms_ = observation.milliseconds;
    // delta<=50ms and phase is rebased at every committed sample. No product
    // of unbounded epoch elapsed time and timer frequency is ever formed.
    phase_units_ += static_cast<std::int64_t>(std::uint64_t{delta} * request_.timing.timer_hz);
    return true;
}

bool RollingAcquisition::observation_valid(const DmaObservation& observation) noexcept
{
    if (!observation.enabled) { fail(RollingFault::Disabled); return false; }
    if (observation.remaining == 0U || observation.remaining > dma_count_ || (observation.flags & 0xf8U) != 0U) {
        fail(RollingFault::InvalidObservation); return false;
    }
    if ((observation.flags & DmaError) != 0U) { fail(RollingFault::TransferError); return false; }
    if ((observation.flags & (DmaHalf | DmaComplete)) == (DmaHalf | DmaComplete)) { fail(RollingFault::CoalescedFlags); return false; }
    return true;
}

bool RollingAcquisition::append(std::uint16_t sample, bool clipped, bool clamped) noexcept
{
    const auto before = count_;
    bool complete{};
    const auto slot = static_cast<std::size_t>(next_sample_ % RollingCapacity);
    const auto bit = std::uint32_t{1U} << (slot % 32U);
    auto& source_bits = clipped_bits_[slot / 32U];
    auto& clamp_bits = clamped_bits_[slot / 32U];
    if ((source_bits & bit) != 0U) { --clipped_count_; }
    if ((clamp_bits & bit) != 0U) { --clamped_count_; }
    source_bits = (source_bits & ~bit) | (clipped ? bit : 0U);
    clamp_bits = (clamp_bits & ~bit) | (clamped ? bit : 0U);
    if (clipped) { ++clipped_count_; }
    if (clamped) { ++clamped_count_; }
    history_[slot] = sample;
    if (count_ != RollingCapacity) { ++count_; } else { increment(statistics_.history_overwrites); }
    if (request_.capture.mode != TriggerMode::Auto) {
        if (triggered_) {
            --remaining_;
            if (remaining_ == 0U) { complete = true; }
        } else {
            const auto& capture = request_.capture;
            const auto hit = crossing_.update(sample, capture.level, capture.hysteresis, capture.edge);
            if (hit && before >= capture.pretrigger) {
                accepted_edge_pending_ = true;
                triggered_ = true;
                trigger_sample_ = next_sample_;
                remaining_ = static_cast<std::uint16_t>(RollingCapacity - capture.pretrigger - 1U);
                if (remaining_ == 0U) { complete = true; }
            }
        }
    }
    ++next_sample_;
    increment(statistics_.samples_committed);
    return complete;
}

bool RollingAcquisition::writer_phase(std::uint16_t remaining, std::uint8_t completed) const noexcept
{
    const auto half = dma_count_ / 2U;
    return completed == DmaHalf ? remaining > 0U && remaining <= half : remaining > half && remaining <= dma_count_;
}

void RollingAcquisition::poll() noexcept
{
    if (state_ != RollingState::Running) { return; }
    const auto before = port_.observe(port_.context, true);
    if (!update_clock(before, request_.maximum_poll_ms, RollingFault::PollDeadline) || !observation_valid(before)) { return; }
    const auto half = static_cast<std::uint16_t>(dma_count_ / 2U);
    const auto period = static_cast<std::int64_t>(std::uint64_t{raw_timing_.divider} * half * 1000U);
    const auto margin = static_cast<std::int64_t>(std::uint64_t{request_.timing_margin_ms} * request_.timing.timer_hz);
    if (before.flags == 0U) {
        if (!writer_phase(before.remaining, expected_ == DmaHalf ? DmaComplete : DmaHalf)) { fail(RollingFault::WriterPhase); }
        else if (phase_units_ > period + margin) { fail(RollingFault::ProgressDeadline); }
        return;
    }
    if (before.flags != expected_) { fail(RollingFault::UnexpectedFlag); return; }
    if (!writer_phase(before.remaining, expected_)) { fail(RollingFault::WriterPhase); return; }
    if (phase_units_ < period - margin) { fail(RollingFault::EarlySample); return; }
    if (phase_units_ > period + margin + static_cast<std::int64_t>(std::uint64_t{request_.maximum_poll_ms} * request_.timing.timer_hz)) {
        fail(RollingFault::ProgressDeadline); return;
    }
    port_.acknowledge(port_.context, expected_);
    const volatile std::uint16_t* source = dma_;
    const auto offset = expected_ == DmaHalf ? 0U : half;
    for (std::uint16_t i{}; i < half; ++i) { scratch_[i] = source[offset + i]; }
    const auto after = port_.observe(port_.context, false);
    if (!update_clock(after, request_.maximum_copy_ms, RollingFault::CopyDeadline) || !observation_valid(after)) { return; }
    if (after.flags != 0U || !writer_phase(after.remaining, expected_) || after.remaining > before.remaining || phase_units_ > period + margin +
        static_cast<std::int64_t>(std::uint64_t{request_.maximum_poll_ms + request_.maximum_copy_ms} * request_.timing.timer_hz)) {
        fail(RollingFault::CopyDeadline); return;
    }
    // Quarantine the whole block. Neither history nor filter state changes
    // until both observations and EVERY code have passed validation.
    for (std::uint16_t i{}; i < half; ++i) {
        if (scratch_[i] > AdcMax) { fail(RollingFault::InvalidSample); return; }
    }
    phase_units_ -= period;
    expected_ = expected_ == DmaHalf ? DmaComplete : DmaHalf;
    bool complete{};
    for (std::uint16_t i{}; i < half && state_ == RollingState::Running && !complete; ++i) {
        increment(statistics_.raw_samples_committed);
        auto value = scratch_[i];
        bool clipped{}, clamped{};
        if (request_.processing == RollingProcessing::Filtered25) {
            const auto filtered = decimator_.push(value);
            if (filtered.state == DecimationState::Invalid) { fail(RollingFault::InvalidSample); return; }
            if (filtered.state != DecimationState::Output) { continue; }
            value = filtered.value; clipped = filtered.raw_clipped; clamped = filtered.output_clamped;
        }
        if (next_sample_ == UINT64_MAX) { fail(RollingFault::CounterExhausted); return; }
        complete = append(value, clipped, clamped);
    }
    if (complete) {
        if (!quiesce()) { fail(RollingFault::PortFailure); return; }
        state_ = RollingState::Ready;
        increment(statistics_.frames_ready);
    }
}

void RollingAcquisition::physical_update(const PanelUpdate& update) noexcept
{
    if (state_ != RollingState::Running) { return; }
    if (!update.ready || update.invalidate_acquisition ||
        update.settings.generation != request_.physical.generation ||
        update.settings.microvolts_per_div != request_.physical.microvolts_per_div ||
        update.settings.coupling != request_.physical.coupling) { fail(RollingFault::PhysicalChange); }
}

AcquisitionStopResult RollingAcquisition::stop() noexcept
{
    if (state_ != RollingState::Running) { return AcquisitionStopResult::Inactive; }
    if (!quiesce()) { fail(RollingFault::PortFailure); return AcquisitionStopResult::Failed; }
    if (request_.capture.mode != TriggerMode::Auto) { count_ = 0U; }
    state_ = RollingState::Stopped;
    return AcquisitionStopResult::Stopped;
}

bool RollingAcquisition::release() noexcept
{
    if (state_ != RollingState::Ready) { return false; }
    count_ = 0U;
    state_ = RollingState::Stopped;
    return true;
}

bool RollingAcquisition::snapshot(RollingSnapshot& out) const noexcept
{
    if (count_ == 0U || state_ == RollingState::Fault ||
        (request_.capture.mode != TriggerMode::Auto && state_ != RollingState::Ready)) { return false; }
    auto capture = request_.capture;
    if (capture.mode == TriggerMode::Auto) {
        capture.length = count_;
        capture.pretrigger = static_cast<std::uint16_t>(count_ - 1U);
    }
    out = RollingSnapshot{request_.physical, request_.timing, capture, session_, next_sample_, next_sample_ - count_,
        elapsed_ms_, request_.epoch, static_cast<std::uint32_t>(next_sample_ - count_),
        static_cast<std::uint32_t>(triggered_ ? trigger_sample_ : next_sample_ - 1U), count_, triggered_, request_.processing,
        clipped_count_ != 0U, clamped_count_ != 0U, raw_timing_,
        static_cast<std::uint16_t>(request_.processing == RollingProcessing::Filtered25 ? SlowDecimator::GroupDelayRawSamples : 0U),
        static_cast<std::uint16_t>(request_.processing == RollingProcessing::Filtered25 ? SlowDecimator::FirstOutputRawSamples : 1U)};
    return true;
}

bool RollingAcquisition::read(const RollingSnapshot& snapshot, std::uint16_t index, std::uint16_t& out) const noexcept
{
    if (state_ == RollingState::Fault || count_ == 0U || snapshot.session != session_ || snapshot.epoch != request_.epoch ||
        snapshot.count == 0U || snapshot.count > RollingCapacity || index >= snapshot.count || snapshot.revision > next_sample_ ||
        snapshot.revision < snapshot.count || snapshot.first_sample != snapshot.revision - snapshot.count ||
        snapshot.first_sequence != static_cast<std::uint32_t>(snapshot.first_sample) ||
        (request_.capture.mode != TriggerMode::Auto && state_ != RollingState::Ready)) { return false; }
    const auto ordinal = snapshot.first_sample + index;
    if (ordinal >= next_sample_ || next_sample_ - ordinal > count_) { return false; }
    out = history_[static_cast<std::size_t>(ordinal % RollingCapacity)];
    return true;
}

static_assert(sizeof(RollingAcquisition) < 1920U, "Retain slow-mode SRAM headroom");
} // namespace dso
