#include "dso_ui_controller.hpp"
#include "dso_physical_settings.hpp"
#include "dso_scope_screen.hpp"
#include "dso_u64_division.hpp"

#include <cstddef>

// Bounded screen-text composition from controller-owned state.
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
class ScreenTextBuilder final {
public:
    template<std::size_t N>
    explicit ScreenTextBuilder(char (&buffer)[N], std::uint16_t* dirty = nullptr,
                               ScreenTextField field = ScreenTextField::Count,
                               ScreenTextRange* range = nullptr) noexcept
        : data_{buffer}, dirty_{dirty}, range_{range}, size_{N}, field_{field}
    {
        static_assert(N <= 64U, "Screen-text change endpoints must fit the bounded glyph range");
    }

    ScreenTextBuilder(char* buffer, std::uint16_t size, std::uint16_t* dirty,
                      ScreenTextField field, ScreenTextRange* range) noexcept
        : data_{buffer}, dirty_{dirty}, range_{range}, size_{size}, field_{field}
    {
    }

    // Scope exit terminates the label, clears its old suffix, and publishes the dirty range.
    // Keep each builder scoped to one label so cache state is ready before rendering.
    ~ScreenTextBuilder() noexcept
    {
        if (!old_ended_ && data_[used_] != '\0') {
            auto tail = used_;
            while (tail + 1U < size_ && data_[tail] != '\0') {
                ++tail;
            }
            change(used_, tail); // Erase every previously visible suffix glyph.
        }
        data_[used_] = '\0';
        if (changed_ && dirty_ != nullptr) {
            *dirty_ |= screen_text_mask(field_);
        }
    }

    ScreenTextBuilder(const ScreenTextBuilder&) = delete;
    ScreenTextBuilder& operator=(const ScreenTextBuilder&) = delete;

    ScreenTextBuilder& add(const char* value) noexcept
    {
        // Compare the previous label in place before overwriting each byte.
        // Terminate once, after composition: intermediate add() calls must not
        // erase old bytes before the exact comparison reaches them.
        while (*value != '\0' && used_ + 1U < size_) {
            old_ended_ = old_ended_ || data_[used_] == '\0';
            if (old_ended_ || data_[used_] != *value) {
                change(used_, static_cast<std::uint16_t>(used_ + 1U));
            }
            data_[used_++] = *value++;
        }
        return *this;
    }

    // All formatter integer parts fit uint32_t: calibration input is bounded to +/-100 V,
    // ADC-derived quantities cover at most 4095 codes, and decimal callers format
    // bounded timer/sample/frequency ratios. Ten digits therefore cover every UI value.
    ScreenTextBuilder& number(std::uint32_t value) noexcept
    {
        char digits[10]{};
        unsigned count{};
        do {
            digits[count++] = static_cast<char>('0' + value % 10U);
            value /= 10U;
        } while (value != 0U);
        while (count != 0U) {
            const char one[]{digits[--count], '\0'};
            add(one);
        }
        return *this;
    }

    ScreenTextBuilder& signed_number(std::int64_t value) noexcept
    {
        const auto magnitude = value < 0 ? static_cast<std::uint64_t>(-(value + 1)) + 1U
                                         : static_cast<std::uint64_t>(value);
        if (value < 0) {
            add("-");
        }
        return number(static_cast<std::uint32_t>(magnitude));
    }

    ScreenTextBuilder& decimal(std::uint64_t numerator, std::uint64_t denominator) noexcept
    {
        const auto scaled = detail::divide_u64(numerator * 100U + denominator / 2U,
                                               denominator)
                                .quotient;
        const auto integer = detail::divide_u64(scaled, 100U);
        number(static_cast<std::uint32_t>(integer.quotient));
        const auto fraction = static_cast<std::uint32_t>(integer.remainder);
        if (fraction != 0U) {
            add(".");
            const char digits[]{static_cast<char>('0' + fraction / 10U),
                                static_cast<char>('0' + fraction % 10U), '\0'};
            add(digits);
        }
        return *this;
    }

private:
    void change(std::uint16_t first, std::uint16_t end) noexcept
    {
        changed_ = true;
        if (range_ != nullptr) {
            range_->include(first, end);
        }
    }
    char* data_;
    std::uint16_t* dirty_;
    ScreenTextRange* range_;
    std::uint16_t size_;
    std::uint16_t used_{};
    ScreenTextField field_;
    bool changed_{};
    bool old_ended_{};
};

