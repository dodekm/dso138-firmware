#include "dso_ui_controller.hpp"
#include "dso_adc_codes.hpp"
#include "dso_acquisition.hpp"
#include "dso_front_panel.hpp"
#include "dso_scope_screen.hpp"
#include "dso_scope_storage.hpp"
#include "dso_physical_settings.hpp"
#include "dso_settings_contract.hpp"

namespace dso {

const ScreenSnapshot& ScopeController::scene() const noexcept
{
    return screen_.snapshot();
}

ScopeController::ScopeController(DmaAcquisition& acquisition, ScopeScreen& screen, UiControllerConfig config) noexcept
    : acquisition_{acquisition}, screen_{screen}, config_{config}, ui_{ScopeTimebaseCount, config.startup_run}
{
    screen_.reset_composed_view();
    if (config_.storage != nullptr && config_.storage->state() != ScopeStorageState::Disabled) {
        storage_.status = UiStorageStatus::Idle;
    }
    if (config_.calibration_bank != nullptr && config_.calibration_bank->board_identity() != 0U) {
        calibration_status_ = UiCalibrationStatus::Ready;
    }
    reconcile_acquisition_readiness();
}

void ScopeController::reconcile_acquisition_readiness() noexcept
{
    if (planned_index_ != ui_.settings().timebase_index) {
        config_.acquisition_input.set(true);
        planned_index_ = ui_.settings().timebase_index;
        if (!plan_scope_timebase(planned_index_, plan_)) { plan_.support = ScopeTimebaseSupport::Invalid; }
        config_.acquisition_input.set(false);
    }
    const bool boot_storage = storage_.status != UiStorageStatus::Disabled &&
        (storage_.boot_pending || storage_.boot_active || storage_.boot_waiting_input);
    ui_.set_capture_inhibited(boot_storage || !config_.acquisition_enabled ||
        plan_.support != ScopeTimebaseSupport::CircularDma);
}

void ScopeController::panel_update(const PanelUpdate& update, std::uint32_t now_ms) noexcept
{
    // No pre-pause observation may republish a physical identity during I/O.
    if (storage_busy() && config_.storage_input.pause != nullptr) { return; }
    const auto& before = ui_.physical();
    if (update.invalidate_acquisition || !update.ready || before.generation != update.settings.generation ||
        before.microvolts_per_div != update.settings.microvolts_per_div || before.coupling != update.settings.coupling) {
        invalidate_export();
        supersede_host_job();
    }
    const auto prior_request = ui_.request_id();
    calibration_session_.physical_update(update);
    acquisition_.physical_update(update);
    ui_.physical_update(update);
    if (host_.request != 0U && ui_.request_id() != prior_request) { cancel_host_job(HostCaptureState::Cancelled); }
    buttons(update.buttons, now_ms);
    if (storage_.boot_waiting_input && update.ready && update.buttons.held == 0U &&
        !update.buttons.discontinuity) {
        storage_.boot_waiting_input = false;
        reconcile_acquisition_readiness();
    }
}

void ScopeController::buttons(const ButtonEvents& event, std::uint32_t now_ms) noexcept
{
    host_.local_buttons_held = event.held;
    if (storage_busy()) {
        ui_.buttons({0U, 0U, event.held, true}, now_ms);
        return;
    }
    if (event.pressed != 0U) { restore_host_settings(); }
    const auto prior_settings = ui_.settings();
    const auto prior_request = ui_.request_id();
    ui_.buttons(event, now_ms);
    if (event.pressed != 0U || event.discontinuity || !(prior_settings == ui_.settings()) ||
        ui_.request_id() != prior_request) {
        ++host_.settings_revision;
        invalidate_export();
        supersede_host_job();
    }
    if (host_.request != 0U && (ui_.request_id() != host_.request || ui_.state() == UiRunState::Hold)) {
        cancel_host_job(HostCaptureState::Cancelled);
    }
    // Fault retry is permitted only after the UI accepted a fresh OK gesture.
    if (fault_ != UiControllerFault::None && ui_.state() != UiRunState::Fault) { fault_ = UiControllerFault::None; }
}

bool ScopeController::sample(std::uint16_t index, std::uint16_t& out) const noexcept
{
    if (!retained_frame_.valid || index >= retained_frame_.metadata.capture.length) { return false; }
    out = retained_frame_.samples[index]; return true;
}

bool ScopeController::acquisition_running() const noexcept
{
    return acquisition_.state() == AcquisitionState::Running;
}

const MeasurementResult* ScopeController::measurements() const noexcept
{
    const auto& frame = retained_frame_;
    const auto* result = analysis_.measurement.result();
    return frame.valid && result != nullptr &&
                   result->acquisition_epoch == frame.metadata.acquisition_epoch
               ? result
               : nullptr;
}

bool ScopeController::calibration_point(std::int64_t reference_microvolts, CalibrationPoint& point) const noexcept
{
    const auto* result = measurements();
    const auto& frame = retained_frame_;
    if (storage_busy() || result == nullptr || processing_.work != UiWork::Idle ||
        ui_.wants_run() || ui_.state() != UiRunState::Hold ||
        acquisition_running() || processing_.pending_frame_consumption ||
        acquisition_.state() != AcquisitionState::Stopped ||
        !ui_.physical_ready() || !same_physical_settings(frame.metadata.physical, ui_.physical()) ||
        frame.metadata.physical.coupling != InputCoupling::Dc || result->adc_clipped) {
        return false;
    }
    const CalibrationPoint candidate{
        frame.metadata.physical, frame.metadata.acquisition_epoch, result->mean_code_q16,
        reference_microvolts, result->count, result->minimum_code, result->maximum_code,
        static_cast<std::uint16_t>(result->rail_low_samples + result->rail_high_samples)};
    if (!calibration_point_valid(candidate)) { return false; }
    point = candidate;
    return true;
}

void ScopeController::cancel_consumers() noexcept
{
    screen_.cancel();
    analysis_.projector.cancel();
    analysis_.spectrum.cancel();
    if (processing_.spectrum_workspace_leased) {
        acquisition_.end_spectrum_workspace();
        processing_.spectrum_workspace_leased = false;
    }
    processing_.work = UiWork::Idle;
}

void ScopeController::publish_fault(UiControllerFault reason) noexcept
{
    fault_ = reason;
    analysis_.projection.available = false;
    ui_.on_presentation_fault();
}

bool ScopeController::cancel_acquisition() noexcept
{
    // Partial copy must never become a valid record. Completed copied frames
    // survive cancellation and remain available for HOLD/view changes.
    cancel_consumers();
    if (analysis_.measurement.state() == MeasurementState::Statistics || analysis_.measurement.state() == MeasurementState::Frequency) {
        analysis_.measurement.cancel();
    }
    const bool failed_running_stop = acquisition_.stop() == AcquisitionStopResult::Failed;
    static_cast<void>(acquisition_.release());
    processing_.pending_frame_consumption = false;
    processing_.active_request = 0U;
    if (failed_running_stop) {
        // A cancellation can be requested after the UI has already transitioned
        // to Hold/ArmPending, so its normal Acquiring fault gate cannot see this.
        // Apply the fault after cleanup without recursing through fail().
        calibration_session_.abort();
        publish_fault(UiControllerFault::Acquisition);
    }
    return !failed_running_stop;
}

void ScopeController::fail(UiControllerFault reason) noexcept
{
    invalidate_export();
    cancel_host_job(HostCaptureState::Fault);
    calibration_session_.abort();
    if (!cancel_acquisition()) {
        // Cancellation already published Acquisition after a failed stop; the
        // original fault reason still wins without repeating that publication.
        fault_ = reason;
    } else {
        publish_fault(reason);
    }
    // Redraw is deferred to the next service. A failed LCD must not produce a
    // retry loop; an explicit OK retries. Other failures may show the last frame.
    if (reason == UiControllerFault::Display) { static_cast<void>(ui_.take_actions()); }
}

void ScopeController::arm(std::uint32_t request) noexcept
{
    if (host_.request != request && host_.capture_state == HostCaptureState::Complete) { host_.capture_state = HostCaptureState::Cancelled; }
    invalidate_export();
    if (!config_.acquisition_enabled || plan_.support != ScopeTimebaseSupport::CircularDma ||
        ui_.physical().microvolts_per_div < MinimumPhysicalMicrovoltsPerDivision ||
        ui_.physical().microvolts_per_div > MaximumPhysicalMicrovoltsPerDivision) {
        fail(UiControllerFault::Configuration); return;
    }
    if (!cancel_acquisition()) { return; }
    const auto& settings = ui_.settings();
    // A measured fit in the board-bound RAM bank always takes precedence. The
    // callback fills a missing profile; only without a bank may it supply a
    // measured profile under the legacy standalone resolver contract.
    analysis_.active_calibration = config_.calibration_bank != nullptr ?
        config_.calibration_bank->resolve(ui_.physical()) : VoltageCalibration{};
    if (analysis_.active_calibration.kind == CalibrationKind::Unavailable && config_.calibration.resolve != nullptr) {
        const auto fallback = config_.calibration.resolve(config_.calibration.context, ui_.physical());
        if (config_.calibration_bank != nullptr && fallback.kind == CalibrationKind::Measured) {
            fail(UiControllerFault::Configuration); return;
        }
        analysis_.active_calibration = fallback;
    }
    if (analysis_.active_calibration.kind != CalibrationKind::Unavailable && !calibration_valid(analysis_.active_calibration)) {
        fail(UiControllerFault::Configuration); return;
    }
    const auto capacity = CaptureCapacity;
    const auto pretrigger = static_cast<std::uint16_t>(
        std::uint32_t{settings.trigger_position_permille} * (capacity - 1U) /
        settings_contract::TriggerPositionPermilleMaximum);
    auto hysteresis = static_cast<std::uint16_t>(8U);
    if (hysteresis > settings.trigger_level_code) { hysteresis = settings.trigger_level_code; }
    if (hysteresis > AdcMax - settings.trigger_level_code) { hysteresis = static_cast<std::uint16_t>(AdcMax - settings.trigger_level_code); }
    // At least pretrigger+1 samples, or 50ms nominal Auto wait, whichever is
    // longer. Deadline is in sample events and cannot overflow at 500kS/s.
    const auto divider = plan_.timing.divider;
    if (divider == 0U) { fail(UiControllerFault::Configuration); return; }
    // For positive divider, floor(timer/(divider*20)) equals
    // floor(floor(timer/divider)/20), avoiding a widened product.
    auto timeout = plan_.timing.timer_hz / divider / 20U;
    if (timeout <= pretrigger) { timeout = static_cast<std::uint32_t>(pretrigger) + 1U; }
    const auto period = plan_.timing.divider;
    // Reaching this fast path proves the planner selected CircularDma, whose
    // widened divider*DmaHalfSamples check guarantees this product is below
    // the half-range. DmaAcquisition::start independently validates the
    // request before any hardware callbacks.
    const auto half_cycles = period * DmaHalfSamples;
    const auto safety = period > 4096U ? period : 4096U;
    if (half_cycles <= safety + 1U) { fail(UiControllerFault::Configuration); return; }
    const auto room = half_cycles - safety - 1U;
    // Use the actual half-buffer ownership budget, capped at 1ms. Measured
    // 512-sample processing exceeds the former arbitrary 500us cap. The
    // monitor still requires a full safety margin and validates each copy.
    const auto poll_cycles = room < 72000U ? room : 72000U;
    const auto capture_mode = host_.request == request && !host_.uses_selected_mode ?
        TriggerMode::Single : settings.mode;
    const AcquisitionRequest acquisition_request{
        {CaptureCapacity, pretrigger, settings.trigger_level_code, hysteresis, capture_mode, settings.edge,
         capture_mode == TriggerMode::Auto ? timeout : 0U},
        ui_.physical(), plan_.timing, {period, safety, poll_cycles, 2U}, request};
    processing_.active_request = request;
    processing_.active_timebase_index = settings.timebase_index;
    // Keep the completed physical trace throughout acquisition. The projection
    // is logically unavailable until the next frozen record is projected;
    // neither label formatting nor an LCD transfer may delay the fast producer.
    analysis_.projection.available = false;
    config_.acquisition_input.set(true);
    const bool started = acquisition_.start(acquisition_request);
    config_.acquisition_input.set(false);
    if (!started || !ui_.on_capture_started(request)) { fail(UiControllerFault::Acquisition); }
}

void ScopeController::begin_copy() noexcept
{
    invalidate_export();
    auto& frame = retained_frame_;
    const auto* metadata = acquisition_.metadata();
    if (metadata == nullptr || processing_.active_request == 0U ||
        metadata->acquisition_epoch != processing_.active_request ||
        metadata->capture.length > frame.samples.size() || !ui_.on_frame_ready(processing_.active_request)) {
        fail(UiControllerFault::Copy);
        return;
    }
    cancel_consumers(); // Before changing the source/projection borrowed by any consumer.
    analysis_.measurement.cancel();
    frame.valid = false;
    frame.metadata = *metadata;
    frame.timebase_index = processing_.active_timebase_index;
    frame.calibration = analysis_.active_calibration;
    frame.request = processing_.active_request;
    processing_.pending_frame_consumption = true;
    retained_frame_.copied_samples = 0U;
    processing_.work = UiWork::Copying;
}

void ScopeController::poll_acquisition() noexcept
{
    acquisition_.poll();
    if (ui_.state() == UiRunState::Acquiring && acquisition_.state() == AcquisitionState::Fault) {
        fail(UiControllerFault::Acquisition); return;
    }
    if (acquisition_.state() == AcquisitionState::Ready && !processing_.pending_frame_consumption) { begin_copy(); }
}

ProjectionSettings ScopeController::view() const noexcept
{
    const auto& settings = ui_.settings();
    return {scope_timebase_us(settings.timebase_index), ScopePlotWidth, ScopePlotHeight,
        ScopePixelsPerVerticalDiv, settings.trigger_position_permille,
        settings.vertical_offset_pixels, settings.horizontal_offset_samples};
}

void ScopeController::begin_spectrum() noexcept
{
    const auto& frame = retained_frame_;
    analysis_.projection.available = false;
    if (!frame.valid || frame.metadata.capture.length < SpectrumLength ||
        frame.metadata.timing.timer_hz == 0U || frame.metadata.timing.divider == 0U ||
        acquisition_running()) {
        processing_.work = UiWork::Idle;
        present();
        return;
    }
    const auto maximum_start =
        static_cast<std::uint16_t>(frame.metadata.capture.length - SpectrumLength);
    const auto centered = frame.metadata.capture.pretrigger > SpectrumLength / 2U
                              ? static_cast<std::uint16_t>(
                                    frame.metadata.capture.pretrigger - SpectrumLength / 2U)
                              : std::uint16_t{0U};
    const auto start = centered > maximum_start ? maximum_start : centered;
    std::int32_t* workspace{};
    if (!acquisition_.begin_spectrum_workspace(workspace)) { fail(UiControllerFault::Spectrum); return; }
    processing_.spectrum_workspace_leased = true;
    if (!analysis_.spectrum.begin(frame.samples.data(), frame.metadata.capture.length, start,
                         frame.metadata, workspace,
                         static_cast<std::uint16_t>(DmaRingSamples / 2U), analysis_.projection)) {
        analysis_.spectrum.cancel();
        acquisition_.end_spectrum_workspace();
        processing_.spectrum_workspace_leased = false;
        fail(UiControllerFault::Spectrum);
        return;
    }
    processing_.work = UiWork::Spectrum;
}

void ScopeController::redraw() noexcept
{
    const auto& frame = retained_frame_;
    // Full label composition/reprojection is stopped-producer work. While a
    // fast record is pending, keep drawing the already composed scene. A new
    // record, HOLD/cancel or fault always requests a fresh composition.
    if (acquisition_.state() == AcquisitionState::Running) { return; }
    if (processing_.work == UiWork::Copying || processing_.work == UiWork::Measuring) { return; }
    cancel_consumers();
    revision_ = next_panel_generation(revision_);
    if (ui_.state() == UiRunState::Fault) { present(); return; }
    if (frame.valid) {
        if (measurements() == nullptr) {
            if (!analysis_.measurement.begin(frame.samples.data(), frame.metadata.capture.length,
                                    frame.metadata, frame.calibration)) {
                fail(UiControllerFault::Measurement); return;
            }
            processing_.work = UiWork::Measuring;
            return;
        }
        if (ui_.domain() == Domain::Spectrum) { begin_spectrum(); return; }
        if (!analysis_.projector.begin(frame.samples.data(), frame.metadata.capture.length,
                              frame.metadata, frame.calibration, view(), analysis_.projection)) {
            fail(UiControllerFault::Projection); return;
        }
        processing_.work = UiWork::Projecting;
    } else { analysis_.projection.available = false; present(); }
}

void ScopeController::present() noexcept
{
    format_scene();
    if (!config_.rendering_enabled) { processing_.work = UiWork::Idle; finish_presentation(); return; }
    if (!screen_.begin_composed(dirty_labels_)) { fail(UiControllerFault::Display); return; }
    dirty_labels_ = 0U;
    processing_.work = UiWork::Rendering;
}

void ScopeController::finish_presentation() noexcept
{
    if (!processing_.pending_frame_consumption) { return; }
    // Screen is Complete (or disabled); the projection remains private. Release
    // the acquisition record only after all copy/projection consumers finish.
    const bool released = acquisition_.release();
    if (!released) { fail(UiControllerFault::Copy); return; }
    processing_.pending_frame_consumption = false;
    static_cast<void>(ui_.on_frame_consumed(retained_frame_.request));
    if (host_.request != 0U && retained_frame_.request == host_.request) {
        cancel_host_job(HostCaptureState::Complete);
    }
}

void ScopeController::calibrate(const CalibrationRequest& request) noexcept
{
    invalidate_export();
    if (request.command == CalibrationCommand::Abort) {
        calibration_session_.abort(); calibration_status_ = UiCalibrationStatus::Updated; redraw(); return;
    }
    if (config_.calibration_bank == nullptr || config_.calibration_bank->board_identity() == 0U) {
        calibration_status_ = UiCalibrationStatus::Disabled; redraw(); return;
    }
    if (ui_.wants_run() || ui_.state() != UiRunState::Hold || acquisition_running() ||
        processing_.pending_frame_consumption ||
        acquisition_.state() != AcquisitionState::Stopped ||
        processing_.work == UiWork::Copying || processing_.work == UiWork::Measuring) {
        calibration_status_ = UiCalibrationStatus::HoldRequired; redraw(); return;
    }
    // A held view repaint owns no producer. Cancel it so a fresh command is
    // responsive even when selection changed faster than the screen rendered.
    cancel_consumers();
    bool accepted{};
    if (request.command == CalibrationCommand::Begin) {
        accepted = calibration_session_.begin(ui_.physical(), *config_.calibration_bank);
    } else if (request.command == CalibrationCommand::AcceptReference) {
        CalibrationPoint point{};
        if (!calibration_point(request.reference_microvolts, point)) {
            calibration_status_ = UiCalibrationStatus::ReferenceRequired; redraw(); return;
        }
        accepted = calibration_session_.accept(point, request.tolerance_microvolts);
    } else if (request.command == CalibrationCommand::Install) {
        accepted = calibration_session_.install(ui_.physical(), *config_.calibration_bank);
    }
    calibration_status_ = accepted ? UiCalibrationStatus::Updated : UiCalibrationStatus::Error;
    redraw();
}

UiServiceResult ScopeController::service() noexcept
{
    UiServiceResult result{processing_.work, 0U, {analysis_.projector.state(), 0U, 0U}, {screen_.state(), 0U, 0U},
        {analysis_.measurement.state(), 0U}};
    if (storage_.boot_pending) {
        storage_.boot_pending = false;
        if (storage_.status != UiStorageStatus::Disabled) {
            // RUN intent stays visible, but reconcile_acquisition_readiness inhibited capture
            // before any panel input or acquisition request can reach hardware.
            storage_.boot_active = true;
            start_storage(StorageCommand::None);
        }
    }
    // Storage may borrow the DMA workspace; defer acquisition polling until it completes.
    if (storage_busy()) { service_storage(); result.work = processing_.work; return result; }
    const auto pending = ui_.take_actions();
    if (pending.cancel_acquisition && !cancel_acquisition()) { result.work = processing_.work; return result; }
    // Timer factorization is configuration work, never work done while a
    // canceled producer is still running. Reconcile the backend before arm.
    reconcile_acquisition_readiness();
    const auto actions = ui_.take_actions();
    if (actions.cancel_acquisition && !cancel_acquisition()) { result.work = processing_.work; return result; }
    const auto storage = pending.storage != StorageCommand::None ? pending.storage : actions.storage;
    if (storage != StorageCommand::None) {
        start_storage(storage);
        result.work = processing_.work; return result;
    }
    const auto calibration = pending.calibration.command != CalibrationCommand::None ? pending.calibration : actions.calibration;
    if (calibration.command != CalibrationCommand::None) {
        calibrate(calibration);
        result.work = processing_.work;
        return result;
    }
    if (actions.arm) { arm(actions.request_id); }
    else if (pending.arm && pending.request_id == ui_.request_id() &&
             ui_.state() == UiRunState::ArmPending && !ui_.capture_inhibited()) { arm(pending.request_id); }
    if ((pending.redraw || actions.redraw) && fault_ != UiControllerFault::Display) { redraw(); }
    const auto prior_blocks = acquisition_.statistics().blocks_committed;
    poll_acquisition();
    // A leased half has just been copied and processed. Give the next DMA
    // poll its full budget instead of combining that work with LCD transfer.
    if (acquisition_.state() == AcquisitionState::Running &&
        acquisition_.statistics().blocks_committed != prior_blocks) { result.work = processing_.work; return result; }

    if (processing_.work == UiWork::Copying) {
        for (unsigned i{}; i < 64U && retained_frame_.copied_samples < retained_frame_.metadata.capture.length; ++i) {
            std::uint16_t value{};
            const bool read = acquisition_.read(retained_frame_.copied_samples, value);
            if (!read) {
                fail(UiControllerFault::Copy);
                break;
            }
            retained_frame_.samples[retained_frame_.copied_samples++] = value; ++result.copied;
        }
        if (processing_.work == UiWork::Copying &&
            retained_frame_.copied_samples == retained_frame_.metadata.capture.length) {
            retained_frame_.valid = true; processing_.work = UiWork::Idle; redraw();
        }
    } else if (processing_.work == UiWork::Measuring) {
        result.measurement = analysis_.measurement.step(32U);
        if (result.measurement.state == MeasurementState::Complete) { processing_.work = UiWork::Idle; redraw(); }
        else if (result.measurement.state != MeasurementState::Statistics && result.measurement.state != MeasurementState::Frequency) {
            fail(UiControllerFault::Measurement);
        }
    } else if (processing_.work == UiWork::Spectrum) {
        const auto spectrum_state = analysis_.spectrum.step(8U);
        if (spectrum_state == SpectrumState::Complete) {
            if (processing_.spectrum_workspace_leased) {
                acquisition_.end_spectrum_workspace();
                processing_.spectrum_workspace_leased = false;
            }
            processing_.work = UiWork::Idle;
            present();
        } else if (spectrum_state == SpectrumState::Invalid || spectrum_state == SpectrumState::Cancelled ||
                   spectrum_state == SpectrumState::Idle) { fail(UiControllerFault::Spectrum); }
    } else if (processing_.work == UiWork::Projecting) {
        result.projection = analysis_.projector.step(8U, 8U);
        if (result.projection.state == ProjectionState::Complete) { present(); }
        else if (result.projection.state == ProjectionState::Invalid) { fail(UiControllerFault::Projection); }
    } else if (processing_.work == UiWork::Rendering) {
        // Fast DMA has a half-buffer-derived poll deadline. A 64-pixel
        // GPIO stripe exceeded that deadline on the bench. Keep UI progress
        // bounded more tightly while the producer runs; stopped/slow paths
        // retain the larger stripe. Hardware polling guards remain unchanged.
        const auto pixels = static_cast<std::uint16_t>(acquisition_.state() == AcquisitionState::Running ? 16U : 64U);
        result.render = screen_.step(revision_, {pixels, 1U});
        if (result.render.state == RenderState::Complete) { processing_.work = UiWork::Idle; finish_presentation(); }
        else if (result.render.state != RenderState::Drawing) { fail(UiControllerFault::Display); }
    }
    result.work = processing_.work;
    return result;
}

} // namespace dso
