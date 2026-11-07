#include "dso_ui_controller.hpp"
#include "dso_acquisition.hpp"
#include "dso_front_panel.hpp"

namespace dso {

// USB-facing capture leases and setting overrides share controller-owned state.
void ScopeController::invalidate_export() noexcept
{
    host_.export_token = 0U;
}

void ScopeController::cancel_host_job(HostCaptureState state) noexcept
{
    if (host_.request == 0U) { return; }
    host_.capture_state = state;
    host_.request = 0U;
}

void ScopeController::supersede_host_job() noexcept
{
    if (host_.request != 0U) { cancel_host_job(HostCaptureState::Cancelled); }
    else if (host_.capture_state == HostCaptureState::Complete) { host_.capture_state = HostCaptureState::Cancelled; }
}

// Share the foreground ownership check for every host operation that starts
// from HOLD. Each caller adds its own producer or record requirement.
#if defined(__GNUC__)
__attribute__((noinline))
#endif
bool ScopeController::host_idle_hold() const noexcept
{
    return ui_.state() == UiRunState::Hold && !ui_.wants_run() &&
        host_.export_token == 0U && !processing_.pending_frame_consumption && processing_.work == UiWork::Idle &&
        !storage_busy() && !ui_.has_pending_operation() && host_.request == 0U;
}

std::uint32_t ScopeController::begin_export() noexcept
{
    if (!retained_frame_.valid || retained_frame_.metadata.capture.length == 0U ||
        acquisition_.state() != AcquisitionState::Stopped || !host_idle_hold()) { return 0U; }
    host_.export_generation = next_panel_generation(host_.export_generation);
    host_.export_token = host_.export_generation;
    return host_.export_token;
}

const std::uint16_t* ScopeController::export_samples(std::uint32_t token) const noexcept
{
    return token != 0U && token == host_.export_token ? retained_frame_.samples.data() : nullptr;
}

bool ScopeController::end_export(std::uint32_t token) noexcept
{
    if (token == 0U || token != host_.export_token) { return false; }
    invalidate_export();
    return true;
}

std::uint32_t ScopeController::request_host_single(bool use_selected_mode) noexcept
{
    if (!config_.acquisition_enabled || !ui_.physical_ready() || ui_.capture_inhibited() ||
        acquisition_running() || !host_idle_hold()) { return 0U; }
    const auto request = ui_.request_host_single();
    if (request == 0U) { return 0U; }
    invalidate_export();
    host_.job = next_panel_generation(host_.job);
    host_.request = request;
    host_.uses_selected_mode = use_selected_mode;
    host_.capture_state = HostCaptureState::Pending;
    return host_.job;
}

HostCaptureState ScopeController::host_capture_state(std::uint32_t job) const noexcept
{
    return job != 0U && job == host_.job ? host_.capture_state : HostCaptureState::None;
}

bool ScopeController::cancel_host_capture(std::uint32_t job) noexcept
{
    if (job == 0U || job != host_.job || host_.capture_state != HostCaptureState::Pending || host_.request == 0U ||
        !ui_.cancel_host_single(host_.request)) { return false; }
    cancel_host_job(HostCaptureState::Cancelled);
    invalidate_export();
    return true;
}

bool ScopeController::host_settings_writable() const noexcept
{
    return host_.local_buttons_held == 0U && acquisition_.state() == AcquisitionState::Stopped &&
        !calibration_session_.active() && host_idle_hold();
}

HostSettingsResult ScopeController::apply_host_settings(std::uint16_t expected_revision,
    const ScopeUiSettings& settings) noexcept
{
    if (!scope_ui_settings_valid(settings, ScopeTimebaseCount)) { return HostSettingsResult::Invalid; }
    if (expected_revision != host_.settings_revision) { return HostSettingsResult::Stale; }
    if (!host_settings_writable()) { return HostSettingsResult::Busy; }

    const auto& current = ui_.settings();
    if (current == settings) { return HostSettingsResult::Applied; }
    if (!host_.override_active) {
        host_.prior_settings = current;
        host_.override_active = true;
    }
    ui_.apply_host_selection(settings);
    ++host_.settings_revision;
    reconcile_acquisition_readiness();
    return HostSettingsResult::Applied;
}

void ScopeController::restore_host_settings() noexcept
{
    if (!host_.override_active) { return; }
    ui_.apply_host_selection(host_.prior_settings);
    host_.override_active = false;
    ++host_.settings_revision;
}

bool ScopeController::host_hold_available() const noexcept
{
    return ui_.wants_run() && ui_.settings().mode != TriggerMode::Single &&
        host_.request == 0U && host_.local_buttons_held == 0U && !storage_busy() &&
        fault_ == UiControllerFault::None;
}

HostSettingsResult ScopeController::hold_from_host(std::uint16_t expected_revision) noexcept
{
    if (expected_revision != host_.settings_revision) { return HostSettingsResult::Stale; }
    if (!host_hold_available()) { return HostSettingsResult::Busy; }
    ui_.hold();
    ++host_.settings_revision;
    return HostSettingsResult::Applied;
}

} // namespace dso
