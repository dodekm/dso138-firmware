#include "dso_spectrum.hpp"
#include "dso_adc_codes.hpp"
#include "dso_capture_config.hpp"
#include "dso_frame_metadata.hpp"
#include "dso_scaling.hpp"
#include "dso_timebase.hpp"
#include "dso_u64_division.hpp"

#include <algorithm>
#include <cstdint>
#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace dso {
namespace {

constexpr std::int32_t kNegativeInfinityDbfs = -32768;
constexpr std::uint16_t kLog2Q12[17] = {
    0, 358, 696, 1016, 1319, 1607, 1882, 2145,
    2396, 2637, 2869, 3092, 3307, 3514, 3715, 3908, 4096
};
std::uint8_t leading_zeros(std::uint32_t value) noexcept
{
#if defined(__GNUC__) || defined(__clang__)
    return static_cast<std::uint8_t>(__builtin_clz(value));
#elif defined(_MSC_VER)
    unsigned long most_significant_bit = 0UL;
    static_cast<void>(_BitScanReverse(&most_significant_bit, value));
    return static_cast<std::uint8_t>(31UL - most_significant_bit);
#else
    std::uint8_t zeros = 0U;
    for (std::uint32_t mask = 0x80000000U; (value & mask) == 0U; mask >>= 1U) {
        ++zeros;
    }
    return zeros;
#endif
}

std::uint32_t log2_q12(std::uint32_t value) noexcept
{
    if (value == 0U) { return 0U; }
    const auto exponent = static_cast<std::uint8_t>(31U - leading_zeros(value));
    const auto normalized = exponent >= 12U
        ? static_cast<std::uint32_t>(value >> (exponent - 12U))
        : static_cast<std::uint32_t>(value << (12U - exponent));
    const auto index = static_cast<std::uint8_t>((normalized - 4096U) >> 8U);
    const auto fraction = normalized & 255U;
    const auto lower = kLog2Q12[index];
    const auto difference = static_cast<std::uint32_t>(
        kLog2Q12[static_cast<std::size_t>(index) + 1U] - lower);
    return static_cast<std::uint32_t>(exponent) * 4096U + lower +
           (difference * fraction + 128U) / 256U;
}

std::int32_t hann_q15(std::uint16_t phase) noexcept
{
    const auto cosine = static_cast<std::int32_t>(dsp::fft_cosine_q15(phase));
    const auto numerator = 32768 - cosine;
    auto coefficient = numerator / 2;
    if ((numerator & 1) != 0 && (coefficient & 1) != 0) { ++coefficient; }
    return coefficient;
}

std::int32_t dbfs_from_log_power(std::uint32_t log_power, std::uint16_t bin) noexcept
{
    const auto exponent = static_cast<std::int32_t>(log_power >> 12U);
    const auto fraction = log_power & 4095U;
    // Hann coherent gain is 1/2. Double interior bins, but leave DC/Nyquist
    // one-sided endpoints undoubled. These are power-of-two corrections.
    const auto correction = (bin == 0U || bin == SpectrumLength / 2U) ? 2 : 4;
    // Under the FFT power bound, the whole term is within ±1.6M and the
    // fractional product is below 124M, so signed 32-bit arithmetic suffices.
    const auto whole = (exponent - 54 + correction) * 30103;
    const auto fractional = static_cast<std::int32_t>(
        (fraction * 30103U + 2048U) >> 12U);
    const auto numerator = whole + fractional;
    const auto magnitude = static_cast<std::uint32_t>(numerator < 0 ? -numerator : numerator);
    const auto rounded = static_cast<std::int32_t>((magnitude + 50U) / 100U);
    return numerator < 0 ? -rounded : rounded;
}

} // namespace

bool SpectrumAnalyzer::begin(const std::uint16_t* samples, std::uint16_t count,
                             std::uint16_t start, const FrameMetadata& frame,
                             std::int32_t* work, std::uint16_t capacity_words,
                             WaveformProjection& projection) noexcept
{
    fft_.cancel();
    if (projection_ != nullptr) { projection_->available = false; }
    reset();
    projection.available = false;
    projection.interpolated = false;
    projection.raw_codes = false;
    if (samples == nullptr || work == nullptr || count == 0U ||
        count > CaptureCapacity ||
        static_cast<std::uint32_t>(start) + SpectrumLength > count ||
        capacity_words < SpectrumLength || !valid_record_timing(frame.timing)) {
        state_ = SpectrumState::Invalid;
        return false;
    }

    samples_ = samples;
    work_ = work;
    projection_ = &projection;
    start_ = start;
    capacity_words_ = capacity_words;
    timer_hz_ = frame.timing.timer_hz;
    divider_ = frame.timing.divider;
    projection.width = SpectrumPlotWidth;
    projection.height = SpectrumPlotHeight;
    projection.available = false;
    projection.interpolated = false;
    projection.raw_codes = false;
    state_ = SpectrumState::ClearingProjection;
    return true;
}

