#include "dso_measurements.hpp"
#include "dso_voltage_math.hpp"
#include "dso_fixed_ratio.hpp"

namespace dso {
namespace {
constexpr std::uint64_t Q32{std::uint64_t{1U} << 32U};
static_assert(CaptureCapacity <= 1024U, "Q32 measurement denominator proof assumes at most 1024 samples");
static_assert(std::uint64_t{AdcMax} * AdcMax * Q32 + 2U < (std::uint64_t{1U} << 56U),
              "12-bit ADC Q32 RMS values, including calibration rounding, must fit the bounded root domain");

std::uint64_t frequency_rates(std::uint64_t numerator, std::uint64_t denominator,
                              std::uint64_t& millihertz) noexcept
{
    const auto ratio = detail::million_ratio(numerator, denominator);
    // Use the unrounded microhertz quotient: rounding it first can move a
    // value just below a millihertz half-step onto the wrong side of it.
    millihertz = (ratio.quotient + 500U) / 1000U;
    return ratio.quotient + (ratio.remainder >= (denominator + 1U) / 2U ? 1U : 0U);
}

std::uint64_t rational_q32(std::uint64_t numerator, std::uint64_t denominator) noexcept
{
    // Used with denominator<=1024^2 and quotient<=4095^2; both shifts fit.
    return (numerator / denominator) * Q32 + (numerator % denominator) * Q32 / denominator;
}

std::uint32_t square_root_code_q32(std::uint64_t value) noexcept
{
    // All callers pass a Q32 mean square in code units. For raw RMS and AC RMS,
    // it is at most AdcMax^2*Q32. For calibrated RMS, valid zero_code_q16 lies
    // in [0,AdcMax*65536], so each sample's offset magnitude is <=AdcMax; the
    // rational and signed correction truncations add less than 2 Q32 units.
    // Thus every input is below 2^56. Consume 28 base-4 digits: root<2^28,
    // stored remainder<2^29, and the next shifted remainder<2^31. The trial
    // is below 2^29; all intermediates fit uint32_t.
    std::uint32_t root{}, remainder{};
    for (unsigned digit = 0U; digit < 28U; ++digit) {
        remainder = (remainder << 2U) | static_cast<std::uint32_t>((value >> 54U) & 3U);
        value <<= 2U;
        root <<= 1U;
        const auto trial = (root << 1U) | 1U;
        if (remainder >= trial) { remainder -= trial; ++root; }
    }
    return root;
}

std::int64_t rounded_signed(std::int64_t numerator, std::uint64_t denominator) noexcept
{
    // All internal signed operands are strictly greater than INT64_MIN.
    const auto magnitude = static_cast<std::uint64_t>(numerator < 0 ? -numerator : numerator);
    const auto result = static_cast<std::int64_t>((magnitude + denominator / 2U) / denominator);
    return numerator < 0 ? -result : result;
}

std::uint64_t magnitude(std::int64_t value) noexcept
{ return static_cast<std::uint64_t>(value < 0 ? -value : value); }

std::uint64_t scaled_rms(std::uint32_t code_q16, VoltageCalibration calibration) noexcept
{ return static_cast<std::uint64_t>(detail::round_gain_product(
      std::int64_t{code_q16} * calibration.microvolts_per_code_q16, calibration.gain_shift)); }

bool valid_begin(std::uint16_t count, const FrameMetadata& metadata,
                 VoltageCalibration calibration, const MeasurementConfig& config) noexcept
{
    return count != 0U && count <= CaptureCapacity && metadata.capture.length == count &&
        metadata.capture.pretrigger < count && metadata.acquisition_epoch != 0U &&
        metadata.physical.generation != 0U && metadata.physical.microvolts_per_div >= 10000U &&
        metadata.physical.microvolts_per_div <= 5000000U &&
        (metadata.physical.coupling == InputCoupling::Ac || metadata.physical.coupling == InputCoupling::Dc ||
         metadata.physical.coupling == InputCoupling::Ground) &&
        metadata.timing.timer_hz >= 1000000U && metadata.timing.timer_hz <= 72000000U &&
        metadata.timing.divider != 0U &&
        (std::uint64_t{metadata.timing.prescaler} + 1U) * (std::uint64_t{metadata.timing.period} + 1U) == metadata.timing.divider &&
        (metadata.processing.kind == SampleProcessingKind::Filtered25 ||
         (metadata.processing.kind == SampleProcessingKind::Point && !metadata.processing.source_clipped && !metadata.processing.output_clamped)) &&
        (calibration.kind == CalibrationKind::Unavailable || calibration_valid(calibration)) &&
        config.minimum_peak_to_peak_codes != 0U && config.minimum_peak_to_peak_codes <= AdcMax &&
        config.minimum_hysteresis_codes != 0U && config.minimum_hysteresis_codes <= AdcMax / 2U &&
        config.minimum_periods >= 1U && config.minimum_periods < CaptureCapacity &&
        config.minimum_samples_per_period >= 2U && config.minimum_samples_per_period <= CaptureCapacity &&
        config.maximum_period_deviation_permille < 1000U;
}
}

bool MeasurementEngine::begin(const std::uint16_t* samples, std::uint16_t count,
                              const FrameMetadata& metadata, VoltageCalibration calibration,
                              MeasurementConfig config) noexcept
{
    state_ = MeasurementState::Invalid;
    samples_ = nullptr;
    if (samples == nullptr || !valid_begin(count, metadata, calibration, config)) { return false; }
    samples_ = samples; metadata_ = metadata; calibration_ = calibration; config_ = config;
    result_ = MeasurementResult{};
    result_.count = count;
    result_.minimum_code = AdcMax;
    result_.acquisition_epoch = metadata.acquisition_epoch;
    result_.physical_generation = metadata.physical.generation;
    result_.calibration_kind = calibration.kind;
    result_.voltage_available = calibration.kind != CalibrationKind::Unavailable;
    result_.input_ac_coupled = metadata.physical.coupling == InputCoupling::Ac;
    result_.source_clipped = metadata.processing.source_clipped;
    result_.filter_clamped = metadata.processing.output_clamped;
    next_ = 0U; sum_ = 0U; sum_squares_ = 0U;
    have_previous_ = false; below_ = false; candidate_ = false; have_crossing_ = false;
    state_ = MeasurementState::Statistics;
    return true;
}

void MeasurementEngine::cancel() noexcept
{
    samples_ = nullptr;
    state_ = MeasurementState::Cancelled;
}

void MeasurementEngine::finish_statistics() noexcept
{
    const auto n = std::uint64_t{result_.count};
    result_.peak_to_peak_codes = static_cast<std::uint16_t>(result_.maximum_code - result_.minimum_code);
    result_.mean_code_q16 = static_cast<std::uint32_t>((std::uint64_t{sum_} * 65536U + n / 2U) / n);
    result_.rms_code_q16 = square_root_code_q32(rational_q32(sum_squares_, n));
    const auto variance = n * sum_squares_ - std::uint64_t{sum_} * sum_;
    result_.ac_rms_code_q16 = square_root_code_q32(rational_q32(variance, n * n));
    result_.adc_clipped = result_.rail_low_samples != 0U || result_.rail_high_samples != 0U ||
        result_.source_clipped || result_.filter_clamped;
    result_.amplitude_valid = result_.voltage_available && !result_.adc_clipped;
    if (result_.voltage_available) {
        static_cast<void>(code_to_microvolts(result_.minimum_code, calibration_, result_.minimum_microvolts));
        static_cast<void>(code_to_microvolts(result_.maximum_code, calibration_, result_.maximum_microvolts));
        result_.peak_to_peak_microvolts = static_cast<std::uint64_t>(result_.maximum_microvolts - result_.minimum_microvolts);
        const auto low = magnitude(result_.minimum_microvolts);
        const auto high = magnitude(result_.maximum_microvolts);
        result_.peak_microvolts = low > high ? low : high;
        const auto mean_delta = rounded_signed(std::int64_t{sum_} * 65536 -
            std::int64_t{result_.count} * calibration_.zero_code_q16, n);
        result_.mean_microvolts = detail::round_gain_product(
            mean_delta * calibration_.microvolts_per_code_q16, calibration_.gain_shift);
        result_.ac_rms_microvolts = scaled_rms(result_.ac_rms_code_q16, calibration_);

        // Center about integer zero first; fractional correction then fits64bits.
        const auto integer_zero = calibration_.zero_code_q16 / 65536;
        const auto fraction = std::int64_t{calibration_.zero_code_q16 % 65536};
        const auto centered_sum = std::int64_t{sum_} - std::int64_t{result_.count} * integer_zero;
        const auto centered_squares = static_cast<std::uint64_t>(static_cast<std::int64_t>(sum_squares_) -
            std::int64_t{2} * integer_zero * sum_ + std::int64_t{result_.count} * integer_zero * integer_zero);
        // Valid captures/calibration bound |numerator| below 2^55; n is 1..1024.
        const auto correction_numerator = 2 * fraction * centered_sum * 65536;
        const auto correction_magnitude = magnitude(correction_numerator);
        const auto correction_quotient = static_cast<std::int64_t>(correction_magnitude / n);
        const auto correction = correction_numerator < 0 ? -correction_quotient : correction_quotient;
        const auto mean_square = static_cast<std::int64_t>(rational_q32(centered_squares, n)) - correction + fraction * fraction;
        result_.rms_microvolts = scaled_rms(square_root_code_q32(static_cast<std::uint64_t>(mean_square < 0 ? 0 : mean_square)), calibration_);
    }

    // Conservative frequency validity order. Rail evidence always suppresses a
    // voltage/frequency accuracy claim; raw mathematical moments remain useful.
    if (result_.adc_clipped) { result_.frequency_status = FrequencyStatus::Clipped; }
    else if (metadata_.physical.coupling == InputCoupling::Ground) { result_.frequency_status = FrequencyStatus::Grounded; }
    else if (result_.peak_to_peak_codes < config_.minimum_peak_to_peak_codes ||
             result_.peak_to_peak_codes < 2U * config_.minimum_hysteresis_codes) { result_.frequency_status = FrequencyStatus::LowAmplitude; }
    else {
        auto hysteresis = static_cast<std::uint16_t>(result_.peak_to_peak_codes / 8U);
        if (hysteresis < config_.minimum_hysteresis_codes) { hysteresis = config_.minimum_hysteresis_codes; }
        midpoint_x2_ = static_cast<std::uint16_t>(result_.minimum_code + result_.maximum_code);
        lower_x2_ = static_cast<std::uint16_t>(midpoint_x2_ - 2U * hysteresis);
        upper_x2_ = static_cast<std::uint16_t>(midpoint_x2_ + 2U * hysteresis);
        next_ = 0U;
        state_ = MeasurementState::Frequency;
        return;
    }
    state_ = MeasurementState::Complete;
}

void MeasurementEngine::accept_crossing(std::uint32_t time_q16) noexcept
{
    if (!have_crossing_) { first_crossing_q16_ = time_q16; have_crossing_ = true; }
    else {
        const auto period = time_q16 - last_crossing_q16_;
        if (result_.periods == 0U || period < result_.minimum_period_q16) { result_.minimum_period_q16 = period; }
        if (period > result_.maximum_period_q16) { result_.maximum_period_q16 = period; }
        ++result_.periods;
    }
    last_crossing_q16_ = time_q16;
}

void MeasurementEngine::frequency_sample(std::uint16_t sample) noexcept
{
    const auto code_x2 = static_cast<std::uint16_t>(sample * 2U);
    if (!have_previous_) {
        have_previous_ = true;
        below_ = code_x2 <= lower_x2_;
    } else if (code_x2 <= lower_x2_) {
        below_ = true; candidate_ = false;
    } else if (below_) {
        const auto previous_x2 = static_cast<std::uint16_t>(previous_ * 2U);
        if (previous_x2 <= midpoint_x2_ && code_x2 > midpoint_x2_) {
            const auto denominator = static_cast<std::uint32_t>(code_x2 - previous_x2);
            const auto numerator = std::uint32_t{static_cast<std::uint16_t>(midpoint_x2_ - previous_x2)} * 65536U;
            candidate_q16_ = (std::uint32_t{next_} - 1U) * 65536U + (numerator + denominator / 2U) / denominator;
            candidate_ = true;
        }
        if (candidate_ && code_x2 >= upper_x2_) {
            accept_crossing(candidate_q16_);
            below_ = false; candidate_ = false;
        }
    }
    previous_ = sample;
}

void MeasurementEngine::finish_frequency() noexcept
{
    if (result_.periods < config_.minimum_periods) { result_.frequency_status = FrequencyStatus::InsufficientCycles; }
    else if (result_.minimum_period_q16 < std::uint32_t{config_.minimum_samples_per_period} * 65536U) {
        result_.frequency_status = FrequencyStatus::TooFast;
    } else {
        const auto span = std::uint64_t{last_crossing_q16_ - first_crossing_q16_};
        const auto periods = std::uint64_t{result_.periods};
        const auto tolerance = std::uint64_t{config_.maximum_period_deviation_permille};
        if (std::uint64_t{result_.minimum_period_q16} * periods * 1000U < span * (1000U - tolerance) ||
            std::uint64_t{result_.maximum_period_q16} * periods * 1000U > span * (1000U + tolerance)) {
            result_.frequency_status = FrequencyStatus::Unstable;
        } else {
            const auto numerator = std::uint64_t{metadata_.timing.timer_hz} * periods * 65536U;
            const auto denominator = std::uint64_t{metadata_.timing.divider} * span;
            result_.frequency_microhertz = frequency_rates(numerator, denominator, result_.frequency_millihz);
            result_.frequency_status = result_.frequency_microhertz == 0U ? FrequencyStatus::BelowResolution : FrequencyStatus::Valid;
        }
    }
    state_ = MeasurementState::Complete;
}

MeasurementStep MeasurementEngine::step(std::uint16_t sample_budget) noexcept
{
    MeasurementStep progress{state_, 0U};
    while (progress.samples < sample_budget &&
           (state_ == MeasurementState::Statistics || state_ == MeasurementState::Frequency)) {
        const auto sample = samples_[next_];
        ++progress.samples;
        if (sample > AdcMax) { state_ = MeasurementState::Invalid; samples_ = nullptr; break; }
        if (state_ == MeasurementState::Statistics) {
            sum_ += sample;
            sum_squares_ += std::uint32_t{sample} * sample;
            if (sample < result_.minimum_code) { result_.minimum_code = sample; }
            if (sample > result_.maximum_code) { result_.maximum_code = sample; }
            if (sample == 0U) { ++result_.rail_low_samples; }
            if (sample == AdcMax) { ++result_.rail_high_samples; }
            ++next_;
            if (next_ == result_.count) { finish_statistics(); }
        } else {
            frequency_sample(sample);
            ++next_;
            if (next_ == result_.count) { finish_frequency(); }
        }
    }
    progress.state = state_;
    return progress;
}

} // namespace dso
