#ifndef DSO_UI_MODEL_HPP
#define DSO_UI_MODEL_HPP

#include "dso_capture.hpp"
#include "dso_front_panel.hpp"

#include <cstdint>

namespace dso {

enum class UiFocus : std::uint8_t {
    Timebase, TriggerMode, TriggerEdge, TriggerLevel, TriggerPosition,
    VerticalPosition, HorizontalPosition, Measurements, Storage,
    CalibrationReference, CalibrationTolerance, Calibration, Domain, Count
};

enum class MeasurementView : std::uint8_t { Overview, Amplitude, Rms, Frequency };
enum class Domain : std::uint8_t { Scope, Spectrum };
enum class StorageAction : std::uint8_t { SaveSettings, RestoreSettings, SaveCalibration, RestoreCalibration };
enum class StorageCommand : std::uint8_t { None, SaveSettings, RestoreSettings, SaveCalibration, RestoreCalibration };
enum class CalibrationAction : std::uint8_t { Begin, AcceptReference, Install, Abort };
enum class CalibrationCommand : std::uint8_t { None, Begin, AcceptReference, Install, Abort };
// Values are frozen by the accepted OK gesture, not read later from editors.
struct CalibrationRequest final {
    CalibrationCommand command{CalibrationCommand::None};
    std::int32_t reference_microvolts{};
    std::uint32_t tolerance_microvolts{};
};

enum class UiRunState : std::uint8_t {
    Hold, WaitingForControls, WaitingForBackend, ArmPending, Acquiring,
    FrameReady, Fault
};

struct ScopeUiSettings final {
    std::uint8_t timebase_index;
    TriggerMode mode;
    TriggerEdge edge;
    std::uint16_t trigger_level_code;
    std::uint16_t trigger_position_permille;
    std::int16_t vertical_offset_pixels;
    std::int16_t horizontal_offset_samples;
    MeasurementView measurement_view{MeasurementView::Overview};
};
[[nodiscard]] bool scope_ui_settings_valid(const ScopeUiSettings& settings, std::uint8_t catalogue_count) noexcept;

struct UiActions final {
    bool cancel_acquisition;
    bool arm;
    bool redraw;
    std::uint32_t request_id;
    StorageCommand storage{StorageCommand::None};
    CalibrationRequest calibration{};
};

// Foreground-only UI intent, not a hardware/frame owner. Supply catalogue count
// from the actual timebase table; there is no duplicate table here. Construction
// starts in Hold. Unknown physical settings and an inhibited backend prevent arm.
//
// Actions are consumed once. On arm, the controller copies settings/physical
// metadata, cancels/relinquishes old consumers, starts its acquisition, then calls
// on_capture_started with that request ID. Stale callbacks are rejected. On a
// completed frame, notify ready; only after copy/render consumers are safe to
// release may on_frame_consumed allow another Auto/Normal arm. Single never
// rearms from completion or physical/configuration changes.
//
// View changes in Hold never change a frozen FrameMetadata or trigger hardware.
// The renderer must keep the frame's physical/timebase calibration metadata.
// Running capture-affecting changes request cancellation before a new arm;
// vertical/horizontal view offsets only redraw. Apply a PanelUpdate before new
// arm decisions, including its invalidate_acquisition acceptance event.
class ScopeUi final {
public:
    explicit ScopeUi(std::uint8_t timebase_count, bool startup_run = false) noexcept;
    ScopeUi(const ScopeUi&) = delete;
    ScopeUi& operator=(const ScopeUi&) = delete;

    void physical_update(const PanelUpdate& update) noexcept;
    void set_capture_inhibited(bool inhibited) noexcept;
    // Input is already debounced. Exactly one held+pressed key is actionable;
    // chords suppress all actions until every key is released. Plus/Minus repeat
    // after 400ms, then >=80ms intervals, at most one action per call. Mode,
    // edge, measurement, storage and calibration action selection do not repeat. No catch-up loops
    // or hidden destructive/flash gestures.
    // A discontinuity resets repeat and suppresses input until all released.
    // In Storage, OK first stops RUN intent; a fresh OK in Hold queues the
    // selected command without arming. Only the first unconsumed command is
    // retained. An acquisition OK outside Storage supersedes that command.
    // Calibration reference/tolerance fields use OK to select a decimal step;
    // the Calibration field queues a value snapshot. These fields first stop
    // RUN and never arm it. Storage/acquisition OK supersede calibration work.
    void buttons(const ButtonEvents& event, std::uint32_t now_ms) noexcept;

