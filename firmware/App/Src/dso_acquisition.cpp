#include "dso_acquisition.hpp"
#include "dso_front_panel.hpp"
#include "dso_physical_settings.hpp"

#include <new>

namespace dso {
namespace {
void add_saturated(std::uint32_t& counter, std::uint32_t value = 1U) noexcept
{
    counter = UINT32_MAX - counter < value ? UINT32_MAX : counter + value;
}
bool timing_valid(const AcquisitionRequest& request) noexcept
{
    const auto& timing = request.timing;
    if (timing.timer_hz == 0U || timing.timer_hz > 72000000U ||
        timing.adc_hz < 600000U || timing.adc_hz > 14000000U ||
        timing.divider == 0U || timing.trigger_cycles_x2 < 34U ||
        request.dma.sample_period_cycles != timing.divider ||
        (std::uint64_t{timing.prescaler} + 1U) *
        (std::uint64_t{timing.period} + 1U) != timing.divider) { return false; }
    const auto sample = timing.trigger_cycles_x2 - 31U;
    if (sample != 3U && sample != 15U && sample != 27U && sample != 57U &&
        sample != 83U && sample != 111U && sample != 143U && sample != 479U) { return false; }
    return std::uint64_t{timing.timer_hz} * timing.trigger_cycles_x2 <=
        std::uint64_t{2U} * timing.adc_hz * timing.divider;
}
}

bool DmaAcquisition::valid_port() const noexcept
{
    return port_.prepare != nullptr && port_.arm_dma != nullptr &&
        port_.start_trigger != nullptr && port_.stop != nullptr &&
        port_.observe != nullptr && port_.acknowledge != nullptr;
}

bool DmaAcquisition::begin_spectrum_workspace(std::int32_t*& words) noexcept
{
    words = nullptr;
    if ((state_ != AcquisitionState::Stopped && state_ != AcquisitionState::Ready) ||
        workspace_owner_ != WorkspaceOwner::Dma || monitor_.state() == DmaRingState::Running ||
        monitor_.state() == DmaRingState::Copying) { return false; }
    workspace_.dma.~DmaBuffer();
    ::new (static_cast<void*>(&workspace_.spectrum)) SpectrumBuffer;
    workspace_owner_ = WorkspaceOwner::Spectrum;
    words = workspace_.spectrum.words;
    return true;
}

void DmaAcquisition::end_spectrum_workspace() noexcept
{
    if (workspace_owner_ != WorkspaceOwner::Spectrum) { return; }
    workspace_.spectrum.~SpectrumBuffer();
    ::new (static_cast<void*>(&workspace_.dma)) DmaBuffer;
    workspace_owner_ = WorkspaceOwner::Dma;
}

bool DmaAcquisition::begin_recall_staging() noexcept
{
    if (state_ != AcquisitionState::Stopped || workspace_owner_ != WorkspaceOwner::Dma) { return false; }
    workspace_owner_ = WorkspaceOwner::Recall;
    return true;
}

void DmaAcquisition::end_recall_staging() noexcept
{
    if (workspace_owner_ != WorkspaceOwner::Recall) { return; }
    workspace_owner_ = WorkspaceOwner::Dma;
}

bool DmaAcquisition::quiesce() noexcept
{
    // Only a validated start can enter Running; every caller is cleanup for
    // that epoch or its validated pre-arm path.
    const bool stopped = port_.stop(port_.context);
    monitor_.stop();
    return stopped;
}

void DmaAcquisition::fail(AcquisitionFault reason) noexcept
{
    capture_.gap();
    static_cast<void>(quiesce());
    fault_ = reason;
    add_saturated(statistics_.faults);
    if (reason == AcquisitionFault::Transport) { add_saturated(statistics_.transport_faults); }
    if (reason == AcquisitionFault::PhysicalChange) { add_saturated(statistics_.physical_change_faults); }
    state_ = AcquisitionState::Fault;
}

bool DmaAcquisition::start(const AcquisitionRequest& request) noexcept
{
    if (workspace_owner_ != WorkspaceOwner::Dma || state_ == AcquisitionState::Ready || state_ == AcquisitionState::Running) { return false; }
    if (!valid_port() || request.epoch == 0U || request.physical.generation == 0U ||
        request.physical.microvolts_per_div == 0U ||
        (request.physical.coupling != InputCoupling::Dc &&
         request.physical.coupling != InputCoupling::Ac &&
         request.physical.coupling != InputCoupling::Ground) ||
        !timing_valid(request)) {
        return false;
    }
    // Configuration validation must precede hardware side effects. The same
    // pure predicate is used by monitor::arm once a real DMA observation exists.
    if (!valid_dma_ring_config(request.dma)) { return false; }
    if (!capture_.arm(request.capture, 0U)) { return false; }
    state_ = AcquisitionState::Stopped;
    fault_ = AcquisitionFault::None;
    static_cast<void>(quiesce());
    if (!port_.prepare(port_.context, request.timing) ||
        !port_.arm_dma(port_.context, workspace_.dma.samples, DmaRingSamples)) {
        fail(AcquisitionFault::PortFailure); return false;
    }
    const auto initial = port_.observe(port_.context, true);
    if (!monitor_.arm(request.dma, initial, request.epoch)) {
        fail(AcquisitionFault::Transport); return false;
    }
    metadata_ = FrameMetadata{request.physical, request.timing, request.capture, request.epoch, 0U, false};
    tail_samples_ = 0U;
    // No port calls may intervene between this observation/arm and enabling
    // the trigger. The port must fit that interval inside safety_cycles.
    if (!port_.start_trigger(port_.context)) {
        fail(AcquisitionFault::PortFailure); return false;
    }
    const auto started = port_.observe(port_.context, false);
    if (!started.enabled || started.flags != 0U || started.remaining == 0U ||
        started.remaining > DmaRingSamples ||
        static_cast<std::uint32_t>(started.cycles - initial.cycles) >= request.dma.safety_cycles ||
        static_cast<std::uint32_t>(started.milliseconds - initial.milliseconds) > request.dma.max_poll_ms) {
        fail(AcquisitionFault::PortFailure); return false;
    }
    state_ = AcquisitionState::Running;
    add_saturated(statistics_.epochs_started);
    return true;
}

void DmaAcquisition::poll() noexcept
{
    if (state_ != AcquisitionState::Running) { return; }
    DmaLease lease{};
    const auto result = monitor_.begin(port_.observe(port_.context, true), lease);
    if (result == DmaPoll::Waiting) { return; }
    if (result == DmaPoll::Fault) { fail(AcquisitionFault::Transport); return; }
    port_.acknowledge(port_.context, lease.acknowledge_flag);
    const volatile std::uint16_t* source = workspace_.dma.samples + lease.offset;
    for (std::uint16_t index = 0U; index < DmaHalfSamples; ++index) {
        scratch_[index] = source[index];
    }
    if (!monitor_.commit(lease, port_.observe(port_.context, false))) {
        fail(AcquisitionFault::Transport); return;
    }
    add_saturated(statistics_.blocks_committed);
    const auto consumed = capture_.push(scratch_, DmaHalfSamples, lease.first_sequence);
    if (capture_.state() == CaptureState::Fault) { fail(AcquisitionFault::InvalidSamples); return; }
    if (capture_.state() == CaptureState::Ready) {
        metadata_.trigger_sequence = capture_.trigger_sequence();
        metadata_.edge_triggered = capture_.edge_triggered();
        tail_samples_ = static_cast<std::uint32_t>(DmaHalfSamples - consumed);
        if (!quiesce()) { fail(AcquisitionFault::PortFailure); return; }
        // A frozen record is publishable only after the producer confirms that
        // shutdown completed without the port's latched stop/readback error.
        add_saturated(statistics_.frames_ready);
        add_saturated(statistics_.discarded_tail_samples, tail_samples_);
        state_ = AcquisitionState::Ready;
    } else if (consumed != DmaHalfSamples) {
        fail(AcquisitionFault::Protocol);
    }
}

void DmaAcquisition::physical_update(const PanelUpdate& update) noexcept
{
    if (state_ != AcquisitionState::Running) { return; }
    if (!update.ready || update.invalidate_acquisition ||
        !same_physical_settings(update.settings, metadata_.physical)) {
        fail(AcquisitionFault::PhysicalChange);
    }
}

AcquisitionStopResult DmaAcquisition::stop() noexcept
{
    // Fault paths already perform bounded cleanup, and start() re-quiesces
    // before preparing a new epoch. Do not repeat port callbacks for a
    // non-running owner; Ready completion has separately verified its stop.
    if (state_ != AcquisitionState::Running) { return AcquisitionStopResult::Inactive; }
    if (!quiesce()) {
        fail(AcquisitionFault::PortFailure);
        return AcquisitionStopResult::Failed;
    }
    capture_.stop();
    state_ = AcquisitionState::Stopped;
    return AcquisitionStopResult::Stopped;
}

bool DmaAcquisition::release() noexcept
{
    if (state_ != AcquisitionState::Ready || workspace_owner_ != WorkspaceOwner::Dma) { return false; }
    capture_.stop();
    state_ = AcquisitionState::Stopped;
    return true;
}

bool DmaAcquisition::read(std::uint16_t index, std::uint16_t& value) const noexcept
{
    return state_ == AcquisitionState::Ready && capture_.read(index, &value);
}

const FrameMetadata* DmaAcquisition::metadata() const noexcept
{
    return state_ == AcquisitionState::Ready ? &metadata_ : nullptr;
}

} // namespace dso