// Share the dirty-range update and termination code for one-shot labels.
#if defined(__GNUC__)
__attribute__((noinline))
#endif
void set_label_text(char* buffer, std::uint16_t capacity, std::uint16_t* dirty,
                    ScreenTextField field, ScreenTextRange* range, const char* value) noexcept
{
    ScreenTextBuilder{buffer, capacity, dirty, field, range}.add(value);
}

template<std::size_t N>
void set_label(char (&buffer)[N], const char* value, std::uint16_t* dirty = nullptr,
               ScreenTextField field = ScreenTextField::Count,
               ScreenTextRange* range = nullptr) noexcept
{
    static_assert(N <= 64U, "Screen-text change endpoints must fit the bounded glyph range");
    set_label_text(buffer, N, dirty, field, range, value);
}

void voltage(ScreenTextBuilder& text, std::int64_t microvolts) noexcept
{
    const auto magnitude = microvolts < 0 ? static_cast<std::uint64_t>(-(microvolts + 1)) + 1U : static_cast<std::uint64_t>(microvolts);
    if (microvolts < 0) { text.add("-"); }
    if (magnitude >= 1000000U) { text.decimal(magnitude, 1000000U).add("V"); }
    else if (magnitude >= 1000U) { text.decimal(magnitude, 1000U).add("mV"); }
    else { text.number(static_cast<std::uint32_t>(magnitude)).add("uV"); }
}
void timebase(ScreenTextBuilder& text, std::uint32_t microseconds) noexcept
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
const char* frequency_state(FrequencyStatus status) noexcept
{
    return compact_label(FrequencyStatePool, FrequencyStateOffsets, static_cast<std::size_t>(status), 7U);
}
void microhertz(ScreenTextBuilder& text, std::uint64_t value) noexcept;
void frequency(ScreenTextBuilder& text, const MeasurementResult& result) noexcept
{
    if (result.frequency_status != FrequencyStatus::Valid) { text.add("-- ").add(frequency_state(result.frequency_status)); return; }
    microhertz(text, result.frequency_microhertz);
}
void microhertz(ScreenTextBuilder& text, std::uint64_t value) noexcept
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