SpectrumState SpectrumAnalyzer::step(std::uint16_t max_operations) noexcept
{
    auto remaining = max_operations;
    while (remaining != 0U && state_ != SpectrumState::Complete &&
           state_ != SpectrumState::Cancelled && state_ != SpectrumState::Invalid &&
           state_ != SpectrumState::Idle) {
        if (state_ == SpectrumState::Transforming) {
            const auto phase = fft_.step(remaining);
            if (phase == dsp::FftPhase::Complete) {
                state_ = SpectrumState::Projecting;
                index_ = 0U;
                result_.bin_spacing_microhertz = frequency_microhertz(1U);
                result_.nyquist_microhertz = frequency_microhertz(SpectrumLength / 2U);
            } else if (phase == dsp::FftPhase::Invalid ||
                       phase == dsp::FftPhase::Cancelled) {
                state_ = SpectrumState::Invalid;
            }
            return state_;
        }
        --remaining;
        switch (state_) {
        case SpectrumState::ClearingProjection:
            projection_->columns[index_] = ProjectionColumn{};
            if (++index_ == SpectrumPlotWidth) { index_ = 0U; state_ = SpectrumState::Mean; }
            break;
        case SpectrumState::Mean: mean_one(); break;
        case SpectrumState::Preparing: prepare_one(); break;
        case SpectrumState::Projecting: project_one(); break;
        default: state_ = SpectrumState::Invalid; break;
        }
    }
    return state_;
}

void SpectrumAnalyzer::cancel() noexcept
{
    fft_.cancel();
    samples_ = nullptr;
    work_ = nullptr;
    if (projection_ != nullptr) { projection_->available = false; }
    projection_ = nullptr;
    result_.available = false;
    result_.peak_available = false;
    state_ = SpectrumState::Cancelled;
}

void SpectrumAnalyzer::reset() noexcept
{
    samples_ = nullptr;
    work_ = nullptr;
    projection_ = nullptr;
    result_ = SpectrumResult{};
    state_ = SpectrumState::Idle;
    start_ = index_ = last_x_ = 0U;
    capacity_words_ = 0U;
    last_y_ = 0;
    sample_sum_ = 0U;
    peak_score_ = 0U;
    packed_real_ = 0;
    timer_hz_ = divider_ = 0U;
    mean_q16_ = 0;
    have_last_point_ = false;
}

void SpectrumAnalyzer::mean_one() noexcept
{
    const auto sample = samples_[static_cast<std::uint16_t>(start_ + index_)];
    if (sample > AdcMax) { state_ = SpectrumState::Invalid; return; }
    sample_sum_ += sample;
    if (++index_ == SpectrumLength) {
        // Exact for N=512: multiply the sum by 65536/512 = 128.
        mean_q16_ = static_cast<std::int32_t>(sample_sum_ * 128U);
        index_ = 0U;
        state_ = SpectrumState::Preparing;
    }
}

void SpectrumAnalyzer::prepare_one() noexcept
{
    const auto sample = samples_[static_cast<std::uint16_t>(start_ + index_)];
    if (sample > AdcMax) { state_ = SpectrumState::Invalid; return; }
    const auto centered = static_cast<std::int64_t>(sample) * 65536 - mean_q16_;
    const auto windowed = centered * hann_q15(index_);
    const auto prepared = windowed >= 0
        ? (windowed + 16384) / 32768
        : -((-windowed + 16384) / 32768);
    if (prepared < -(std::int64_t{1} << 28U) ||
        prepared > (std::int64_t{1} << 28U)) { state_ = SpectrumState::Invalid; return; }
    if ((index_ & 1U) == 0U) {
        packed_real_ = static_cast<std::int32_t>(prepared);
    } else {
        work_[index_ - 1U] = packed_real_;
        work_[index_] = static_cast<std::int32_t>(prepared);
    }
    if (++index_ == SpectrumLength) {
        if (!fft_.begin(SpectrumLength, work_, capacity_words_)) {
            state_ = SpectrumState::Invalid;
            return;
        }
        state_ = SpectrumState::Transforming;
    }
}

