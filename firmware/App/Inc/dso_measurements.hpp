#ifndef DSO_MEASUREMENTS_HPP
#define DSO_MEASUREMENTS_HPP

#include "dso_scaling.hpp"

namespace dso {

enum class MeasurementState : std::uint8_t { Idle, Statistics, Frequency, Complete, Invalid, Cancelled };
enum class FrequencyStatus : std::uint8_t {
    Valid, LowAmplitude, InsufficientCycles, TooFast, Unstable, Clipped, Grounded, BelowResolution
};

struct MeasurementConfig final {
    std::uint16_t minimum_peak_to_peak_codes{32U};
    std::uint16_t minimum_hysteresis_codes{4U};
    std::uint16_t minimum_periods{3U};
    std::uint16_t minimum_samples_per_period{8U};
    std::uint16_t maximum_period_deviation_permille{100U};
};

struct MeasurementResult final {
    std::uint32_t acquisition_epoch{};
    std::uint32_t physical_generation{};
    CalibrationKind calibration_kind{CalibrationKind::Unavailable};
    std::uint16_t count{};
    std::uint16_t minimum_code{};
    std::uint16_t maximum_code{};
    std::uint16_t peak_to_peak_codes{};
    std::uint16_t rail_low_samples{};
    std::uint16_t rail_high_samples{};
    std::uint32_t mean_code_q16{};
    std::uint32_t rms_code_q16{}; // RMS about ADC code zero, not input zero.
    std::int64_t mean_microvolts{};
    std::uint64_t peak_to_peak_microvolts{};
    std::uint64_t peak_microvolts{}; // maximum absolute extrema; not half Vpp.
    std::uint64_t rms_microvolts{}; // Total RMS about supplied calibration zero.
    std::uint64_t frequency_microhertz{}; // Primary rounded rate; Valid requires positive uHz.
    std::uint64_t frequency_millihz{}; // Legacy independently rounded rate; may be0 when Valid.
    std::uint16_t periods{};
    std::uint32_t minimum_period_q16{};
    std::uint32_t maximum_period_q16{};
    FrequencyStatus frequency_status{FrequencyStatus::InsufficientCycles};
    bool adc_clipped{}; // One or more raw ADC samples reached a rail.
    bool voltage_available{}; // Explicit Nominal/Measured profile supplied.
    bool amplitude_valid{}; // voltage_available and no observed ADC rail hits.
    bool input_ac_coupled{}; // Input DC removed by front-end, not recovered here.
};

struct MeasurementStep final {
    MeasurementState state;
    std::uint16_t samples;
};

// Foreground-only bounded two-pass reduction of an immutable, private record.
// The source stays alive and unchanged until Complete/Invalid/Cancelled.
// Metadata/profile are copied at begin. No heap, floating point, HAL or IRQ use.
// Result is published only on Complete; cancel/rejected begin invalidates it.
class MeasurementEngine final {
public:
    MeasurementEngine() = default;
    MeasurementEngine(const MeasurementEngine&) = delete;
    MeasurementEngine& operator=(const MeasurementEngine&) = delete;
    MeasurementEngine(MeasurementEngine&&) = delete;
    MeasurementEngine& operator=(MeasurementEngine&&) = delete;
    [[nodiscard]] bool begin(const std::uint16_t* samples, std::uint16_t count,
                             const FrameMetadata& metadata, VoltageCalibration calibration,
                             MeasurementConfig config = {}) noexcept;
    [[nodiscard]] MeasurementStep step(std::uint16_t sample_budget) noexcept;
    void cancel() noexcept;
    [[nodiscard]] MeasurementState state() const noexcept { return state_; }
    [[nodiscard]] const MeasurementResult* result() const noexcept
    { return state_ == MeasurementState::Complete ? &result_ : nullptr; }

private:
    void finish_statistics() noexcept;
    void frequency_sample(std::uint16_t sample) noexcept;
    void finish_frequency() noexcept;
    void accept_crossing(std::uint32_t time_q16) noexcept;
    const std::uint16_t* samples_{};
    FrameMetadata metadata_{};
    VoltageCalibration calibration_{};
    MeasurementConfig config_{};
    MeasurementResult result_{};
    MeasurementState state_{MeasurementState::Idle};
    std::uint16_t next_{};
    std::uint32_t sum_{};
    std::uint64_t sum_squares_{};
    std::uint16_t midpoint_x2_{};
    std::uint16_t lower_x2_{};
    std::uint16_t upper_x2_{};
    std::uint16_t previous_{};
    std::uint32_t candidate_q16_{};
    std::uint32_t first_crossing_q16_{};
    std::uint32_t last_crossing_q16_{};
    bool have_previous_{};
    bool below_{};
    bool candidate_{};
    bool have_crossing_{};
};

} // namespace dso
#endif
