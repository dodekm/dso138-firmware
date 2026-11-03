#include "dso_ui_controller.hpp"
#include "dso_u64_division.hpp"

#include <cstddef>
#include <new>
#include <type_traits>

namespace dso {
namespace {
template<std::size_t PoolSize, std::size_t Count, std::size_t OracleCount>
constexpr bool label_pool_matches(const char (&pool)[PoolSize], const std::uint8_t (&offsets)[Count],
                                  const char* const (&oracle)[OracleCount]) noexcept
{
    if (Count != OracleCount || PoolSize >= 256U) { return false; }
    for (std::size_t label{}; label < Count; ++label) {
        if (offsets[label] >= PoolSize) { return false; }
        std::size_t byte{};
        do {
            if (byte >= PoolSize - offsets[label]) { return false; }
            if (pool[offsets[label] + byte] != oracle[label][byte]) { return false; }
        } while (oracle[label][byte++] != '\0');
    }
    return true;
}
template<std::size_t PoolSize, std::size_t Count>
constexpr const char* compact_label(const char (&pool)[PoolSize], const std::uint8_t (&offsets)[Count],
                                    std::size_t index, std::size_t invalid) noexcept
{
    return pool + offsets[index < Count ? index : invalid];
}
constexpr bool label_equals(const char* actual, const char* expected) noexcept
{
    for (std::size_t i{};; ++i) {
        if (actual[i] != expected[i]) { return false; }
        if (expected[i] == '\0') { return true; }
    }
}

// Offsets follow each enum's named ordinal order. Compile-time oracles below
// bind both the packed text and the enum values so reordering requires a review.
constexpr char StorageActionPool[] = "SAVE CONFIG\0LOAD CONFIG\0SAVE CAL\0RESTORE CAL";
constexpr std::uint8_t StorageActionOffsets[]{0U, 12U, 24U, 33U};
constexpr const char* StorageActionOracle[]{"SAVE CONFIG", "LOAD CONFIG", "SAVE CAL", "RESTORE CAL"};
static_assert(label_pool_matches(StorageActionPool, StorageActionOffsets, StorageActionOracle), "Storage action labels/offsets changed");
static_assert(static_cast<std::size_t>(StorageAction::SaveSettings) == 0U &&
              static_cast<std::size_t>(StorageAction::RestoreSettings) == 1U &&
              static_cast<std::size_t>(StorageAction::SaveCalibration) == 2U &&
              static_cast<std::size_t>(StorageAction::RestoreCalibration) == 3U,
              "Update storage action label order");
constexpr char StorageStatusPool[] = "DISABLED\0SAVED\0RESTORED\0EMPTY\0ERROR\0READ ONLY\0HOLD TO SAVE\0READY";
constexpr std::uint8_t StorageStatusOffsets[]{0U, 59U, 59U, 9U, 15U, 24U, 30U, 36U, 46U};
constexpr const char* StorageStatusOracle[]{"DISABLED", "READY", "READY", "SAVED", "RESTORED", "EMPTY", "ERROR", "READ ONLY", "HOLD TO SAVE"};
static_assert(label_pool_matches(StorageStatusPool, StorageStatusOffsets, StorageStatusOracle), "Storage status labels/offsets changed");
static_assert(static_cast<std::size_t>(UiStorageStatus::Disabled) == 0U &&
              static_cast<std::size_t>(UiStorageStatus::Idle) == 1U &&
              static_cast<std::size_t>(UiStorageStatus::Busy) == 2U &&
              static_cast<std::size_t>(UiStorageStatus::Saved) == 3U &&
              static_cast<std::size_t>(UiStorageStatus::Restored) == 4U &&
              static_cast<std::size_t>(UiStorageStatus::Empty) == 5U &&
              static_cast<std::size_t>(UiStorageStatus::Error) == 6U &&
              static_cast<std::size_t>(UiStorageStatus::ReadOnly) == 7U &&
              static_cast<std::size_t>(UiStorageStatus::HoldRequired) == 8U,
              "Update storage status label order");
constexpr char FrequencyStatePool[] = "OK\0LOW\0CYCLES\0FAST\0UNSTABLE\0CLIP\0GND\0RES";
constexpr std::uint8_t FrequencyStateOffsets[]{0U, 3U, 7U, 14U, 19U, 28U, 33U, 37U};
constexpr const char* FrequencyStateOracle[]{"OK", "LOW", "CYCLES", "FAST", "UNSTABLE", "CLIP", "GND", "RES"};
static_assert(label_pool_matches(FrequencyStatePool, FrequencyStateOffsets, FrequencyStateOracle), "Frequency labels/offsets changed");
static_assert(static_cast<std::size_t>(FrequencyStatus::Valid) == 0U &&
              static_cast<std::size_t>(FrequencyStatus::LowAmplitude) == 1U &&
              static_cast<std::size_t>(FrequencyStatus::InsufficientCycles) == 2U &&
              static_cast<std::size_t>(FrequencyStatus::TooFast) == 3U &&
              static_cast<std::size_t>(FrequencyStatus::Unstable) == 4U &&
              static_cast<std::size_t>(FrequencyStatus::Clipped) == 5U &&
              static_cast<std::size_t>(FrequencyStatus::Grounded) == 6U &&
              static_cast<std::size_t>(FrequencyStatus::BelowResolution) == 7U,
              "Update frequency label order");
constexpr char CalibrationStagePool[] = "POINT 1\0POINT 2\0CHECK 3\0VERIFIED\0INSTALLED\0ABORTED\0FAILED\0IDLE";
constexpr std::uint8_t CalibrationStageOffsets[]{58U, 0U, 8U, 16U, 24U, 33U, 43U, 51U};
constexpr const char* CalibrationStageOracle[]{"IDLE", "POINT 1", "POINT 2", "CHECK 3", "VERIFIED", "INSTALLED", "ABORTED", "FAILED"};
static_assert(label_pool_matches(CalibrationStagePool, CalibrationStageOffsets, CalibrationStageOracle), "Calibration stage labels/offsets changed");
static_assert(static_cast<std::size_t>(CalibrationSessionState::Idle) == 0U &&
              static_cast<std::size_t>(CalibrationSessionState::FirstReference) == 1U &&
              static_cast<std::size_t>(CalibrationSessionState::SecondReference) == 2U &&
              static_cast<std::size_t>(CalibrationSessionState::ValidationReference) == 3U &&
              static_cast<std::size_t>(CalibrationSessionState::Verified) == 4U &&
              static_cast<std::size_t>(CalibrationSessionState::Installed) == 5U &&
              static_cast<std::size_t>(CalibrationSessionState::Aborted) == 6U &&
              static_cast<std::size_t>(CalibrationSessionState::Failed) == 7U,
              "Update calibration stage label order");
constexpr char CalibrationFailurePool[] = "Need new reference\0Refs too close\0Bad slope\0Cal out of range\0Rounding failed\0Check 3 failed; BEGIN\0Check DC/session/refs";
constexpr std::uint8_t CalibrationFailureOffsets[]{0U, 19U, 34U, 44U, 61U, 77U, 99U};
constexpr const char* CalibrationFailureOracle[]{"Need new reference", "Refs too close", "Bad slope", "Cal out of range", "Rounding failed", "Check 3 failed; BEGIN", "Check DC/session/refs"};
static_assert(label_pool_matches(CalibrationFailurePool, CalibrationFailureOffsets, CalibrationFailureOracle), "Calibration failure labels/offsets changed");
static_assert(static_cast<std::size_t>(CalibrationStatus::StaleEpoch) == 5U &&
              static_cast<std::size_t>(CalibrationStatus::InsufficientSpan) == 6U &&
              static_cast<std::size_t>(CalibrationStatus::InvalidSlope) == 7U &&
              static_cast<std::size_t>(CalibrationStatus::Unrepresentable) == 8U &&
              static_cast<std::size_t>(CalibrationStatus::FitResidual) == 9U &&
              static_cast<std::size_t>(CalibrationStatus::ValidationFailed) == 10U,
              "Update calibration failure label range");
static_assert(label_equals(compact_label(StorageActionPool, StorageActionOffsets, 99U, 3U), "RESTORE CAL"), "Invalid storage action fallback changed");
static_assert(label_equals(StorageStatusPool + StorageStatusOffsets[1U], "READY"), "Invalid storage status fallback changed");
static_assert(label_equals(compact_label(FrequencyStatePool, FrequencyStateOffsets, 99U, 7U), "RES"), "Invalid frequency fallback changed");
static_assert(label_equals(compact_label(CalibrationStagePool, CalibrationStageOffsets, 99U, 0U), "IDLE"), "Invalid calibration stage fallback changed");
static_assert(label_equals(compact_label(CalibrationFailurePool, CalibrationFailureOffsets, 6U, 6U), "Check DC/session/refs"), "Calibration failure fallback changed");

// Small bounded formatter: no printf floating point, locale, heap, or hidden
// std::string storage. SI case remains significant throughout the screen.
class Text final {
public:
    template<std::size_t N> explicit Text(char (&buffer)[N], std::uint16_t* dirty = nullptr,
                                          std::uint16_t bit = 0U, ScreenTextRange* range = nullptr) noexcept
        : data_{buffer}, dirty_{dirty}, range_{range}, size_{N}, bit_{bit}
    { static_assert(N <= 64U, "Text change endpoints must fit the bounded glyph range"); }
    ~Text() noexcept
    {
        if (!old_ended_ && data_[used_] != '\0') {
            auto tail = used_;
            while (tail + 1U < size_ && data_[tail] != '\0') { ++tail; }
            change(used_,tail); // Erase every previously visible suffix glyph.
        }
        data_[used_] = '\0';
        if (changed_ && dirty_ != nullptr) { *dirty_ |= bit_; }
    }
    Text(const Text&) = delete;
    Text& operator=(const Text&) = delete;
    Text& add(const char* value) noexcept
    {
        // Compare the previous label in place before overwriting each byte.
        // Terminate once, after composition: intermediate add() calls must not
        // erase old bytes before the exact comparison reaches them.
        while (*value != '\0' && used_ + 1U < size_) {
            old_ended_ = old_ended_ || data_[used_] == '\0';
            if (old_ended_ || data_[used_] != *value) {
                change(used_,static_cast<std::uint16_t>(used_ + 1U));
            }
            data_[used_++] = *value++;
        }
        return *this;
    }
    // All formatter integer parts fit uint32_t: calibration input is bounded to +/-100 V,
    // ADC-derived quantities cover at most 4095 codes, and decimal callers format
    // bounded timer/sample/frequency ratios. Ten digits therefore cover every UI value.
    Text& number(std::uint32_t value) noexcept
    {
        char digits[10]{}; unsigned count{};
        do { digits[count++] = static_cast<char>('0' + value % 10U); value /= 10U; } while (value != 0U);
        while (count != 0U) { const char one[]{digits[--count], '\0'}; add(one); }
        return *this;
    }
    Text& signed_number(std::int64_t value) noexcept
    {
        const auto magnitude = value < 0 ? static_cast<std::uint64_t>(-(value + 1)) + 1U : static_cast<std::uint64_t>(value);
        if (value < 0) { add("-"); } return number(static_cast<std::uint32_t>(magnitude));
    }
    Text& decimal(std::uint64_t numerator, std::uint64_t denominator) noexcept
    {
        const auto scaled = detail::divide_u64(numerator * 100U + denominator / 2U, denominator).quotient;
        const auto integer = detail::divide_u64(scaled, 100U);
        number(static_cast<std::uint32_t>(integer.quotient));
        const auto fraction = static_cast<std::uint32_t>(integer.remainder);
        if (fraction != 0U) {
            add("."); const char digits[]{static_cast<char>('0' + fraction / 10U),
                static_cast<char>('0' + fraction % 10U), '\0'};
            add(digits);
        }
        return *this;
    }
private:
    void change(std::uint16_t first, std::uint16_t end) noexcept
    {
        changed_ = true;
        if (range_ != nullptr) { range_->include(first,end); }
    }
    char* data_;
    std::uint16_t* dirty_;
    ScreenTextRange* range_;
    std::uint16_t size_;
    std::uint16_t used_{};
    std::uint16_t bit_;
    bool changed_{};
    bool old_ended_{};
};

void voltage(Text& text, std::int64_t microvolts) noexcept
{
    const auto magnitude = microvolts < 0 ? static_cast<std::uint64_t>(-(microvolts + 1)) + 1U : static_cast<std::uint64_t>(microvolts);
    if (microvolts < 0) { text.add("-"); }
    if (magnitude >= 1000000U) { text.decimal(magnitude, 1000000U).add("V"); }
    else if (magnitude >= 1000U) { text.decimal(magnitude, 1000U).add("mV"); }
    else { text.number(static_cast<std::uint32_t>(magnitude)).add("uV"); }
}
void timebase(Text& text, std::uint32_t microseconds) noexcept
{
    if (microseconds >= 1000000U) { text.decimal(microseconds, 1000000U).add("s/div"); }
    else if (microseconds >= 1000U) { text.decimal(microseconds, 1000U).add("ms/div"); }
    else { text.number(microseconds).add("us/div"); }
}
const char* mode_name(TriggerMode mode) noexcept
{
    return mode == TriggerMode::Auto ? "AUTO" : mode == TriggerMode::Normal ? "NORMAL" : "SINGLE";
}
const char* coupling_name(InputCoupling coupling) noexcept
{
    return coupling == InputCoupling::Dc ? "DC" : coupling == InputCoupling::Ac ? "AC" :
        coupling == InputCoupling::Ground ? "GND" : "?";
}
const char* storage_action_name(StorageAction action) noexcept
{
    return compact_label(StorageActionPool, StorageActionOffsets, static_cast<std::size_t>(action), 3U);
}
const char* storage_status_name(UiStorageStatus status) noexcept
{
    const auto index = static_cast<std::size_t>(status);
    return StorageStatusPool + StorageStatusOffsets[index < sizeof(StorageStatusOffsets) ? index : 1U];
}
const char* calibration_action_name(CalibrationAction action) noexcept
{
    switch (action) {
    case CalibrationAction::Begin: return "BEGIN";
    case CalibrationAction::AcceptReference: return "ACCEPT REF";
    case CalibrationAction::Install: return "INSTALL";
    default: return "ABORT";
    }
}
const char* measurement_view_name(MeasurementView view) noexcept
{
    switch (view) {
    case MeasurementView::Overview: return "OVERVIEW";
    case MeasurementView::Amplitude: return "AMPLITUDE";
    case MeasurementView::Rms: return "RMS/MEAN";
    default: return "FREQ";
    }
}
bool same_physical(const PhysicalSettings& a, const PhysicalSettings& b) noexcept
{
    return a.generation == b.generation && a.microvolts_per_div == b.microvolts_per_div && a.coupling == b.coupling;
}
const char* frequency_state(FrequencyStatus status) noexcept
{
    return compact_label(FrequencyStatePool, FrequencyStateOffsets, static_cast<std::size_t>(status), 7U);
}
void microhertz(Text& text, std::uint64_t value) noexcept;
void frequency(Text& text, const MeasurementResult& result) noexcept
{
    if (result.frequency_status != FrequencyStatus::Valid) { text.add("-- ").add(frequency_state(result.frequency_status)); return; }
    microhertz(text, result.frequency_microhertz);
}
void microhertz(Text& text, std::uint64_t value) noexcept
{
    if (value >= 1000000000000ULL) { text.decimal(value, 1000000000000ULL).add("MHz"); }
    else if (value >= 1000000U) { text.decimal(value, 1000000U).add("Hz"); }
    else if (value >= 1000U) { text.decimal(value, 1000U).add("mHz"); }
    else { text.number(static_cast<std::uint32_t>(value)).add("uHz"); }
}
const char* calibration_stage(CalibrationSessionState state) noexcept
{
    return compact_label(CalibrationStagePool, CalibrationStageOffsets, static_cast<std::size_t>(state), 0U);
}
const char* calibration_failure(CalibrationStatus status_value) noexcept
{
    const auto status = static_cast<std::size_t>(status_value);
    const auto index = status >= 5U && status <= 10U ? status - 5U : 6U;
    return compact_label(CalibrationFailurePool, CalibrationFailureOffsets, index, 6U);
}
const char* calibration_failure(const CalibrationSession& session) noexcept
{
    return calibration_failure(session.fit_status());
}
}

ScopeController::ScopeController(DmaAcquisition& acquisition, ScopeScreen& screen, UiControllerConfig config) noexcept
    : acquisition_{acquisition}, screen_{screen}, config_{config}, ui_{ScopeTimebaseCount, config.startup_run}
{
    screen_.invalidate();
    // Reinitialize the renderer's trivial composition object in its existing
    // storage. Placement construction allocates nothing and avoids a second
    // 296-byte temporary on the constrained target stack.
    static_assert(std::is_trivially_destructible<ScreenSnapshot>::value, "Review composition lifetime reset");
    ::new (static_cast<void*>(&screen_.view_)) ScreenSnapshot{};
    if (config_.storage != nullptr && config_.storage->state() != ScopeStorageState::Disabled) {
        storage_status_ = UiStorageStatus::Idle;
    }
    if (config_.calibration_bank != nullptr && config_.calibration_bank->board_identity() != 0U) {
        calibration_status_ = UiCalibrationStatus::Ready;
    }
    refresh_backend();
}

void ScopeController::refresh_backend() noexcept
{
    if (planned_index_ != ui_.settings().timebase_index) {
        config_.acquisition_input.set(true);
        planned_index_ = ui_.settings().timebase_index;
        if (!plan_scope_timebase(planned_index_, plan_)) { plan_.support = ScopeTimebaseSupport::Invalid; }
        config_.acquisition_input.set(false);
    }
    const bool boot_storage = storage_status_ != UiStorageStatus::Disabled &&
        (storage_boot_pending_ || storage_boot_active_ || storage_boot_waiting_input_);
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
    if (host_request_ != 0U && ui_.request_id() != prior_request) { cancel_host_job(HostCaptureState::Cancelled); }
    buttons(update.buttons, now_ms);
    if (storage_boot_waiting_input_ && update.ready && update.buttons.held == 0U &&
        !update.buttons.discontinuity) {
        storage_boot_waiting_input_ = false;
        refresh_backend();
    }
}

void ScopeController::buttons(const ButtonEvents& event, std::uint32_t now_ms) noexcept
{
    if (storage_busy()) {
        ui_.buttons({0U, 0U, event.held, true}, now_ms);
        return;
    }
    const auto prior_settings = ui_.settings();
    const auto prior_request = ui_.request_id();
    ui_.buttons(event, now_ms);
    const auto& next_settings = ui_.settings();
    if (event.pressed != 0U || event.discontinuity || prior_settings.timebase_index != next_settings.timebase_index ||
        prior_settings.mode != next_settings.mode || prior_settings.edge != next_settings.edge ||
        prior_settings.trigger_level_code != next_settings.trigger_level_code ||
        prior_settings.trigger_position_permille != next_settings.trigger_position_permille ||
        prior_settings.vertical_offset_pixels != next_settings.vertical_offset_pixels ||
        prior_settings.horizontal_offset_samples != next_settings.horizontal_offset_samples ||
        prior_settings.measurement_view != next_settings.measurement_view || ui_.request_id() != prior_request) {
        invalidate_export();
        supersede_host_job();
    }
    if (host_request_ != 0U && (ui_.request_id() != host_request_ || ui_.state() == UiRunState::Hold)) {
        cancel_host_job(HostCaptureState::Cancelled);
    }
    // Fault retry is permitted only after the UI accepted a fresh OK gesture.
    if (fault_ != UiControllerFault::None && ui_.state() != UiRunState::Fault) { fault_ = UiControllerFault::None; }
}

void ScopeController::invalidate_export() noexcept
{
    export_active_ = false;
    export_token_ = 0U;
}

void ScopeController::cancel_host_job(HostCaptureState state) noexcept
{
    if (host_request_ == 0U) { return; }
    host_state_ = state;
    host_request_ = 0U;
}

void ScopeController::supersede_host_job() noexcept
{
    if (host_request_ != 0U) { cancel_host_job(HostCaptureState::Cancelled); }
    else if (host_state_ == HostCaptureState::Complete) { host_state_ = HostCaptureState::Cancelled; }
}

bool ScopeController::begin_export(CaptureExportView& view) noexcept
{
    if (export_active_ || !frame_valid_ || frame_.capture.length == 0U || ui_.state() != UiRunState::Hold || ui_.wants_run() ||
        acquisition_.state() != AcquisitionState::Stopped || pending_consumption_ || work_ != UiWork::Idle ||
        storage_busy() || ui_.has_pending_operation() || host_request_ != 0U) { return false; }
    export_generation_ = next_panel_generation(export_generation_);
    export_token_ = export_generation_;
    export_active_ = true;
    view = CaptureExportView{frame_, frozen_calibration_, export_token_};
    return true;
}

bool ScopeController::export_sample(std::uint32_t token, std::uint16_t index, std::uint16_t& out) const noexcept
{
    if (!export_active_ || token == 0U || token != export_token_ || !frame_valid_ || index >= frame_.capture.length) { return false; }
    out = samples_[index];
    return true;
}

void ScopeController::end_export(std::uint32_t token) noexcept
{
    if (export_active_ && token != 0U && token == export_token_) { invalidate_export(); }
}

std::uint32_t ScopeController::request_host_single() noexcept
{
    if (!config_.acquisition_enabled || !ui_.physical_ready() || ui_.capture_inhibited() ||
        ui_.state() != UiRunState::Hold || ui_.wants_run() || export_active_ || acquisition_running() || pending_consumption_ ||
        work_ != UiWork::Idle || storage_busy() || ui_.has_pending_operation() || host_request_ != 0U) { return 0U; }
    const auto request = ui_.request_host_single();
    if (request == 0U) { return 0U; }
    invalidate_export();
    host_job_ = next_panel_generation(host_job_);
    host_request_ = request;
    host_state_ = HostCaptureState::Pending;
    return host_job_;
}

HostCaptureState ScopeController::host_capture_state(std::uint32_t job) const noexcept
{
    return job != 0U && job == host_job_ ? host_state_ : HostCaptureState::None;
}

bool ScopeController::cancel_host_capture(std::uint32_t job) noexcept
{
    if (job == 0U || job != host_job_ || host_state_ != HostCaptureState::Pending || host_request_ == 0U ||
        !ui_.cancel_host_single(host_request_)) { return false; }
    cancel_host_job(HostCaptureState::Cancelled);
    invalidate_export();
    return true;
}

bool ScopeController::sample(std::uint16_t index, std::uint16_t& out) const noexcept
{
    if (!frame_valid_ || index >= frame_.capture.length) { return false; }
    out = samples_[index]; return true;
}

bool ScopeController::acquisition_running() const noexcept
{
    return acquisition_.state() == AcquisitionState::Running;
}

const MeasurementResult* ScopeController::measurements() const noexcept
{
    const auto* result = measurement_.result();
    return frame_valid_ && result != nullptr && result->acquisition_epoch == frame_.acquisition_epoch ? result : nullptr;
}

bool ScopeController::calibration_point(std::int64_t reference_microvolts, CalibrationPoint& point) const noexcept
{
    const auto* result = measurements();
    if (storage_busy() || result == nullptr || work_ != UiWork::Idle || ui_.wants_run() || ui_.state() != UiRunState::Hold ||
        acquisition_running() || pending_consumption_ || acquisition_.state() != AcquisitionState::Stopped ||
        !ui_.physical_ready() || !same_physical(frame_.physical, ui_.physical()) ||
        frame_.physical.coupling != InputCoupling::Dc || result->adc_clipped) { return false; }
    const CalibrationPoint candidate{frame_.physical, frame_.acquisition_epoch, result->mean_code_q16,
        reference_microvolts, result->count, result->minimum_code, result->maximum_code,
        static_cast<std::uint16_t>(result->rail_low_samples + result->rail_high_samples)};
    if (!calibration_point_valid(candidate)) { return false; }
    point = candidate;
    return true;
}

void ScopeController::cancel_consumers() noexcept
{
    screen_.cancel();
    projector_.cancel();
    spectrum_.cancel();
    if (spectrum_workspace_leased_) {
        acquisition_.end_spectrum_workspace();
        spectrum_workspace_leased_ = false;
    }
    work_ = UiWork::Idle;
}

void ScopeController::publish_fault(UiControllerFault reason) noexcept
{
    fault_ = reason;
    projection_.available = false;
    ui_.on_presentation_fault();
}

bool ScopeController::cancel_acquisition() noexcept
{
    // Partial copy must never become a valid record. Completed copied frames
    // survive cancellation and remain available for HOLD/view changes.
    cancel_consumers();
    if (measurement_.state() == MeasurementState::Statistics || measurement_.state() == MeasurementState::Frequency) {
        measurement_.cancel();
    }
    const bool failed_running_stop = acquisition_.stop() == AcquisitionStopResult::Failed;
    static_cast<void>(acquisition_.release());
    pending_consumption_ = false;
    active_request_ = 0U;
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
    if (host_request_ != request && host_state_ == HostCaptureState::Complete) { host_state_ = HostCaptureState::Cancelled; }
    invalidate_export();
    if (!config_.acquisition_enabled || plan_.support != ScopeTimebaseSupport::CircularDma ||
        ui_.physical().microvolts_per_div < 10000U || ui_.physical().microvolts_per_div > 5000000U) {
        fail(UiControllerFault::Configuration); return;
    }
    if (!cancel_acquisition()) { return; }
    const auto& settings = ui_.settings();
    // A measured fit in the board-bound RAM bank always takes precedence. The
    // callback fills a missing profile; only without a bank may it supply a
    // measured profile under the legacy standalone resolver contract.
    active_calibration_ = config_.calibration_bank != nullptr ?
        config_.calibration_bank->resolve(ui_.physical()) : VoltageCalibration{};
    if (active_calibration_.kind == CalibrationKind::Unavailable && config_.calibration.resolve != nullptr) {
        const auto fallback = config_.calibration.resolve(config_.calibration.context, ui_.physical());
        if (config_.calibration_bank != nullptr && fallback.kind == CalibrationKind::Measured) {
            fail(UiControllerFault::Configuration); return;
        }
        active_calibration_ = fallback;
    }
    if (active_calibration_.kind != CalibrationKind::Unavailable && !calibration_valid(active_calibration_)) {
        fail(UiControllerFault::Configuration); return;
    }
    const auto capacity = CaptureCapacity;
    const auto pretrigger = static_cast<std::uint16_t>(std::uint32_t{settings.trigger_position_permille} * (capacity - 1U) / 1000U);
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
    const auto capture_mode = host_request_ == request ? TriggerMode::Single : settings.mode;
    const AcquisitionRequest acquisition_request{
        {CaptureCapacity, pretrigger, settings.trigger_level_code, hysteresis, capture_mode, settings.edge,
         capture_mode == TriggerMode::Auto ? timeout : 0U},
        ui_.physical(), plan_.timing, {period, safety, poll_cycles, 2U}, request};
    active_request_ = request;
    // Keep the completed physical trace throughout acquisition. The projection
    // is logically unavailable until the next frozen record is projected;
    // neither label formatting nor an LCD transfer may delay the fast producer.
    projection_.available = false;
    config_.acquisition_input.set(true);
    const bool started = acquisition_.start(acquisition_request);
    config_.acquisition_input.set(false);
    if (!started || !ui_.on_capture_started(request)) { fail(UiControllerFault::Acquisition); }
}

void ScopeController::begin_copy() noexcept
{
    invalidate_export();
    const auto* metadata = acquisition_.metadata();
    if (metadata == nullptr || active_request_ == 0U || metadata->acquisition_epoch != active_request_ ||
        metadata->capture.length > samples_.size() || !ui_.on_frame_ready(active_request_)) { fail(UiControllerFault::Copy); return; }
    cancel_consumers(); // Before changing the source/projection borrowed by any consumer.
    measurement_.cancel();
    frame_valid_ = false;
    frame_ = *metadata;
    frozen_calibration_ = active_calibration_;
    frame_request_ = active_request_;
    pending_consumption_ = true;
    copied_ = 0U;
    work_ = UiWork::Copying;
}

void ScopeController::poll_acquisition() noexcept
{
    acquisition_.poll();
    if (ui_.state() == UiRunState::Acquiring && acquisition_.state() == AcquisitionState::Fault) {
        fail(UiControllerFault::Acquisition); return;
    }
    if (acquisition_.state() == AcquisitionState::Ready && !pending_consumption_) { begin_copy(); }
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
    projection_.available = false;
    if (!frame_valid_ || frame_.capture.length < SpectrumLength ||
        frame_.timing.timer_hz == 0U || frame_.timing.divider == 0U || acquisition_running()) {
        work_ = UiWork::Idle;
        present();
        return;
    }
    const auto maximum_start = static_cast<std::uint16_t>(frame_.capture.length - SpectrumLength);
    const auto centered = frame_.capture.pretrigger > SpectrumLength / 2U ?
        static_cast<std::uint16_t>(frame_.capture.pretrigger - SpectrumLength / 2U) : std::uint16_t{0U};
    const auto start = centered > maximum_start ? maximum_start : centered;
    std::int32_t* workspace{};
    if (!acquisition_.begin_spectrum_workspace(workspace)) { fail(UiControllerFault::Spectrum); return; }
    spectrum_workspace_leased_ = true;
    if (!spectrum_.begin(samples_.data(), frame_.capture.length, start, frame_, workspace,
                         static_cast<std::uint16_t>(DmaRingSamples / 2U), projection_)) {
        spectrum_.cancel();
        acquisition_.end_spectrum_workspace();
        spectrum_workspace_leased_ = false;
        fail(UiControllerFault::Spectrum);
        return;
    }
    work_ = UiWork::Spectrum;
}

void ScopeController::redraw() noexcept
{
    // Full label composition/reprojection is stopped-producer work. While a
    // fast record is pending, keep drawing the already composed scene. A new
    // record, HOLD/cancel or fault always requests a fresh composition.
    if (acquisition_.state() == AcquisitionState::Running) { return; }
    if (work_ == UiWork::Copying || work_ == UiWork::Measuring) { return; }
    cancel_consumers();
    revision_ = next_panel_generation(revision_);
    if (ui_.state() == UiRunState::Fault) { present(); return; }
    if (frame_valid_) {
        if (measurements() == nullptr) {
            if (!measurement_.begin(samples_.data(), frame_.capture.length, frame_, frozen_calibration_)) {
                fail(UiControllerFault::Measurement); return;
            }
            work_ = UiWork::Measuring;
            return;
        }
        if (ui_.domain() == Domain::Spectrum) { begin_spectrum(); return; }
        if (!projector_.begin(samples_.data(), frame_.capture.length, frame_, frozen_calibration_, view(), projection_)) {
            fail(UiControllerFault::Projection); return;
        }
        work_ = UiWork::Projecting;
    } else { projection_.available = false; present(); }
}

void ScopeController::present() noexcept
{
    format_scene();
    if (!config_.rendering_enabled) { work_ = UiWork::Idle; finish_presentation(); return; }
    if (!screen_.begin_composed(dirty_labels_)) { fail(UiControllerFault::Display); return; }
    dirty_labels_ = 0U;
    work_ = UiWork::Rendering;
}

void ScopeController::finish_presentation() noexcept
{
    if (!pending_consumption_) { return; }
    // Screen is Complete (or disabled); the projection remains private. Release
    // the acquisition record only after all copy/projection consumers finish.
    const bool released = acquisition_.release();
    if (!released) { fail(UiControllerFault::Copy); return; }
    pending_consumption_ = false;
    static_cast<void>(ui_.on_frame_consumed(frame_request_));
    if (host_request_ != 0U && frame_request_ == host_request_) { cancel_host_job(HostCaptureState::Complete); }
}

bool ScopeController::stored_calibration(void* context, std::uint16_t offset, std::uint8_t* output, std::uint16_t count) noexcept
{ return static_cast<ScopeStorage*>(context)->read_selected_calibration(offset, output, count); }

bool ScopeController::staged_calibration(void* context, std::uint16_t offset, std::uint8_t* output, std::uint16_t count) noexcept
{
    const auto& self = *static_cast<ScopeController*>(context);
    if (self.acquisition_.workspace_owner_ != DmaAcquisition::WorkspaceOwner::Recall || output == nullptr || offset > CalibrationSupportedRecordMaximum ||
        count > CalibrationSupportedRecordMaximum - offset) { return false; }
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(self.acquisition_.workspace_.dma.samples);
    for (std::uint16_t i{}; i < count; ++i) { output[i] = bytes[offset + i]; }
    return true;
}

void ScopeController::finish_storage(UiStorageStatus status) noexcept
{
    const bool boot = storage_boot_active_;
    storage_boot_active_ = false;
    acquisition_.end_recall_staging();
    storage_phase_ = StoragePhase::Idle;
    storage_command_ = StorageCommand::None;
    storage_status_ = status;
    if (config_.storage_input.pause != nullptr) { config_.storage_input.pause(config_.storage_input.context, false); }
    static_cast<void>(ui_.take_actions());
    if (boot && config_.storage_input.pause != nullptr) { storage_boot_waiting_input_ = true; }
    refresh_backend();
    redraw();
}

void ScopeController::start_storage(StorageCommand command) noexcept
{
    invalidate_export();
    if (command == StorageCommand::RestoreSettings || command == StorageCommand::RestoreCalibration) { calibration_session_.abort(); }
    if (config_.storage == nullptr || config_.storage->state() == ScopeStorageState::Disabled) {
        storage_status_ = UiStorageStatus::Disabled; redraw(); return;
    }
    if (command == StorageCommand::SaveCalibration || command == StorageCommand::RestoreCalibration) {
        if (config_.calibration_bank == nullptr || config_.calibration_bank->board_identity() == 0U) {
            storage_status_ = UiStorageStatus::Disabled; redraw(); return;
        }
        if (config_.calibration_bank->board_identity() != config_.storage->board_identity()) {
            storage_status_ = UiStorageStatus::Error; redraw(); return;
        }
    }
    const bool boot_open = storage_boot_active_ && command == StorageCommand::None;
    if ((!boot_open && (ui_.wants_run() || ui_.state() != UiRunState::Hold)) || acquisition_running() || pending_consumption_ ||
        (boot_open && (fault_ != UiControllerFault::None || ui_.state() == UiRunState::Fault || frame_valid_ || work_ != UiWork::Idle)) ||
        acquisition_.state() != AcquisitionState::Stopped ||
        work_ == UiWork::Copying || work_ == UiWork::Measuring) {
        if (boot_open) { finish_storage(UiStorageStatus::Error); }
        else { storage_status_ = UiStorageStatus::HoldRequired; redraw(); }
        return;
    }
    cancel_consumers();
    storage_command_ = command;
    if (config_.storage_input.pause != nullptr) {
        config_.storage_input.pause(config_.storage_input.context, true);
        panel_update(PanelUpdate{{}, {0U, 0U, DebouncedButtons::All, true}, false, true, false}, 0U);
    }
    if (!config_.storage->begin_open()) { finish_storage(UiStorageStatus::Error); return; }
    storage_phase_ = StoragePhase::Opening;
    storage_status_ = UiStorageStatus::Busy;
    // Freeze gestures before borrowing samples or a stopped DMA workspace.
    ui_.buttons({0U, 0U, 0U, true}, 0U);
    static_cast<void>(ui_.take_actions());
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
    if (ui_.wants_run() || ui_.state() != UiRunState::Hold || acquisition_running() || pending_consumption_ ||
        acquisition_.state() != AcquisitionState::Stopped ||
        work_ == UiWork::Copying || work_ == UiWork::Measuring) {
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

void ScopeController::service_storage() noexcept
{
    auto& store = *config_.storage;
    static_cast<void>(ui_.take_actions()); // Physical updates cannot arm during storage.
    if (storage_phase_ == StoragePhase::Opening || storage_phase_ == StoragePhase::Saving) {
        const auto progress = store.step(16U);
        if (progress.state == ScopeStorageState::Error) { finish_storage(UiStorageStatus::Error); return; }
        if (progress.state != ScopeStorageState::Ready) { return; }
        if (storage_phase_ == StoragePhase::Saving) { finish_storage(UiStorageStatus::Saved); return; }
        const auto* old = store.record();
        const bool boot = storage_command_ == StorageCommand::None && storage_boot_active_;
        const bool boot_bank = storage_command_ == StorageCommand::None && old != nullptr && old->calibration_bytes != 0U &&
            config_.calibration_bank != nullptr && config_.calibration_bank->board_identity() != 0U;
        if ((storage_command_ == StorageCommand::None && !boot_bank) || storage_command_ == StorageCommand::RestoreSettings) {
            if (old == nullptr) { finish_storage(UiStorageStatus::Empty); return; }
            const bool restored = boot ? ui_.restore_boot_settings(old->settings) : ui_.restore_settings(old->settings);
            finish_storage(restored ? UiStorageStatus::Restored : UiStorageStatus::Error); return;
        }
        if (storage_command_ == StorageCommand::RestoreCalibration || boot_bank) {
            if (old == nullptr || old->calibration_bytes == 0U) { finish_storage(UiStorageStatus::Empty); return; }
            if (boot_bank && config_.calibration_bank->board_identity() != store.board_identity()) { finish_storage(UiStorageStatus::Error); return; }
            if (!acquisition_.begin_recall_staging()) { finish_storage(UiStorageStatus::Error); return; }
            copied_ = 0U; storage_phase_ = StoragePhase::Reading; return;
        }
        if (!store.writable()) { finish_storage(UiStorageStatus::ReadOnly); return; }
        SavedRecord record{};
        SavedByteSource calibration_source{};
        if (old != nullptr) {
            record.settings = old->settings;
            record.calibration_bytes = old->calibration_bytes;
            if (record.calibration_bytes != 0U) { calibration_source = {&store, stored_calibration}; }
        }
        record.board_identity = store.board_identity();
        if (storage_command_ == StorageCommand::SaveSettings || old == nullptr) { record.settings = ui_.settings(); }
        if (storage_command_ == StorageCommand::SaveCalibration) {
            if (!acquisition_.begin_recall_staging()) { finish_storage(UiStorageStatus::Error); return; }
            const auto count = config_.calibration_bank->export_record(reinterpret_cast<std::uint8_t*>(acquisition_.workspace_.dma.samples), CalibrationSupportedRecordMaximum);
            if (count == 0U) { finish_storage(UiStorageStatus::Error); return; }
            record.calibration_bytes = static_cast<std::uint16_t>(count);
            calibration_source = {this, staged_calibration};
        }
        if (!store.begin_save(record, calibration_source)) { finish_storage(UiStorageStatus::Error); return; }
        storage_phase_ = StoragePhase::Saving; return;
    }
    const auto* record = store.record();
    if (record == nullptr) { finish_storage(UiStorageStatus::Error); return; }
    if (storage_phase_ == StoragePhase::Reading) {
        const auto remaining = static_cast<std::uint16_t>(record->calibration_bytes - copied_);
        const auto count = static_cast<std::uint16_t>(remaining > 32U ? 32U : remaining);
        auto* bytes = reinterpret_cast<std::uint8_t*>(acquisition_.workspace_.dma.samples) + copied_;
        if (count != 0U && !store.read_selected_calibration(copied_, bytes, count)) { finish_storage(UiStorageStatus::Error); return; }
        copied_ = static_cast<std::uint16_t>(copied_ + count);
        if (copied_ != record->calibration_bytes) { return; }
        const auto* staged = reinterpret_cast<const std::uint8_t*>(acquisition_.workspace_.dma.samples);
        const bool boot = storage_command_ == StorageCommand::None;
        auto& bank = *config_.calibration_bank;
        if (!calibration_record_valid(staged, record->calibration_bytes, bank.board_identity()) ||
            (boot && !ui_.can_restore_boot_settings(record->settings))) { finish_storage(UiStorageStatus::Error); return; }
        if (!bank.import_record(staged, record->calibration_bytes) || (boot && !ui_.restore_boot_settings(record->settings))) {
            finish_storage(UiStorageStatus::Error); return;
        }
        calibration_status_ = UiCalibrationStatus::Updated;
        finish_storage(UiStorageStatus::Restored); return;
    }
}

UiServiceResult ScopeController::service() noexcept
{
    UiServiceResult result{work_, 0U, {projector_.state(), 0U, 0U}, {screen_.state(), 0U, 0U},
        {measurement_.state(), 0U}};
    if (storage_boot_pending_) {
        storage_boot_pending_ = false;
        if (storage_status_ != UiStorageStatus::Disabled) {
            // RUN intent stays visible, but refresh_backend inhibited capture
            // before any panel input or acquisition request can reach hardware.
            storage_boot_active_ = true;
            start_storage(StorageCommand::None);
        }
    }
    if (storage_busy()) { service_storage(); result.work = work_; return result; }
    const auto pending = ui_.take_actions();
    if (pending.cancel_acquisition && !cancel_acquisition()) { result.work = work_; return result; }
    // Timer factorization is configuration work, never work done while a
    // canceled producer is still running. Reconcile the backend before arm.
    refresh_backend();
    const auto actions = ui_.take_actions();
    if (actions.cancel_acquisition && !cancel_acquisition()) { result.work = work_; return result; }
    const auto storage = pending.storage != StorageCommand::None ? pending.storage : actions.storage;
    if (storage != StorageCommand::None) {
        start_storage(storage);
        result.work = work_; return result;
    }
    const auto calibration = pending.calibration.command != CalibrationCommand::None ? pending.calibration : actions.calibration;
    if (calibration.command != CalibrationCommand::None) { calibrate(calibration); result.work = work_; return result; }
    if (actions.arm) { arm(actions.request_id); }
    else if (pending.arm && pending.request_id == ui_.request_id() &&
             ui_.state() == UiRunState::ArmPending && !ui_.capture_inhibited()) { arm(pending.request_id); }
    if ((pending.redraw || actions.redraw) && fault_ != UiControllerFault::Display) { redraw(); }
    const auto prior_blocks = acquisition_.statistics().blocks_committed;
    poll_acquisition();
    // A leased half has just been copied and processed. Give the next DMA
    // poll its full budget instead of combining that work with LCD transfer.
    if (acquisition_.state() == AcquisitionState::Running &&
        acquisition_.statistics().blocks_committed != prior_blocks) { result.work = work_; return result; }

    if (work_ == UiWork::Copying) {
        for (unsigned i{}; i < 64U && copied_ < frame_.capture.length; ++i) {
            std::uint16_t value{};
            const bool read = acquisition_.read(copied_, value);
            if (!read) {
                fail(UiControllerFault::Copy);
                break;
            }
            samples_[copied_++] = value; ++result.copied;
        }
        if (work_ == UiWork::Copying && copied_ == frame_.capture.length) {
            frame_valid_ = true; work_ = UiWork::Idle; redraw();
        }
    } else if (work_ == UiWork::Measuring) {
        result.measurement = measurement_.step(32U);
        if (result.measurement.state == MeasurementState::Complete) { work_ = UiWork::Idle; redraw(); }
        else if (result.measurement.state != MeasurementState::Statistics && result.measurement.state != MeasurementState::Frequency) {
            fail(UiControllerFault::Measurement);
        }
    } else if (work_ == UiWork::Spectrum) {
        const auto spectrum_state = spectrum_.step(8U);
        if (spectrum_state == SpectrumState::Complete) {
            if (spectrum_workspace_leased_) {
                acquisition_.end_spectrum_workspace();
                spectrum_workspace_leased_ = false;
            }
            work_ = UiWork::Idle;
            present();
        } else if (spectrum_state == SpectrumState::Invalid || spectrum_state == SpectrumState::Cancelled ||
                   spectrum_state == SpectrumState::Idle) { fail(UiControllerFault::Spectrum); }
    } else if (work_ == UiWork::Projecting) {
        result.projection = projector_.step(8U, 8U);
        if (result.projection.state == ProjectionState::Complete) { present(); }
        else if (result.projection.state == ProjectionState::Invalid) { fail(UiControllerFault::Projection); }
    } else if (work_ == UiWork::Rendering) {
        // Fast DMA has a half-buffer-derived poll deadline. A 64-pixel
        // GPIO stripe exceeded that deadline on the bench. Keep UI progress
        // bounded more tightly while the producer runs; stopped/slow paths
        // retain the larger stripe. Hardware polling guards remain unchanged.
        const auto pixels = static_cast<std::uint16_t>(acquisition_.state() == AcquisitionState::Running ? 16U : 64U);
        result.render = screen_.step(revision_, {pixels, 1U});
        if (result.render.state == RenderState::Complete) { work_ = UiWork::Idle; finish_presentation(); }
        else if (result.render.state != RenderState::Drawing) { fail(UiControllerFault::Display); }
    }
    result.work = work_;
    return result;
}

void ScopeController::format_measurements() noexcept
{
    auto& scene = screen_.view_;
    Text text{scene.record, &dirty_labels_, 64U, &screen_.text_ranges_[6U]};
    const auto* result = measurements();
    if (result == nullptr) { text.add("No measurements"); return; }
    if (result->adc_clipped) {
        text.add("ADC CLIP: rails ").number(result->rail_low_samples + result->rail_high_samples);
        text.add("  F -- CLIP; volts invalid");
        return;
    }
    const auto page = ui_.settings().measurement_view;
    if (page == MeasurementView::Frequency) {
        text.add("F "); frequency(text, *result);
        text.add("  periods ").number(result->periods);
        if (result->periods != 0U) {
            text.add("  P ").decimal(result->minimum_period_q16, 65536U).add("..");
            text.decimal(result->maximum_period_q16, 65536U).add(" samples");
        }
        return;
    }
    if (page == MeasurementView::Amplitude) {
        if (result->voltage_available) {
            text.add("PP "); voltage(text, static_cast<std::int64_t>(result->peak_to_peak_microvolts));
        } else {
            text.add("PP ").number(result->peak_to_peak_codes).add("c");
        }
        return;
    }
    if (page == MeasurementView::Rms) {
        if (result->voltage_available) {
            text.add("RMS "); voltage(text, static_cast<std::int64_t>(result->rms_microvolts));
            text.add(" MEAN "); voltage(text, result->mean_microvolts);
        } else {
            text.add("ADC RMS ").decimal(result->rms_code_q16, 65536U).add("c AVG ")
                .decimal(result->mean_code_q16, 65536U).add("c PP ").number(result->peak_to_peak_codes).add("c");
        }
        return;
    }
    text.add("F "); frequency(text, *result);
    if (result->voltage_available) {
        text.add(" RMS "); voltage(text, static_cast<std::int64_t>(result->rms_microvolts));
        text.add(" PP "); voltage(text, static_cast<std::int64_t>(result->peak_to_peak_microvolts));
    } else {
        text.add(" ADC RMS ").decimal(result->rms_code_q16, 65536U).add("c PP ").number(result->peak_to_peak_codes).add("c");
    }
}

void ScopeController::format_scene() noexcept
{
    // The renderer owns this composition buffer. Release its active read before
    // changing any field; the attached foreground controller is its sole writer.
    screen_.cancel();
    auto& scene = screen_.view_;
    // Reset fields in place. A whole-aggregate temporary costs another 296B
    // stack frame in unoptimized target builds; every label is replaced below.
    scene.zero_y = -1; scene.trigger_y = -1; scene.trigger_x = -1;
    scene.zero_label[0] = '\0';
    scene.highlight = ScreenHighlight::None;
    scene.epoch = revision_;
    scene.waveform = frame_valid_ && projection_.available ? &projection_ : nullptr;
    const auto state = ui_.state();
    const char* status = !ui_.wants_run() ? "HOLD" : (state == UiRunState::Acquiring || state == UiRunState::ArmPending) ? "RUN" :
        state == UiRunState::FrameReady ? "FRAME" : state == UiRunState::WaitingForControls ? "WAIT INPUT" :
        state == UiRunState::WaitingForBackend ? "WAIT BACK" : "ARM";
    if (state == UiRunState::Fault) { status = "FAULT"; }
    Text{scene.status, &dirty_labels_, 1U, &screen_.text_ranges_[0U]}.add(status);
    const auto kind = frame_valid_ ? frozen_calibration_.kind : CalibrationKind::Unavailable;
    const auto& settings = ui_.settings();
    const bool spectrum = ui_.domain() == Domain::Spectrum;
    if (spectrum) {
        const auto* spectrum_result = spectrum_.result();
        Text quality{scene.quality, &dirty_labels_, 2U, &screen_.text_ranges_[1U]};
        quality.add("FFT");
        Text{scene.channel, &dirty_labels_, 4U, &screen_.text_ranges_[2U]}.add("FFT 512 HANN AC");
        Text{scene.timebase, &dirty_labels_, 8U, &screen_.text_ranges_[3U]}.add("dBFS ADC peak: 2048");
        Text peak{scene.trigger, &dirty_labels_, 16U, &screen_.text_ranges_[4U]};
        if (spectrum_result != nullptr && spectrum_result->available && spectrum_result->peak_available) {
            peak.add("Pk "); microhertz(peak, spectrum_.peak_frequency_microhertz()); peak.add(" ");
            const auto level = static_cast<std::int32_t>(spectrum_result->peak_dbfs_x100);
            const auto magnitude = static_cast<std::uint32_t>(level < 0 ? -level : level);
            if (level < 0) { peak.add("-"); }
            peak.decimal(magnitude, 100U).add("dBFS");
        } else { peak.add("Peak --"); }
        if (spectrum_result != nullptr && spectrum_result->available) {
            Text sampling{scene.sampling, &dirty_labels_, 32U, &screen_.text_ranges_[5U]};
            sampling.add("df "); microhertz(sampling, spectrum_result->bin_spacing_microhertz);
            Text span{scene.record, &dirty_labels_, 64U, &screen_.text_ranges_[6U]};
            span.add("0.. "); microhertz(span, spectrum_result->nyquist_microhertz); span.add(" 10dB/div");
            Text{scene.zero_label}.add("0dB");
            scene.zero_y = static_cast<std::int16_t>(SpectrumPlotTop);
        } else {
            Text{scene.sampling, &dirty_labels_, 32U, &screen_.text_ranges_[5U]}.add("df --");
            Text record{scene.record, &dirty_labels_, 64U, &screen_.text_ranges_[6U]};
            if (!frame_valid_) { record.add("FFT NEEDS FROZEN FRAME"); }
            else if (acquisition_running()) { record.add("FFT NEEDS HELD UNIFORM BLOCK"); }
            else if (frame_.capture.length < SpectrumLength) { record.add("FFT NEEDS 512 SAMPLES"); }
            else if (frame_.timing.timer_hz == 0U || frame_.timing.divider == 0U) { record.add("FFT TIMING UNKNOWN"); }
            else { record.add("FFT UNAVAILABLE"); }
            scene.zero_label[0] = '\0';
        }
        scene.trigger_y = -1; scene.trigger_x = -1;
    } else {
    Text{scene.quality, &dirty_labels_, 2U, &screen_.text_ranges_[1U]}.add(kind == CalibrationKind::Measured ? "CAL" : kind == CalibrationKind::Nominal ? "NOM" : "RAW");
    const auto& physical = frame_valid_ ? frame_.physical : ui_.physical();
    Text channel{scene.channel, &dirty_labels_, 4U, &screen_.text_ranges_[2U]}; channel.add(coupling_name(physical.coupling)).add(" ");
    if (kind == CalibrationKind::Unavailable && physical.microvolts_per_div != 0U) { channel.add("SEN "); }
    if (physical.microvolts_per_div != 0U) { voltage(channel, physical.microvolts_per_div); channel.add("/div"); }
    else { channel.add("RANGE UNKNOWN"); }
    Text time{scene.timebase, &dirty_labels_, 8U, &screen_.text_ranges_[3U]}; timebase(time, scope_timebase_us(ui_.settings().timebase_index));
    const auto mode = frame_valid_ ? frame_.capture.mode : settings.mode;
    const auto edge = frame_valid_ ? frame_.capture.edge : settings.edge;
    const auto level = frame_valid_ ? frame_.capture.level : settings.trigger_level_code;
    Text trigger{scene.trigger, &dirty_labels_, 16U, &screen_.text_ranges_[4U]}; trigger.add(mode_name(mode)).add(edge == TriggerEdge::Rising ? " R C" : " F C").number(level);
    if (frame_valid_) { trigger.add(frame_.edge_triggered ? " TRIG" : " FREE"); }
    if (frame_valid_) {
        Text sampling{scene.sampling, &dirty_labels_, 32U, &screen_.text_ranges_[5U]};
        const auto divider = std::uint64_t{frame_.timing.divider};
        if (divider >= 14062500U) { sampling.add("dt ").decimal(divider * 1000U, frame_.timing.timer_hz).add("ms"); }
        else if (frame_.timing.timer_hz >= divider * 1000U) { sampling.decimal(frame_.timing.timer_hz, divider * 1000U).add("kS/s"); }
        else { sampling.decimal(frame_.timing.timer_hz, divider).add("S/s"); }
        format_measurements();
        std::int32_t zero{}, threshold{};
        const auto viewport = view();
        if (kind == CalibrationKind::Unavailable) {
            Text{scene.zero_label}.add("REF");
            if (project_code_y(2048U, frame_, frozen_calibration_, viewport, zero) && zero >= 0 && zero < ScopePlotHeight) {
                scene.zero_y = static_cast<std::int16_t>(zero);
            }
        } else {
            Text{scene.zero_label}.add("0V");
            zero = ScopePlotHeight / 2 + settings.vertical_offset_pixels;
            if (zero >= 0 && zero < ScopePlotHeight) { scene.zero_y = static_cast<std::int16_t>(zero); }
        }
        if (project_code_y(level, frame_, frozen_calibration_, viewport, threshold) && threshold >= 0 && threshold < ScopePlotHeight) {
            scene.trigger_y = static_cast<std::int16_t>(threshold);
        }
        std::int64_t trigger_x{};
        if (project_trigger_x(frame_, viewport, trigger_x) && trigger_x >= 0 && trigger_x <= ScopePlotWidth) {
            scene.trigger_x = static_cast<std::int16_t>(trigger_x == ScopePlotWidth ? ScopePlotWidth - 1U : trigger_x);
        }
    } else { Text{scene.sampling, &dirty_labels_, 32U, &screen_.text_ranges_[5U]}.add("NO RECORD"); Text{scene.record, &dirty_labels_, 64U, &screen_.text_ranges_[6U]}.add("No record"); }
    }
    Text focus{scene.focus, &dirty_labels_, 128U, &screen_.text_ranges_[7U]};
    switch (ui_.focus()) {
    case UiFocus::Timebase: focus.add("TIME "); timebase(focus, scope_timebase_us(settings.timebase_index)); scene.highlight = ScreenHighlight::Timebase; break;
    case UiFocus::TriggerMode: focus.add("NEXT MODE ").add(mode_name(settings.mode)); scene.highlight = ScreenHighlight::Trigger; break;
    case UiFocus::TriggerEdge: focus.add("NEXT EDGE ").add(settings.edge == TriggerEdge::Rising ? "RISING" : "FALLING"); scene.highlight = ScreenHighlight::Trigger; break;
    case UiFocus::TriggerLevel: focus.add("NEXT LEVEL CODE ").number(settings.trigger_level_code); scene.highlight = ScreenHighlight::Trigger; break;
    case UiFocus::TriggerPosition: focus.add("TRIG POS ").decimal(settings.trigger_position_permille, 10U).add("%");
        scene.highlight = ScreenHighlight::Position; break;
    case UiFocus::VerticalPosition: focus.add("VERT ").signed_number(settings.vertical_offset_pixels).add("px"); scene.highlight = ScreenHighlight::Position; break;
    case UiFocus::HorizontalPosition: focus.add("PAN ").signed_number(settings.horizontal_offset_samples).add(" samples"); scene.highlight = ScreenHighlight::Position; break;
    case UiFocus::Measurements:
        focus.add("MEAS ").add(measurement_view_name(settings.measurement_view));
        if (settings.measurement_view == MeasurementView::Amplitude && measurements() != nullptr && measurements()->amplitude_valid) {
            focus.add(" PK "); voltage(focus, static_cast<std::int64_t>(measurements()->peak_microvolts));
        }
        focus.add(" +/- page");
        break;
    case UiFocus::Storage:
        focus.add("STORE ").add(storage_action_name(ui_.storage_action()));
        focus.add("  ").add(storage_status_name(storage_status_));
        break;
    case UiFocus::CalibrationReference:
        focus.add("REF ").signed_number(ui_.reference_microvolts()).add("uV STEP ").number(ui_.calibration_edit_step()).add("uV");
        break;
    case UiFocus::CalibrationTolerance:
        focus.add("TOL ").number(ui_.tolerance_microvolts()).add("uV STEP ").number(ui_.calibration_edit_step()).add("uV");
        break;
    case UiFocus::Calibration:
        focus.add("CAL ").add(calibration_action_name(ui_.calibration_action()));
        focus.add("  ").add(calibration_stage(calibration_session_.state()));
        break;
    case UiFocus::Domain:
        focus.add("DOMAIN ").add(ui_.domain() == Domain::Spectrum ? "SPECTRUM +/-" : "SCOPE +/-");
        break;
    default: break;
    }
    const char* hint = "SEL field  +/- adjust  OK run/hold";
    scene.warning = false;
    if (fault_ != UiControllerFault::None) {
        hint = frame_valid_ ? "FAULT: OK retry; record kept" : "FAULT: OK retry; no record";
        scene.warning = true;
    }
    else if (ui_.focus() == UiFocus::Storage) {
        hint = storage_status_ == UiStorageStatus::Disabled ? "Storage off" :
            storage_status_ == UiStorageStatus::Error ? "Save failed; kept; OK retry" :
            storage_status_ == UiStorageStatus::HoldRequired ? "HOLD to save" :
            "HOLD +/- action; OK; SEL exit";
    }
    else if (ui_.focus() == UiFocus::CalibrationReference || ui_.focus() == UiFocus::CalibrationTolerance || ui_.focus() == UiFocus::Calibration) {
        Text details{scene.record, &dirty_labels_, 64U, &screen_.text_ranges_[6U]}; details.add("DC CAL ").add(calibration_stage(calibration_session_.state()));
        if (calibration_session_.state() == CalibrationSessionState::Verified || calibration_session_.state() == CalibrationSessionState::Installed ||
            calibration_session_.fit_status() == CalibrationStatus::ValidationFailed) {
            details.add(" ERR ").number(calibration_session_.validation_error_microvolts()).add("uV TOL ")
                .number(calibration_session_.validation_tolerance_microvolts()).add("uV");
        }
        hint = ui_.focus() == UiFocus::CalibrationReference ? "DC: +/- value; OK digit" :
            ui_.focus() == UiFocus::CalibrationTolerance ? "+/- tol; OK digit; check 3" :
            "HOLD +/- action; OK; SEL exit";
        if (calibration_status_ == UiCalibrationStatus::Disabled) { hint = "Board ID needed"; }
        else if (calibration_status_ == UiCalibrationStatus::HoldRequired) { hint = "Wait for HOLD; OK"; }
        else if (calibration_status_ == UiCalibrationStatus::ReferenceRequired) { hint = "HOLD fresh, steady, unclipped DC"; }
        else if (calibration_status_ == UiCalibrationStatus::Error) { hint = calibration_failure(calibration_session_); }
    }
    else if (spectrum) {
        scene.warning = acquisition_running() ||
            !frame_valid_ || frame_.capture.length < SpectrumLength ||
            (frame_valid_ && (frame_.timing.timer_hz == 0U || frame_.timing.divider == 0U));
        hint = acquisition_running() ? "HOLD for FFT" :
            !frame_valid_ ? "FFT needs a frozen uniform frame" :
            frame_.capture.length < SpectrumLength ? "FFT needs at least 512 samples" :
            (frame_.timing.timer_hz == 0U || frame_.timing.divider == 0U) ? "FFT timing is unknown" :
            "0dBFS reference = 2048-code peak";
    }
    else if (plan_.support != ScopeTimebaseSupport::CircularDma) { hint = "Capture unavailable; view only"; scene.warning = true; }
    else if (!config_.acquisition_enabled) { hint = "Hardware disabled"; scene.warning = true; }
    else if (!ui_.physical_ready()) { hint = "Switches settling"; scene.warning = true; }
    else if (frame_valid_ && !same_physical(frame_.physical, ui_.physical())) { hint = "OLD range/coupling held"; scene.warning = true; }
    else if (measurements() != nullptr && measurements()->adc_clipped) { hint = "ADC clip: amp/freq invalid"; scene.warning = true; }
    else if (measurements() != nullptr && measurements()->input_ac_coupled) { hint = "AC-coupled RMS/mean"; }
    else if (settings.measurement_view == MeasurementView::Frequency) { hint = "F clock nominal; alias risk"; }
    else if (frame_valid_ && kind == CalibrationKind::Unavailable) { hint = "RAW: no volts"; scene.warning = true; }
    else if (frame_valid_ && projection_.interpolated) { hint = "Interpolated trace"; }
    Text{scene.hint, &dirty_labels_, 256U, &screen_.text_ranges_[8U]}.add(hint);
}

} // namespace dso