void SpectrumAnalyzer::project_one() noexcept
{
    dsp::Complex32 value{};
    if (!fft_.bin(index_, value)) { state_ = SpectrumState::Invalid; return; }
    // Use the same exponent for both components, so the scaled squares fit in
    // 31 bits. The omitted low bits contribute less than 1/16384 relative
    // component error; the corresponding power/dB error is below 0.01 dB.
    const auto abs_real = value.real < 0
        ? 0U - static_cast<std::uint32_t>(value.real)
        : static_cast<std::uint32_t>(value.real);
    const auto abs_imag = value.imag < 0
        ? 0U - static_cast<std::uint32_t>(value.imag)
        : static_cast<std::uint32_t>(value.imag);
    const auto maximum = std::max(abs_real, abs_imag);
    const auto exponent = maximum == 0U
        ? 0U : static_cast<std::uint32_t>(31U - leading_zeros(maximum));
    const auto shift = static_cast<std::uint16_t>(exponent > 14U ? exponent - 14U : 0U);
    const auto scaled_real = abs_real >> shift;
    const auto scaled_imag = abs_imag >> shift;
    const auto power = scaled_real * scaled_real + scaled_imag * scaled_imag;
    const auto log_power = power == 0U ? 0U : log2_q12(power) +
        static_cast<std::uint32_t>(shift) * 2U * 4096U;
    const auto level = power == 0U ? kNegativeInfinityDbfs : dbfs_x100(log_power, index_);
    if (index_ != 0U && power != 0U) {
        const auto correction = (index_ == SpectrumLength / 2U ? 2U : 4U) * 4096U;
        const auto score = log_power + correction;
        // This Q12 log key is for bin ranking, not sub-bin peak precision.
        // Strict comparison keeps the lowest bin on exact score ties.
        if (score > peak_score_) {
            peak_score_ = score;
            result_.peak_available = true;
            result_.peak_bin = index_;
            result_.peak_dbfs_x100 = static_cast<std::int16_t>(level);
        }
    }
    const auto coordinate = static_cast<std::int16_t>(ordinate(level));
    const auto x = static_cast<std::uint16_t>(
        (static_cast<std::uint32_t>(index_) * (SpectrumPlotWidth - 1U) +
         (SpectrumLength / 4U)) / (SpectrumLength / 2U));
    auto& column = projection_->columns[x];
    if (!have_last_point_) {
        column.top = coordinate;
        column.bottom = coordinate;
        column.valid = true;
        column.clipped = false;
    } else {
        const auto gap = static_cast<std::uint16_t>(x - last_x_);
        if (gap == 1U) {
            column.top = std::min(last_y_, coordinate);
            column.bottom = std::max(last_y_, coordinate);
        } else if (gap == 2U) {
            const auto middle = static_cast<std::int16_t>(
                (static_cast<std::int32_t>(last_y_) + coordinate + 1) / 2);
            auto& between = projection_->columns[static_cast<std::uint16_t>(x - 1U)];
            between.top = std::min(last_y_, middle);
            between.bottom = std::max(last_y_, middle);
            between.valid = true;
            between.clipped = false;
            column.top = std::min(middle, coordinate);
            column.bottom = std::max(middle, coordinate);
            projection_->interpolated = true;
        } else {
            state_ = SpectrumState::Invalid;
            return;
        }
        column.valid = true;
        column.clipped = false;
    }
    last_x_ = x;
    last_y_ = coordinate;
    have_last_point_ = true;
    if (++index_ > SpectrumLength / 2U) {
        result_.available = true;
        projection_->available = true;
        state_ = SpectrumState::Complete;
        fft_.cancel();
        samples_ = nullptr;
        work_ = nullptr;
        projection_ = nullptr;
    }
}

std::int32_t SpectrumAnalyzer::dbfs_x100(std::uint32_t log_power_q12,
                                          std::uint16_t bin) const noexcept
{
    return dbfs_from_log_power(log_power_q12, bin);
}

std::int32_t SpectrumAnalyzer::ordinate(std::int32_t level) const noexcept
{
    if (level == kNegativeInfinityDbfs) { return SpectrumPlotHeight - 1U; }
    const auto difference = SpectrumDbTopX100 - level;
    if (difference <= 0) { return 0; }
    if (difference >= SpectrumDbRangeX100) { return SpectrumPlotHeight - 1U; }
    const auto scaled = difference * static_cast<std::int32_t>(SpectrumPlotHeight - 1U);
    return (scaled + SpectrumDbRangeX100 / 2) / SpectrumDbRangeX100;
}

std::uint64_t SpectrumAnalyzer::frequency_microhertz(std::uint16_t bin) const noexcept
{
    const auto numerator = static_cast<std::uint64_t>(timer_hz_) * bin * 1000000U;
    const auto denominator = static_cast<std::uint64_t>(divider_) * SpectrumLength;
    return detail::divide_u64(numerator + denominator / 2U, denominator).quotient;
}

std::uint64_t SpectrumAnalyzer::peak_frequency_microhertz() const noexcept
{
    if (state_ != SpectrumState::Complete || !result_.peak_available) { return 0U; }
    return frequency_microhertz(result_.peak_bin);
}

} // namespace dso
