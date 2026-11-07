#ifndef DSO_UI_CONTROLLER_HPP
#define DSO_UI_CONTROLLER_HPP

#include "dso_capture_config.hpp"
#include "dso_frame_metadata.hpp"
#include "dso_render_types.hpp"
#include "dso_scaling.hpp"
#include "dso_screen_scene.hpp"
#include "dso_ui_model.hpp"
#include "dso_instrument_codes.hpp"
#include "dso_measurements.hpp"
#include "dso_calibration.hpp"
#include "dso_calibration_session.hpp"
#include "dso_physical_settings.hpp"
#include "dso_spectrum.hpp"
#include <array>
#include <cstdint>

namespace dso {

class ScopeStorage;
class DmaAcquisition;
class ScopeScreen;
struct PanelUpdate;
struct ButtonEvents;

struct CalibrationSource final {
    void* context{};
    // Called at most once per arm. A configured calibration_bank is the sole
    // authority for measured profiles and is resolved first. This callback is
    // consulted only when that bank has no matching profile; with a bank it may
    // provide Nominal or Unavailable, never Measured. Without a bank, legacy
    // callback behavior (including Measured) is preserved. Unavailable means
    // RAW; malformed supplied profiles fault before acquisition starts.
    VoltageCalibration (*resolve)(void*, const PhysicalSettings&) noexcept{};
};
struct InputPauseGate final {
    void* context{};
    // Synchronous foreground-only callback; no controller reentry. A pause
    // stops control ADC conversions, a paired resume enables future scanning.
    // The configured purpose determines whether qualification is invalidated.
    void (*pause)(void*, bool) noexcept{};
    void set(bool paused) const noexcept { if (pause != nullptr) { pause(context, paused); } }
};
struct UiControllerConfig final {
    bool acquisition_enabled{};
    bool rendering_enabled{};
    CalibrationSource calibration{};
    ScopeStorage* storage{}; // Optional, non-owning; owner outlives controller. Identity zero disables reads.
    // When supplied, this is also the sole per-arm calibration resolver. Zero
    // board identity still disables the workflow; construction grants no I/O.
    CalibrationBank* calibration_bank{};
    InputPauseGate storage_input{}; // Entire job: invalidate/resynchronize input.
    InputPauseGate acquisition_input{}; // Synchronous planning/arm: quiesce ADC2.
    bool startup_run{}; // One initial intent; qualification gates still apply.
};
enum class UiStorageStatus : std::uint8_t { Disabled, Idle, Busy, Saved, Restored, Empty, Error, ReadOnly, HoldRequired };
enum class UiCalibrationStatus : std::uint8_t { Disabled, Ready, HoldRequired, ReferenceRequired, Updated, Error };
enum class UiControllerFault : std::uint8_t { None, Acquisition, Copy, Projection, Display, Configuration, Measurement, Spectrum };
enum class UiWork : std::uint8_t { Idle, Copying, Measuring, Projecting, Rendering, Spectrum };
struct UiServiceResult final {
    UiWork work;
    std::uint16_t copied;
    ProjectionStep projection;
    RenderStep render;
    MeasurementStep measurement;
};

using HostCaptureState = instrument_code_contract::HostCaptureState;
enum class HostSettingsResult : std::uint8_t { Applied, Invalid, Busy, Stale };

// One foreground owner orchestrates input -> acquire -> freeze -> bounded copy
// -> projection -> screen -> release. Auto/Normal rearm only after presentation;
// this intentionally has acquisition dead time, not continuous recording.
// No allocation, ISR calls, global HAL dependency, or default hardware activation.
class ScopeController final {
public:
    // Screen/acquisition outlive this owner. Attaching takes exclusive screen
    // control and cancels any previous render; scene() starts at defaults.
    ScopeController(DmaAcquisition& acquisition, ScopeScreen& screen,
                    UiControllerConfig config = {}) noexcept;
    // End the renderer's projection borrow. External acquisition/storage
    // owners outlive this object; outstanding storage jobs must finish first.
    ~ScopeController() noexcept { invalidate_export(); cancel_consumers(); }
    ScopeController(const ScopeController&) = delete;
    ScopeController& operator=(const ScopeController&) = delete;
    // Apply every tuple (including invalid tuples) BEFORE service/arm. The
    // acceptance event invalidates old acquisition before new settings are used.
    void panel_update(const PanelUpdate& update, std::uint32_t now_ms) noexcept;
    void buttons(const ButtonEvents& event, std::uint32_t now_ms) noexcept;
    [[nodiscard]] UiServiceResult service() noexcept;
    [[nodiscard]] const ScopeUi& ui() const noexcept { return ui_; }
    [[nodiscard]] UiControllerFault fault() const noexcept { return fault_; }
    [[nodiscard]] UiWork work() const noexcept { return processing_.work; }
    [[nodiscard]] bool frame_available() const noexcept { return retained_frame_.valid; }
    // Null without a completed frame; copy the value to retain metadata across
    // a later acquisition, which replaces the controller-owned record.
    [[nodiscard]] const FrameMetadata* frame_metadata() const noexcept
    {
        return retained_frame_.valid ? &retained_frame_.metadata : nullptr;
    }
    [[nodiscard]] std::uint8_t frame_timebase_index() const noexcept
    {
        return retained_frame_.timebase_index;
    }
    [[nodiscard]] VoltageCalibration frame_calibration() const noexcept
    {
        return retained_frame_.calibration;
    }
    // Read-only derived view; contents can change during a later foreground
    // service. The controller manages its render borrow until job completion.
    [[nodiscard]] const WaveformProjection& projection() const noexcept { return analysis_.projection; }
    // Read-only borrowed view into ScopeScreen's composition. Its storage
    // follows the screen lifetime; later controller updates may replace contents.
    [[nodiscard]] const ScreenSnapshot& scene() const noexcept;
    [[nodiscard]] ScopeTimebaseSupport timebase_support() const noexcept { return plan_.support; }
    // Returns the current selection's validated timer plan after foreground service.
    [[nodiscard]] const TimebasePlan* selected_timing() const noexcept
    {
        return plan_.support == ScopeTimebaseSupport::CircularDma &&
               planned_index_ == ui_.settings().timebase_index ? &plan_.timing : nullptr;
    }
    [[nodiscard]] bool acquisition_running() const noexcept;
    [[nodiscard]] bool storage_busy() const noexcept { return storage_.phase != StoragePhase::Idle; }
    [[nodiscard]] UiStorageStatus storage_status() const noexcept { return storage_.status; }
    [[nodiscard]] UiCalibrationStatus calibration_status() const noexcept { return calibration_status_; }
    [[nodiscard]] const CalibrationSession& calibration_session() const noexcept { return calibration_session_; }
    [[nodiscard]] bool sample(std::uint16_t index, std::uint16_t& out) const noexcept;
    // Borrow until the next frame/cancellation/reduction; copy for long-lived use.
    [[nodiscard]] const MeasurementResult* measurements() const noexcept;
    // Build a reference summary only from a completely processed, held DC
    // record matching the currently accepted switches. Reference is external.
    [[nodiscard]] bool calibration_point(std::int64_t reference_microvolts,
                                         CalibrationPoint& point) const noexcept;
    // Returns a nonzero lease for the existing held frame. Frame metadata,
    // calibration, and samples may be borrowed for one foreground service
    // call. Retain only the token between calls.
    [[nodiscard]] std::uint32_t begin_export() noexcept;
    [[nodiscard]] std::uint32_t active_export_token() const noexcept { return host_.export_token; }
    [[nodiscard]] const std::uint16_t* export_samples(std::uint32_t token) const noexcept;
    [[nodiscard]] bool end_export(std::uint32_t token) noexcept;
    // Each host job completes once. Legacy clients force Single triggering.
    [[nodiscard]] std::uint32_t request_host_single(bool use_selected_mode = false) noexcept;
    [[nodiscard]] HostCaptureState host_capture_state(std::uint32_t job) const noexcept;
    [[nodiscard]] bool cancel_host_capture(std::uint32_t job) noexcept;
    // USB changes the next acquisition in idle HOLD. The held record remains
    // immutable. A local button press or transport loss ends the override.
    [[nodiscard]] bool host_settings_writable() const noexcept;
    [[nodiscard]] std::uint16_t host_settings_revision() const noexcept { return host_.settings_revision; }
    [[nodiscard]] HostSettingsResult apply_host_settings(std::uint16_t expected_revision,
        const ScopeUiSettings& settings) noexcept;
    [[nodiscard]] bool host_hold_available() const noexcept;
    [[nodiscard]] HostSettingsResult hold_from_host(std::uint16_t expected_revision) noexcept;
    void restore_host_settings() noexcept;

private:
    enum class StoragePhase : std::uint8_t { Idle, Opening, Saving, Reading };
    // Single foreground-owned record of the persistence workflow and boot read.
    struct StorageState final {
        StoragePhase phase{StoragePhase::Idle};
        StorageCommand command{StorageCommand::None};
        UiStorageStatus status{UiStorageStatus::Disabled};
        std::uint16_t copied_bytes{}; // Offset while restoring the saved calibration record.
        bool boot_pending{true};
        bool boot_active{};
        bool boot_waiting_input{};
    };
    // UI-owned copy of the last DMA record and the settings frozen with it.
    // valid is published only after the bounded sample copy is complete.
    struct RetainedFrameState final {
        FrameMetadata metadata{};
        std::array<std::uint16_t, CaptureCapacity> samples{};
        VoltageCalibration calibration{};
        std::uint32_t request{};
        std::uint8_t timebase_index{};
        std::uint16_t copied_samples{}; // Progress of the bounded DMA-record copy.
        bool valid{};
    };
    // Foreground work stage and resources held across service calls.
    struct ProcessingState final {
        std::uint32_t active_request{};
        std::uint8_t active_timebase_index{};
        UiWork work{UiWork::Idle};
        bool pending_frame_consumption{};
        bool spectrum_workspace_leased{};
    };
    // Per-frame calibration and incremental signal-analysis engines. Kept
    // separate from ProcessingState, which tracks controller scheduling.
    struct AnalysisWorkspace final {
        VoltageCalibration active_calibration{};
        WaveformProjection projection{};
        WaveformProjector projector{};
        SpectrumAnalyzer spectrum{};
        MeasurementEngine measurement{};
    };
    static bool stored_calibration(void*, std::uint16_t, std::uint8_t*, std::uint16_t) noexcept;
    static bool staged_calibration(void*, std::uint16_t, std::uint8_t*, std::uint16_t) noexcept;
    void start_storage(StorageCommand command) noexcept;
    void calibrate(const CalibrationRequest& request) noexcept;
    void service_storage() noexcept;
    void finish_storage(UiStorageStatus status) noexcept;
    void reconcile_acquisition_readiness() noexcept;
    void cancel_consumers() noexcept;
    void invalidate_export() noexcept;
    void cancel_host_job(HostCaptureState state) noexcept;
    void supersede_host_job() noexcept;
    [[nodiscard]] bool host_idle_hold() const noexcept;
    void publish_fault(UiControllerFault reason) noexcept;
    // Returns false only when this cancellation newly discovers a failed stop.
    [[nodiscard]] bool cancel_acquisition() noexcept;
    void fail(UiControllerFault reason) noexcept;
    void arm(std::uint32_t request) noexcept;
    void begin_copy() noexcept;
    void poll_acquisition() noexcept;
    void redraw() noexcept;
    void begin_spectrum() noexcept;
    void present() noexcept;
    void finish_presentation() noexcept;
    void format_scene() noexcept;
    void format_measurements() noexcept;
    [[nodiscard]] ProjectionSettings view() const noexcept;
    DmaAcquisition& acquisition_;
    ScopeScreen& screen_;
    const UiControllerConfig config_;
    ScopeUi ui_;
    ScopeTimebasePlan plan_{};
    std::uint8_t planned_index_{255U};
    UiControllerFault fault_{UiControllerFault::None};
    ProcessingState processing_{};
    // USB lease/job and settings state, including local-control arbitration.
    struct HostControlState final {
        std::uint32_t export_token{};
        std::uint32_t export_generation{};
        std::uint32_t job{};
        std::uint32_t request{};
        HostCaptureState capture_state{HostCaptureState::None};
        bool uses_selected_mode{};
        std::uint16_t settings_revision{};
        std::uint8_t local_buttons_held{};
        bool override_active{};
        ScopeUiSettings prior_settings{};
    };
    HostControlState host_{};
    RetainedFrameState retained_frame_{};
    std::uint32_t revision_{1U};
    // Exact formatter changes accumulate until a renderer accepts the scene.
    // A cancelled physical job independently invalidates the screen's cache.
    std::uint16_t dirty_labels_{ScreenTextAll};
    AnalysisWorkspace analysis_{};
    StorageState storage_{};
    CalibrationSession calibration_session_{};
    UiCalibrationStatus calibration_status_{UiCalibrationStatus::Disabled};
};

} // namespace dso
#endif