    [[nodiscard]] UiActions take_actions() noexcept;
    // Explicit foreground one-shot request used by remote clients. It preserves
    // the user's selected trigger mode and never synthesizes a panel gesture.
    [[nodiscard]] std::uint32_t request_host_single() noexcept;
    [[nodiscard]] bool cancel_host_single(std::uint32_t request_id) noexcept;
    [[nodiscard]] bool on_capture_started(std::uint32_t request_id) noexcept;
    [[nodiscard]] bool on_frame_ready(std::uint32_t request_id) noexcept;
    [[nodiscard]] bool on_frame_consumed(std::uint32_t request_id) noexcept;
    // Active-request faults latch Hold intent; a fresh OK is required, even in
    // Auto/Normal. Late faults from already completed/canceled work are ignored.
    [[nodiscard]] bool on_capture_fault(std::uint32_t request_id) noexcept;
    // Foreground projection/display failure after the controller has stopped
    // producers/consumers and released its Ready frame. Invalidates all tokens
    // and latches Fault until a fresh OK, including when already held.
    void on_presentation_fault() noexcept;
    // Controller must first stop consumers/release pending frames. Validated
    // settings may be restored only in idle Hold; this never arms acquisition.
    // Old tokens and repeat gestures are invalidated; observe a release before
    // another button press. The accepted physical settings remain unchanged.
    [[nodiscard]] bool restore_settings(const ScopeUiSettings& settings) noexcept;
    [[nodiscard]] bool can_restore_settings(const ScopeUiSettings& settings) const noexcept;
    // Boot restore is the only settings load allowed to replace the configured
    // startup intent. It validates fully before mutation and always leaves Hold.
    [[nodiscard]] bool can_restore_boot_settings(const ScopeUiSettings& settings) const noexcept;
    [[nodiscard]] bool restore_boot_settings(const ScopeUiSettings& settings) noexcept;

    [[nodiscard]] const ScopeUiSettings& settings() const noexcept { return settings_; }
    [[nodiscard]] const PhysicalSettings& physical() const noexcept { return physical_; }
    [[nodiscard]] UiFocus focus() const noexcept { return focus_; }
    [[nodiscard]] Domain domain() const noexcept { return domain_; }
    [[nodiscard]] StorageAction storage_action() const noexcept { return storage_action_; }
    [[nodiscard]] CalibrationAction calibration_action() const noexcept { return calibration_action_; }
    [[nodiscard]] std::int32_t reference_microvolts() const noexcept { return reference_microvolts_; }
    [[nodiscard]] std::uint32_t tolerance_microvolts() const noexcept { return tolerance_microvolts_; }
    [[nodiscard]] std::uint32_t calibration_edit_step() const noexcept;
    [[nodiscard]] UiRunState state() const noexcept { return state_; }
    [[nodiscard]] bool wants_run() const noexcept { return wants_run_; }
    [[nodiscard]] bool physical_ready() const noexcept { return physical_ready_; }
    [[nodiscard]] bool capture_inhibited() const noexcept { return inhibited_ || !valid_catalogue_; }
    [[nodiscard]] bool frame_available() const noexcept { return frame_available_; }
    [[nodiscard]] std::uint32_t request_id() const noexcept { return request_id_; }
    [[nodiscard]] bool has_pending_operation() const noexcept {
        return cancel_pending_ || arm_pending_ || storage_pending_ != StorageCommand::None ||
            calibration_pending_.command != CalibrationCommand::None;
    }

private:
    void redraw() noexcept;
    void invalidate(bool cancel) noexcept;
    void request_arm() noexcept;
    void reconcile() noexcept;
    void press(std::uint8_t key) noexcept;
    void adjust(int direction) noexcept;
    void commit_restored_settings(const ScopeUiSettings& settings) noexcept;
    [[nodiscard]] bool repeatable() const noexcept;
    [[nodiscard]] bool matching(std::uint32_t request) const noexcept;

    const std::uint8_t timebase_count_;
    const bool valid_catalogue_;
    ScopeUiSettings settings_{3U, TriggerMode::Auto, TriggerEdge::Rising,
                              2048U, 500U, 0, 0};
    PhysicalSettings physical_{0U, 0U, InputCoupling::Unknown};
    UiFocus focus_{UiFocus::Timebase};
    // Session-only FFT view state; saved scope settings and records stay unchanged.
    Domain domain_{Domain::Scope};
    StorageAction storage_action_{StorageAction::SaveSettings};
    StorageCommand storage_pending_{StorageCommand::None};
    CalibrationAction calibration_action_{CalibrationAction::Begin};
    CalibrationRequest calibration_pending_{};
    std::int32_t reference_microvolts_{};
    std::uint32_t tolerance_microvolts_{};
    std::uint8_t reference_digit_{3U}; // Start at 1000 uV; explicit OK changes decade.
    std::uint8_t tolerance_digit_{};
    UiRunState state_{UiRunState::Hold};
    bool wants_run_{false};
    bool physical_ready_{false};
    bool inhibited_{false};
    bool frame_available_{false};
    bool cancel_pending_{false};
    bool arm_pending_{false};
    bool redraw_pending_{true};
    std::uint32_t request_id_{1U};
    bool suppress_until_release_{false};
    std::uint8_t repeat_key_{0U};
    bool repeated_{false};
    std::uint32_t press_time_{0U};
    std::uint32_t last_repeat_{0U};
    std::uint32_t last_button_time_{0U};
    bool observed_button_time_{false};
    bool host_single_{false};
};

} // namespace dso
#endif
