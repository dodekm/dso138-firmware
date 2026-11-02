#include "dso_ui_model.hpp"

namespace dso {
namespace {
constexpr std::uint32_t HalfRange{0x80000000U};
constexpr std::uint32_t RepeatDelayMs{400U};
constexpr std::uint32_t RepeatIntervalMs{80U};

bool one_key(std::uint8_t value) noexcept
{
    return value != 0U && (value & static_cast<std::uint8_t>(value - 1U)) == 0U;
}

bool valid_physical(const PanelUpdate& update) noexcept
{
    return update.ready && update.settings.generation != 0U &&
        update.settings.microvolts_per_div != 0U &&
        (update.settings.coupling == InputCoupling::Dc ||
         update.settings.coupling == InputCoupling::Ac ||
         update.settings.coupling == InputCoupling::Ground);
}

int bounded(int current, int delta, int minimum, int maximum) noexcept
{
    const auto next = current + delta;
    return next < minimum ? minimum : next > maximum ? maximum : next;
}
}

ScopeUi::ScopeUi(std::uint8_t timebase_count, bool startup_run) noexcept
    : timebase_count_{timebase_count}, valid_catalogue_{timebase_count != 0U}
{
    if (timebase_count_ <= settings_.timebase_index) {
        settings_.timebase_index = timebase_count_ == 0U ? 0U :
            static_cast<std::uint8_t>(timebase_count_ - 1U);
    }
    // Optional initial intent still passes the ordinary physical/backend gates.
    // This is applied once, never a retry policy after HOLD or a fault.
    wants_run_ = startup_run;
    reconcile();
}

bool scope_ui_settings_valid(const ScopeUiSettings& s, std::uint8_t catalogue_count) noexcept
{
    return catalogue_count != 0U && s.timebase_index < catalogue_count &&
        (s.mode == TriggerMode::Auto || s.mode == TriggerMode::Normal || s.mode == TriggerMode::Single) &&
        (s.edge == TriggerEdge::Rising || s.edge == TriggerEdge::Falling) && s.trigger_level_code <= 4095U &&
        s.trigger_position_permille <= 1000U && s.vertical_offset_pixels >= -240 && s.vertical_offset_pixels <= 240 &&
        s.horizontal_offset_samples >= -1024 && s.horizontal_offset_samples <= 1024 &&
        static_cast<unsigned>(s.measurement_view) <= static_cast<unsigned>(MeasurementView::Frequency);
}

void ScopeUi::redraw() noexcept { redraw_pending_ = true; }

void ScopeUi::invalidate(bool cancel) noexcept
{
    cancel_pending_ = cancel_pending_ || cancel;
    arm_pending_ = false;
    frame_available_ = false;
    storage_pending_ = StorageCommand::None;
    calibration_pending_ = {};
    request_id_ = next_panel_generation(request_id_);
}

void ScopeUi::request_arm() noexcept
{
    request_id_ = next_panel_generation(request_id_);
    arm_pending_ = true;
    frame_available_ = false;
    state_ = UiRunState::ArmPending;
    redraw();
}

void ScopeUi::reconcile() noexcept
{
    if (state_ == UiRunState::Fault) { return; }
    if (!wants_run_) { state_ = UiRunState::Hold; return; }
    if (state_ == UiRunState::FrameReady) { return; }
    if (!physical_ready_) { state_ = UiRunState::WaitingForControls; return; }
    if (capture_inhibited()) { state_ = UiRunState::WaitingForBackend; return; }
    if (state_ != UiRunState::Acquiring && state_ != UiRunState::ArmPending) { request_arm(); }
}

void ScopeUi::physical_update(const PanelUpdate& update) noexcept
{
    const bool ready = valid_physical(update);
    const bool changed = physical_ready_ != ready ||
        (ready && (physical_.generation != update.settings.generation ||
                   physical_.microvolts_per_div != update.settings.microvolts_per_div ||
                   physical_.coupling != update.settings.coupling ||
                   update.invalidate_acquisition || update.configuration_changed));
    physical_ready_ = ready;
    physical_ = ready ? update.settings : PhysicalSettings{0U, 0U, InputCoupling::Unknown};
    if (!changed) { return; }
    calibration_pending_ = {};
    if (state_ == UiRunState::Acquiring || state_ == UiRunState::ArmPending) {
        invalidate(true);
        state_ = UiRunState::Hold;
        if (settings_.mode == TriggerMode::Single) { wants_run_ = false; }
    }
    redraw();
    reconcile();
}

void ScopeUi::set_capture_inhibited(bool inhibited) noexcept
{
    if (inhibited_ == inhibited) { return; }
    inhibited_ = inhibited;
    if (inhibited && (state_ == UiRunState::Acquiring || state_ == UiRunState::ArmPending)) {
        invalidate(true);
        state_ = UiRunState::Hold;
        if (settings_.mode == TriggerMode::Single) { wants_run_ = false; }
    }
    redraw();
    reconcile();
}

bool ScopeUi::repeatable() const noexcept
{
    return focus_ != UiFocus::TriggerMode && focus_ != UiFocus::TriggerEdge &&
        focus_ != UiFocus::Measurements && focus_ != UiFocus::Storage && focus_ != UiFocus::Calibration &&
        focus_ != UiFocus::Domain;
}

std::uint32_t ScopeUi::calibration_edit_step() const noexcept
{
    const auto digit = focus_ == UiFocus::CalibrationTolerance ? tolerance_digit_ : reference_digit_;
    std::uint32_t step{1U};
    for (unsigned i{}; i < digit; ++i) { step *= 10U; }
    return step;
}

void ScopeUi::adjust(int direction) noexcept
{
    const auto previous = settings_;
    bool capture_change = true;
    switch (focus_) {
    case UiFocus::Timebase:
        if (valid_catalogue_) {
            settings_.timebase_index = static_cast<std::uint8_t>(bounded(
                settings_.timebase_index, direction, 0, timebase_count_ - 1));
        }
        break;
    case UiFocus::TriggerMode: {
        const auto mode = settings_.mode == TriggerMode::Auto ? 0 :
            settings_.mode == TriggerMode::Normal ? 1 : 2;
        const auto next = bounded(mode, direction, 0, 2);
        settings_.mode = next == 0 ? TriggerMode::Auto : next == 1 ? TriggerMode::Normal : TriggerMode::Single;
        break;
    }
    case UiFocus::TriggerEdge:
        settings_.edge = direction > 0 ? TriggerEdge::Falling : TriggerEdge::Rising;
        break;
    case UiFocus::TriggerLevel:
        settings_.trigger_level_code = static_cast<std::uint16_t>(bounded(
            settings_.trigger_level_code, direction * 16, 0, 4095));
        break;
    case UiFocus::TriggerPosition:
        settings_.trigger_position_permille = static_cast<std::uint16_t>(bounded(
            settings_.trigger_position_permille, direction * 25, 0, 1000));
        break;
    case UiFocus::VerticalPosition:
        settings_.vertical_offset_pixels = static_cast<std::int16_t>(bounded(
            settings_.vertical_offset_pixels, direction * 2, -240, 240));
        capture_change = false;
        break;
    case UiFocus::HorizontalPosition:
        settings_.horizontal_offset_samples = static_cast<std::int16_t>(bounded(
            settings_.horizontal_offset_samples, direction * 16, -1024, 1024));
        capture_change = false;
        break;
    case UiFocus::Measurements:
        settings_.measurement_view = static_cast<MeasurementView>(bounded(
            static_cast<int>(settings_.measurement_view), direction, 0, 3));
        capture_change = false;
        break;
    case UiFocus::Domain: {
        const auto next = direction > 0 ? Domain::Spectrum : Domain::Scope;
        if (domain_ != next) { domain_ = next; redraw(); }
        return;
    }
    case UiFocus::Storage: {
        const auto next = static_cast<StorageAction>(bounded(static_cast<int>(storage_action_), direction, 0, 5));
        if (next != storage_action_) { storage_action_ = next; redraw(); }
        return;
    }
    case UiFocus::CalibrationReference:
        reference_microvolts_ = bounded(reference_microvolts_, direction * static_cast<int>(calibration_edit_step()),
            -100000000, 100000000);
        redraw(); return;
    case UiFocus::CalibrationTolerance:
        tolerance_microvolts_ = static_cast<std::uint32_t>(bounded(static_cast<int>(tolerance_microvolts_),
            direction * static_cast<int>(calibration_edit_step()), 0, 1000000));
        redraw(); return;
    case UiFocus::Calibration:
        calibration_action_ = static_cast<CalibrationAction>(bounded(static_cast<int>(calibration_action_), direction, 0, 3));
        redraw(); return;
    default: return;
    }
    const bool changed = previous.timebase_index != settings_.timebase_index ||
        previous.mode != settings_.mode || previous.edge != settings_.edge ||
        previous.trigger_level_code != settings_.trigger_level_code ||
        previous.trigger_position_permille != settings_.trigger_position_permille ||
        previous.vertical_offset_pixels != settings_.vertical_offset_pixels ||
        previous.horizontal_offset_samples != settings_.horizontal_offset_samples ||
        previous.measurement_view != settings_.measurement_view;
    if (!changed) { return; }
    redraw();
    if (!capture_change || !wants_run_) { return; }
    if (previous.mode == TriggerMode::Single || settings_.mode == TriggerMode::Single) {
        if (state_ == UiRunState::Acquiring || state_ == UiRunState::ArmPending) { invalidate(true); }
        wants_run_ = false;
        state_ = UiRunState::Hold;
        return;
    }
    if (state_ == UiRunState::Acquiring || state_ == UiRunState::ArmPending) {
        invalidate(true);
        state_ = UiRunState::Hold;
    }
    reconcile();
}

void ScopeUi::press(std::uint8_t key) noexcept
{
    if (key == DebouncedButtons::Select) {
        const auto next = static_cast<unsigned>(focus_) + 1U;
        focus_ = next == static_cast<unsigned>(UiFocus::Count) ? UiFocus::Timebase : static_cast<UiFocus>(next);
        redraw();
    } else if (key == DebouncedButtons::Plus || key == DebouncedButtons::Minus) {
        adjust(key == DebouncedButtons::Plus ? 1 : -1);
    } else if (key == DebouncedButtons::Ok) {
        if (focus_ == UiFocus::CalibrationReference || focus_ == UiFocus::CalibrationTolerance || focus_ == UiFocus::Calibration) {
            storage_pending_ = StorageCommand::None;
            if (wants_run_) {
                if (state_ == UiRunState::Acquiring || state_ == UiRunState::ArmPending) { invalidate(true); }
                wants_run_ = false;
                state_ = UiRunState::Hold;
            } else if (state_ != UiRunState::Hold) {
                state_ = UiRunState::Hold;
            } else if (focus_ == UiFocus::CalibrationReference) {
                reference_digit_ = static_cast<std::uint8_t>((reference_digit_ + 1U) % 9U);
            } else if (focus_ == UiFocus::CalibrationTolerance) {
                tolerance_digit_ = static_cast<std::uint8_t>((tolerance_digit_ + 1U) % 7U);
            } else if (calibration_pending_.command == CalibrationCommand::None) {
                calibration_pending_ = {static_cast<CalibrationCommand>(static_cast<unsigned>(calibration_action_) + 1U),
                    reference_microvolts_, tolerance_microvolts_};
            }
            redraw(); return;
        }
        if (focus_ == UiFocus::Storage) {
            calibration_pending_ = {};
            if (wants_run_) {
                if (state_ == UiRunState::Acquiring || state_ == UiRunState::ArmPending) { invalidate(true); }
                wants_run_ = false;
                state_ = UiRunState::Hold;
            } else if (state_ != UiRunState::Hold) {
                // A completed Single or a fault must enter Hold before a
                // separate explicit storage gesture can issue a command.
                state_ = UiRunState::Hold;
            } else if (storage_pending_ == StorageCommand::None) {
                // One foreground command slot: a later menu change cannot
                // replace an operation already requested but not consumed.
                switch (storage_action_) {
                case StorageAction::SaveWaveform: storage_pending_ = StorageCommand::SaveWaveform; break;
                case StorageAction::RecallWaveform: storage_pending_ = StorageCommand::RecallWaveform; break;
                case StorageAction::SaveSettings: storage_pending_ = StorageCommand::SaveSettings; break;
                case StorageAction::RestoreSettings: storage_pending_ = StorageCommand::RestoreSettings; break;
                case StorageAction::SaveCalibration: storage_pending_ = StorageCommand::SaveCalibration; break;
                case StorageAction::RestoreCalibration: storage_pending_ = StorageCommand::RestoreCalibration; break;
                }
            }
            redraw();
            return;
        }
        // A later acquisition gesture supersedes an unconsumed storage
        // request even when the requested Single arm cannot start yet.
        storage_pending_ = StorageCommand::None;
        calibration_pending_ = {};
        if (state_ == UiRunState::Fault) { state_ = UiRunState::Hold; }
        if (wants_run_) {
            if (state_ == UiRunState::Acquiring || state_ == UiRunState::ArmPending) { invalidate(true); }
            wants_run_ = false;
            state_ = UiRunState::Hold;
        } else if (settings_.mode == TriggerMode::Single && (!physical_ready_ || capture_inhibited())) {
            state_ = !physical_ready_ ? UiRunState::WaitingForControls : UiRunState::WaitingForBackend;
        } else {
            wants_run_ = true;
            // A completed Single/Held record stays owned until the controller
            // handles the arm action's cancellation/release ordering.
            if (frame_available_) { invalidate(true); }
            state_ = UiRunState::Hold;
            reconcile();
        }
        redraw();
    }
}

void ScopeUi::buttons(const ButtonEvents& event, std::uint32_t now_ms) noexcept
{
    const auto held = static_cast<std::uint8_t>(event.held & DebouncedButtons::All);
    const auto pressed = static_cast<std::uint8_t>(event.pressed & DebouncedButtons::All);
    const auto elapsed = static_cast<std::uint32_t>(now_ms - last_button_time_);
    const bool discontinuity = event.discontinuity ||
        (observed_button_time_ && elapsed >= HalfRange);
    observed_button_time_ = true;
    last_button_time_ = now_ms;
    if (discontinuity || (!one_key(held) && held != 0U) || (!one_key(pressed) && pressed != 0U)) {
        suppress_until_release_ = held != 0U;
        repeat_key_ = 0U;
        return;
    }
    if (held == 0U) {
        suppress_until_release_ = false;
        repeat_key_ = 0U;
        return;
    }
    if (suppress_until_release_) { return; }
    if (pressed != 0U) {
        if (pressed != held) { suppress_until_release_ = true; repeat_key_ = 0U; return; }
        press(pressed);
        repeat_key_ = (pressed == DebouncedButtons::Plus || pressed == DebouncedButtons::Minus) && repeatable() ? pressed : 0U;
        repeated_ = false;
        press_time_ = now_ms;
        last_repeat_ = now_ms;
        return;
    }
    if (repeat_key_ == 0U || held != repeat_key_ || !repeatable()) { return; }
    const auto age = static_cast<std::uint32_t>(now_ms - press_time_);
    const auto repeat_age = static_cast<std::uint32_t>(now_ms - last_repeat_);
    if (age >= HalfRange || repeat_age >= HalfRange) { repeat_key_ = 0U; return; }
    if ((!repeated_ && age >= RepeatDelayMs) || (repeated_ && repeat_age >= RepeatIntervalMs)) {
        adjust(repeat_key_ == DebouncedButtons::Plus ? 1 : -1);
        repeated_ = true;
        last_repeat_ = now_ms;
    }
}

UiActions ScopeUi::take_actions() noexcept
{
    const UiActions result{cancel_pending_, arm_pending_, redraw_pending_, request_id_, storage_pending_, calibration_pending_};
    cancel_pending_ = false;
    arm_pending_ = false;
    redraw_pending_ = false;
    storage_pending_ = StorageCommand::None;
    calibration_pending_ = {};
    return result;
}

bool ScopeUi::matching(std::uint32_t request) const noexcept
{
    return request != 0U && request == request_id_;
}

bool ScopeUi::on_capture_started(std::uint32_t request) noexcept
{
    if (!matching(request) || state_ != UiRunState::ArmPending || !wants_run_ ||
        !physical_ready_ || capture_inhibited()) { return false; }
    arm_pending_ = false;
    state_ = UiRunState::Acquiring;
    redraw();
    return true;
}

bool ScopeUi::on_frame_ready(std::uint32_t request) noexcept
{
    if (!matching(request) || state_ != UiRunState::Acquiring) { return false; }
    frame_available_ = true;
    arm_pending_ = false;
    if (settings_.mode == TriggerMode::Single) { wants_run_ = false; }
    state_ = UiRunState::FrameReady;
    redraw();
    return true;
}

bool ScopeUi::on_frame_consumed(std::uint32_t request) noexcept
{
    if (!matching(request) || !frame_available_) { return false; }
    frame_available_ = false;
    state_ = UiRunState::Hold;
    reconcile();
    redraw();
    return true;
}

bool ScopeUi::on_capture_fault(std::uint32_t request) noexcept
{
    if (!matching(request) || (state_ != UiRunState::ArmPending &&
                               state_ != UiRunState::Acquiring)) { return false; }
    invalidate(true);
    wants_run_ = false;
    state_ = UiRunState::Fault;
    redraw();
    return true;
}

void ScopeUi::on_presentation_fault() noexcept
{
    invalidate(true);
    wants_run_ = false;
    state_ = UiRunState::Fault;
    redraw();
}

bool ScopeUi::restore_settings(const ScopeUiSettings& settings) noexcept
{
    if (!can_restore_settings(settings)) { return false; }
    commit_restored_settings(settings);
    return true;
}

// The first production link overflowed Flash by 16 B: LTO cloned this cold
// settings-commit body into both validated restore paths. Keep one shared copy.
#if defined(__GNUC__)
__attribute__((noinline))
#endif
void ScopeUi::commit_restored_settings(const ScopeUiSettings& settings) noexcept
{
    settings_ = settings;
    invalidate(false);
    wants_run_ = false;
    state_ = UiRunState::Hold;
    repeat_key_ = 0U;
    repeated_ = false;
    press_time_ = 0U;
    last_repeat_ = 0U;
    last_button_time_ = 0U;
    observed_button_time_ = false;
    suppress_until_release_ = true;
    redraw();
}

bool ScopeUi::can_restore_settings(const ScopeUiSettings& settings) const noexcept
{
    return !wants_run_ && state_ == UiRunState::Hold && !frame_available_ && scope_ui_settings_valid(settings, timebase_count_);
}

bool ScopeUi::can_restore_boot_settings(const ScopeUiSettings& settings) const noexcept
{
    return capture_inhibited() && state_ != UiRunState::Fault && state_ != UiRunState::Acquiring &&
        state_ != UiRunState::FrameReady && !frame_available_ &&
        scope_ui_settings_valid(settings, timebase_count_);
}

bool ScopeUi::restore_boot_settings(const ScopeUiSettings& settings) noexcept
{
    if (!can_restore_boot_settings(settings)) { return false; }
    commit_restored_settings(settings);
    domain_ = Domain::Scope;
    redraw();
    return true;
}

} // namespace dso