void ScopeController::format_measurements() noexcept
{
    auto& scene = screen_.view_;
    ScreenTextBuilder text{scene.record, &dirty_labels_, ScreenTextField::Record,
              &screen_.composed_text_range(ScreenTextField::Record)};
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
    const auto& retained = retained_frame_;
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
    scene.waveform = retained.valid && analysis_.projection.available ? &analysis_.projection : nullptr;
    const auto state = ui_.state();
    const char* status = !ui_.wants_run() ? "HOLD" : (state == UiRunState::Acquiring || state == UiRunState::ArmPending) ? "RUN" :
        state == UiRunState::FrameReady ? "FRAME" : state == UiRunState::WaitingForControls ? "WAIT INPUT" :
        state == UiRunState::WaitingForBackend ? "WAIT BACK" : "ARM";
    if (state == UiRunState::Fault) { status = "FAULT"; }
    set_label(scene.status, status, &dirty_labels_, ScreenTextField::Status,
              &screen_.composed_text_range(ScreenTextField::Status));
    const auto kind = retained.valid ? retained.calibration.kind : CalibrationKind::Unavailable;
    const auto& settings = ui_.settings();
    const bool spectrum = ui_.domain() == Domain::Spectrum;
    if (spectrum) {
        const auto* spectrum_result = analysis_.spectrum.result();
        ScreenTextBuilder quality{scene.quality, &dirty_labels_, ScreenTextField::Quality,
                     &screen_.composed_text_range(ScreenTextField::Quality)};
        quality.add("FFT");
        set_label(scene.channel, "FFT 512 HANN AC", &dirty_labels_, ScreenTextField::Channel,
                  &screen_.composed_text_range(ScreenTextField::Channel));
        set_label(scene.timebase, "dBFS ADC peak: 2048", &dirty_labels_, ScreenTextField::Timebase,
                  &screen_.composed_text_range(ScreenTextField::Timebase));
        ScreenTextBuilder peak{scene.trigger, &dirty_labels_, ScreenTextField::Trigger,
                  &screen_.composed_text_range(ScreenTextField::Trigger)};
        if (spectrum_result != nullptr && spectrum_result->available && spectrum_result->peak_available) {
            peak.add("Pk "); microhertz(peak, analysis_.spectrum.peak_frequency_microhertz()); peak.add(" ");
            const auto level = static_cast<std::int32_t>(spectrum_result->peak_dbfs_x100);
            const auto magnitude = static_cast<std::uint32_t>(level < 0 ? -level : level);
            if (level < 0) { peak.add("-"); }
            peak.decimal(magnitude, 100U).add("dBFS");
        } else { peak.add("Peak --"); }
        if (spectrum_result != nullptr && spectrum_result->available) {
            ScreenTextBuilder sampling{scene.sampling, &dirty_labels_, ScreenTextField::Sampling,
                          &screen_.composed_text_range(ScreenTextField::Sampling)};
            sampling.add("df "); microhertz(sampling, spectrum_result->bin_spacing_microhertz);
            ScreenTextBuilder span{scene.record, &dirty_labels_, ScreenTextField::Record,
                      &screen_.composed_text_range(ScreenTextField::Record)};
            span.add("0.. "); microhertz(span, spectrum_result->nyquist_microhertz); span.add(" 10dB/div");
            set_label(scene.zero_label, "0dB");
            scene.zero_y = static_cast<std::int16_t>(SpectrumPlotTop);
        } else {
            set_label(scene.sampling, "df --", &dirty_labels_, ScreenTextField::Sampling,
                      &screen_.composed_text_range(ScreenTextField::Sampling));
            ScreenTextBuilder record{scene.record, &dirty_labels_, ScreenTextField::Record,
                        &screen_.composed_text_range(ScreenTextField::Record)};
            if (!retained.valid) { record.add("FFT NEEDS FROZEN FRAME"); }
            else if (acquisition_running()) { record.add("FFT NEEDS HELD UNIFORM BLOCK"); }
            else if (retained.metadata.capture.length < SpectrumLength) {
                record.add("FFT NEEDS 512 SAMPLES");
            }
            else if (retained.metadata.timing.timer_hz == 0U ||
                     retained.metadata.timing.divider == 0U) {
                record.add("FFT TIMING UNKNOWN");
            }
            else { record.add("FFT UNAVAILABLE"); }
            scene.zero_label[0] = '\0';
        }
        scene.trigger_y = -1; scene.trigger_x = -1;
    } else {
    set_label(scene.quality, kind == CalibrationKind::Measured ? "CAL" : kind == CalibrationKind::Nominal ? "NOM" : "RAW",
              &dirty_labels_, ScreenTextField::Quality,
              &screen_.composed_text_range(ScreenTextField::Quality));
    const auto& physical = retained.valid ? retained.metadata.physical : ui_.physical();
    ScreenTextBuilder channel{scene.channel, &dirty_labels_, ScreenTextField::Channel,
                 &screen_.composed_text_range(ScreenTextField::Channel)};
    channel.add(coupling_name(physical.coupling)).add(" ");
    if (kind == CalibrationKind::Unavailable && physical.microvolts_per_div != 0U) { channel.add("SEN "); }
    if (physical.microvolts_per_div != 0U) { voltage(channel, physical.microvolts_per_div); channel.add("/div"); }
    else { channel.add("RANGE UNKNOWN"); }
    ScreenTextBuilder time{scene.timebase, &dirty_labels_, ScreenTextField::Timebase,
              &screen_.composed_text_range(ScreenTextField::Timebase)};
    timebase(time, scope_timebase_us(ui_.settings().timebase_index));
    const auto mode = retained.valid ? retained.metadata.capture.mode : settings.mode;
    const auto edge = retained.valid ? retained.metadata.capture.edge : settings.edge;
    const auto level = retained.valid ? retained.metadata.capture.level : settings.trigger_level_code;
    ScreenTextBuilder trigger{scene.trigger, &dirty_labels_, ScreenTextField::Trigger,
                 &screen_.composed_text_range(ScreenTextField::Trigger)};
    trigger.add(mode_name(mode)).add(edge == TriggerEdge::Rising ? " R C" : " F C").number(level);
    if (retained.valid) { trigger.add(retained.metadata.edge_triggered ? " TRIG" : " FREE"); }
    if (retained.valid) {
        ScreenTextBuilder sampling{scene.sampling, &dirty_labels_, ScreenTextField::Sampling,
                      &screen_.composed_text_range(ScreenTextField::Sampling)};
        const auto divider = std::uint64_t{retained.metadata.timing.divider};
        if (divider >= 14062500U) {
            sampling.add("dt ").decimal(divider * 1000U, retained.metadata.timing.timer_hz).add("ms");
        }
        else if (retained.metadata.timing.timer_hz >= divider * 1000U) {
            sampling.decimal(retained.metadata.timing.timer_hz, divider * 1000U).add("kS/s");
        }
        else { sampling.decimal(retained.metadata.timing.timer_hz, divider).add("S/s"); }
        format_measurements();
        std::int32_t zero{}, threshold{};
        const auto viewport = view();
        if (kind == CalibrationKind::Unavailable) {
            set_label(scene.zero_label, "REF");
            if (project_code_y(2048U, retained.metadata, retained.calibration, viewport, zero) &&
                zero >= 0 && zero < ScopePlotHeight) {
                scene.zero_y = static_cast<std::int16_t>(zero);
            }
        } else {
            set_label(scene.zero_label, "0V");
            zero = ScopePlotHeight / 2 + settings.vertical_offset_pixels;
            if (zero >= 0 && zero < ScopePlotHeight) { scene.zero_y = static_cast<std::int16_t>(zero); }
        }
        if (project_code_y(level, retained.metadata, retained.calibration, viewport, threshold) &&
            threshold >= 0 && threshold < ScopePlotHeight) {
            scene.trigger_y = static_cast<std::int16_t>(threshold);
        }
        std::int64_t trigger_x{};
        if (project_trigger_x(retained.metadata, viewport, trigger_x) &&
            trigger_x >= 0 && trigger_x <= ScopePlotWidth) {
            scene.trigger_x = static_cast<std::int16_t>(trigger_x == ScopePlotWidth ? ScopePlotWidth - 1U : trigger_x);
        }
    } else {
        set_label(scene.sampling, "NO RECORD", &dirty_labels_, ScreenTextField::Sampling,
                  &screen_.composed_text_range(ScreenTextField::Sampling));
        set_label(scene.record, "No record", &dirty_labels_, ScreenTextField::Record,
                  &screen_.composed_text_range(ScreenTextField::Record));
    }
    }
    ScreenTextBuilder focus{scene.focus, &dirty_labels_, ScreenTextField::Focus,
               &screen_.composed_text_range(ScreenTextField::Focus)};
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
        focus.add("  ").add(storage_status_name(storage_.status));
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
        hint = retained.valid ? "FAULT: OK retry; record kept" : "FAULT: OK retry; no record";
        scene.warning = true;
    }
    else if (ui_.focus() == UiFocus::Storage) {
        hint = storage_.status == UiStorageStatus::Disabled ? "Storage off" :
            storage_.status == UiStorageStatus::Error ? "Save failed; kept; OK retry" :
            storage_.status == UiStorageStatus::HoldRequired ? "HOLD to save" :
            "HOLD +/- action; OK; SEL exit";
    }
    else if (ui_.focus() == UiFocus::CalibrationReference || ui_.focus() == UiFocus::CalibrationTolerance || ui_.focus() == UiFocus::Calibration) {
        ScreenTextBuilder details{scene.record, &dirty_labels_, ScreenTextField::Record,
                     &screen_.composed_text_range(ScreenTextField::Record)};
        details.add("DC CAL ").add(calibration_stage(calibration_session_.state()));
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
            !retained.valid || retained.metadata.capture.length < SpectrumLength ||
            (retained.valid &&
             (retained.metadata.timing.timer_hz == 0U ||
              retained.metadata.timing.divider == 0U));
        hint = acquisition_running() ? "HOLD for FFT" :
            !retained.valid ? "FFT needs a frozen uniform frame" :
            retained.metadata.capture.length < SpectrumLength ? "FFT needs at least 512 samples" :
            (retained.metadata.timing.timer_hz == 0U ||
             retained.metadata.timing.divider == 0U)
                ? "FFT timing is unknown"
                :
            "0dBFS reference = 2048-code peak";
    }
    else if (plan_.support != ScopeTimebaseSupport::CircularDma) { hint = "Capture unavailable; view only"; scene.warning = true; }
    else if (!config_.acquisition_enabled) { hint = "Hardware disabled"; scene.warning = true; }
    else if (!ui_.physical_ready()) { hint = "Switches settling"; scene.warning = true; }
    else if (retained.valid && !same_physical_settings(retained.metadata.physical, ui_.physical())) {
        hint = "OLD range/coupling held";
        scene.warning = true;
    }
    else if (measurements() != nullptr && measurements()->adc_clipped) { hint = "ADC clip: amp/freq invalid"; scene.warning = true; }
    else if (measurements() != nullptr && measurements()->input_ac_coupled) { hint = "AC-coupled RMS/mean"; }
    else if (settings.measurement_view == MeasurementView::Frequency) { hint = "F clock nominal; alias risk"; }
    else if (retained.valid && kind == CalibrationKind::Unavailable) { hint = "RAW: no volts"; scene.warning = true; }
    else if (retained.valid && analysis_.projection.interpolated) { hint = "Interpolated trace"; }
    set_label(scene.hint, hint, &dirty_labels_, ScreenTextField::Hint,
              &screen_.composed_text_range(ScreenTextField::Hint));
}

} // namespace dso
