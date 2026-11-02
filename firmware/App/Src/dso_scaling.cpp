#include "dso_scaling.hpp"
#include "dso_timebase_presets.hpp"
#include "dso_voltage_math.hpp"
#include "dso_fixed_ratio.hpp"
#include <algorithm>

namespace dso {
namespace {
constexpr std::uint32_t Timebases[ScopeTimebaseCount]{
    10U,20U,50U,100U,200U,500U,1000U,2000U,5000U,10000U,20000U,50000U,
    100000U,200000U,500000U,1000000U,2000000U,5000000U
};
constexpr std::uint32_t catalogue_timebase(std::uint8_t index) noexcept
{
    if (index >= ScopeTimebaseCount) { return 0U; }
    const auto part = index % 3U;
    auto value = part == 0U ? 10U : part == 1U ? 20U : 50U;
    // At most five decades; the largest result is 5,000,000 us/div.
    for (auto decade = static_cast<unsigned>(index / 3U); decade != 0U; --decade) { value *= 10U; }
    return value;
}
constexpr bool timebase_catalogue_matches() noexcept
{
    if (sizeof(detail::PresetTimebases)/sizeof(detail::PresetTimebases[0]) != ScopeTimebaseCount) { return false; }
    for (unsigned index{}; index < ScopeTimebaseCount; ++index) {
        if (Timebases[index] != detail::PresetTimebases[index] ||
            Timebases[index] != catalogue_timebase(static_cast<std::uint8_t>(index))) { return false; }
    }
    return true;
}
static_assert(timebase_catalogue_matches(),
              "Regenerate timer presets when the timebase catalogue changes");
static_assert(ScopeTimebaseCount == 18U && DmaHalfSamples == 512U &&
              sizeof(detail::ScopePresets)/sizeof(detail::ScopePresets[0]) == ScopeTimebaseCount,
              "Timer presets require the current fixed capture profile");
constexpr std::int32_t MaximumZero{4095 * 65536};

bool settings_valid(const FrameMetadata& metadata, const ProjectionSettings& view) noexcept
{
    const auto& timing = metadata.timing;
    return view.time_us_per_div >= 10U && view.time_us_per_div <= Timebases[ScopeTimebaseCount-1U] &&
        view.width >= 2U && view.width <= WaveformColumnCapacity && view.height >= 2U &&
        view.height <= DisplayHeight && view.pixels_per_div > 0U &&
        view.pixels_per_div <= DisplayHeight && view.trigger_position_permille <= 1000U &&
        view.horizontal_offset_samples >= -1024 && view.horizontal_offset_samples <= 1024 &&
        metadata.physical.generation != 0U && metadata.physical.microvolts_per_div >= 10000U &&
        metadata.physical.microvolts_per_div <= 5000000U &&
        (metadata.physical.coupling == InputCoupling::Dc ||
         metadata.physical.coupling == InputCoupling::Ac ||
         metadata.physical.coupling == InputCoupling::Ground) &&
        timing.timer_hz >= 1000000U && timing.timer_hz <= 72000000U && timing.divider != 0U &&
        (std::uint64_t{timing.prescaler} + 1U) * (std::uint64_t{timing.period} + 1U) == timing.divider;
}

std::int64_t rounded_signed(std::int64_t numerator, std::uint64_t denominator) noexcept
{
    const auto magnitude = static_cast<std::uint64_t>(numerator < 0 ? -numerator : numerator);
    const auto q = magnitude / denominator;
    const auto r = magnitude % denominator;
    const auto rounded = static_cast<std::int64_t>(q + (r >= (denominator + 1U)/2U ? 1U : 0U));
    return numerator < 0 ? -rounded : rounded;
}

std::int64_t sample_position_x(std::uint16_t index, std::uint16_t pretrigger,
                               std::uint32_t divider, std::uint32_t timer_hz,
                               const ProjectionSettings& view) noexcept
{
    const auto delta = std::int64_t{index} - pretrigger - view.horizontal_offset_samples;
    const auto magnitude = static_cast<std::uint64_t>(delta < 0 ? -delta : delta);
    // Valid capture/pan bounds give magnitude <= 2047 and width <= 320;
    // this unscaled product fits uint64_t even at the largest divider.
    const auto numerator = magnitude * divider * view.width;
    const auto q = detail::million_ratio(numerator,
        std::uint64_t{timer_hz} * view.time_us_per_div * 10U);
    const auto offset = static_cast<std::int64_t>(q.quotient +
        (delta < 0 && q.remainder != 0U ? 1U : 0U));
    const auto trigger = std::uint32_t{view.trigger_position_permille} * view.width / 1000U;
    return std::int64_t{trigger} + (delta < 0 ? -offset : offset);
}
}

bool calibration_valid(VoltageCalibration calibration) noexcept
{
    return (calibration.kind == CalibrationKind::Nominal || calibration.kind == CalibrationKind::Measured) &&
        calibration.zero_code_q16 >= 0 && calibration.zero_code_q16 <= MaximumZero &&
        calibration.microvolts_per_code_q16 != 0U && calibration.gain_shift <= 2U &&
        (calibration.gain_shift == 0U || calibration.microvolts_per_code_q16 >= 0x80000000U);
}

bool code_to_microvolts(std::uint16_t code, VoltageCalibration calibration,
                         std::int64_t& microvolts) noexcept
{
    if (code > 4095U || !calibration_valid(calibration)) { return false; }
    const auto delta = std::int64_t{code} * 65536 - calibration.zero_code_q16;
    // |delta|<=268369920, gain<=UINT32_MAX: product<2^60, including rounding.
    microvolts = detail::round_gain_product(delta * calibration.microvolts_per_code_q16,
        calibration.gain_shift);
    return true;
}

std::uint32_t scope_timebase_us(std::uint8_t index) noexcept
{
    return catalogue_timebase(index);
}

bool plan_scope_timebase(std::uint8_t index, ScopeTimebasePlan& out) noexcept
{
    if (index >= ScopeTimebaseCount) { return false; }
    // Generated from the original exact factor search, including its lower-PSC
    // tie rule. The catalogue comparison above rejects stale generated data.
    const auto pair = detail::ScopePresets[index];
    const auto divider = (std::uint32_t{pair.prescaler}+1U) * (std::uint32_t{pair.period}+1U);
    if (std::uint64_t{divider} * DmaHalfSamples >= std::uint64_t{0x80000000U}) { return false; }
    out = ScopeTimebasePlan{{pair.prescaler,pair.period,72000000U,divider,12000000U,46U},
                            ScopeTimebaseSupport::CircularDma};
    return true;
}

bool project_code_y(std::uint16_t code, const FrameMetadata& metadata,
                    VoltageCalibration calibration, const ProjectionSettings& view,
                    std::int32_t& y) noexcept
{
    if (code > 4095U || !settings_valid(metadata,view)) { return false; }
    std::int64_t result{};
    if (calibration.kind == CalibrationKind::Unavailable) {
        result = static_cast<std::int64_t>((std::uint32_t{4095U-code} * (view.height-1U) + 2047U)/4095U) +
            view.vertical_offset_pixels;
    } else {
        std::int64_t voltage{};
        if (!code_to_microvolts(code,calibration,voltage)) { return false; }
        result = view.height/2U + std::int64_t{view.vertical_offset_pixels} -
            rounded_signed(voltage * view.pixels_per_div,metadata.physical.microvolts_per_div);
    }
    if (result < INT32_MIN || result > INT32_MAX) { return false; }
    y = static_cast<std::int32_t>(result);
    return true;
}

bool project_trigger_x(const FrameMetadata& metadata, const ProjectionSettings& view,
                        std::int64_t& x) noexcept
{
    if (!settings_valid(metadata,view) || metadata.capture.length == 0U ||
        metadata.capture.length > DisplayMaxSamples ||
        metadata.capture.pretrigger >= metadata.capture.length) { return false; }
    x = sample_position_x(metadata.capture.pretrigger,metadata.capture.pretrigger,
                          metadata.timing.divider,
                          metadata.timing.timer_hz,view);
    return true;
}

bool WaveformProjector::begin(const std::uint16_t* samples, std::uint16_t count,
                              const FrameMetadata& metadata, VoltageCalibration calibration,
                              ProjectionSettings view, WaveformProjection& output) noexcept
{
    output.available = false;
    state_ = ProjectionState::Invalid;
    if (samples == nullptr || count == 0U || count > DisplayMaxSamples ||
        metadata.capture.length != count || metadata.capture.pretrigger >= count ||
        !settings_valid(metadata,view) ||
        (calibration.kind != CalibrationKind::Unavailable && !calibration_valid(calibration))) { return false; }
    for (auto& column : output.columns) { column = ProjectionColumn{}; }
    output.width = view.width; output.height = view.height;
    output.interpolated = false; output.raw_codes = calibration.kind == CalibrationKind::Unavailable;
    samples_ = samples; count_ = count; pretrigger_ = metadata.capture.pretrigger;
    physical_microvolts_per_div_ = metadata.physical.microvolts_per_div;
    timer_hz_ = metadata.timing.timer_hz; divider_ = metadata.timing.divider;
    calibration_ = calibration;
    view_ = view; output_ = &output; next_sample_ = 0U;
    have_previous_ = false; drawing_ = false;
    state_ = ProjectionState::Projecting;
    return true;
}

std::int64_t WaveformProjector::sample_x(std::uint16_t index) const noexcept
{
    return sample_position_x(index,pretrigger_,divider_,timer_hz_,view_);
}

std::int32_t WaveformProjector::sample_y(std::uint16_t sample) const noexcept
{
    // begin() validated the copied scales; step() checked this 12-bit code.
    // Their accepted bounds keep |voltage| <= 1,073,479,680 uV and |y| <
    // 25.8 million, so the public conversion's int32 range check cannot fail.
    if (calibration_.kind == CalibrationKind::Unavailable) {
        return static_cast<std::int32_t>((std::uint32_t{4095U-sample} * (view_.height-1U) + 2047U)/4095U) +
            view_.vertical_offset_pixels;
    }
    const auto delta = std::int64_t{sample} * 65536 - calibration_.zero_code_q16;
    const auto voltage = detail::round_gain_product(delta * calibration_.microvolts_per_code_q16,
                                                     calibration_.gain_shift);
    return static_cast<std::int32_t>(view_.height/2U + std::int64_t{view_.vertical_offset_pixels} -
        rounded_signed(voltage * view_.pixels_per_div,physical_microvolts_per_div_));
}

std::int32_t WaveformProjector::segment_y(std::int64_t column) const noexcept
{
    // Exact endpoints need no interpolation or rounding. The general path
    // below retains its full-width product/remainder handling for interiors.
    if (column == previous_x_) { return previous_y_; }
    if (column == target_x_) { return target_y_; }
    const auto dy = target_y_ - previous_y_;
    const auto delta_x = static_cast<std::uint64_t>(column - previous_x_);
    const auto width = static_cast<std::uint64_t>(target_x_ - previous_x_);
    // Adjacent source samples have |dy| < 2^26 and |delta_x| < 2^34:
    // validated voltage calibration bounds |sample_y| below 25.8M, and the
    // largest accepted divider/viewport ratio is below 2^34. Their product
    // is below 2^60, so the exact signed interpolation numerator fits uint64.
    const auto product = static_cast<std::uint64_t>(dy < 0 ? -dy : dy) * delta_x;
    auto whole = product / width;
    const auto remainder = product % width;
    if (remainder >= (width+1U)/2U) { whole++; }
    return static_cast<std::int32_t>(previous_y_ + (dy < 0 ? -static_cast<std::int64_t>(whole) :
                                  static_cast<std::int64_t>(whole)));
}

void WaveformProjector::merge(std::uint16_t column, std::int32_t y) noexcept
{
    auto& result = output_->columns[column];
    const bool clipped = y < 0 || y >= view_.height;
    const auto bounded = static_cast<std::int16_t>(std::clamp<std::int32_t>(y,0,static_cast<std::int32_t>(view_.height)-1));
    if (!result.valid) { result.top = bounded; result.bottom = bounded; result.valid = true; }
    else { result.top = std::min(result.top,bounded); result.bottom = std::max(result.bottom,bounded); }
    result.clipped = result.clipped || clipped;
}

ProjectionStep WaveformProjector::step(std::uint16_t sample_budget,
                                      std::uint16_t column_budget) noexcept
{
    ProjectionStep used{state_,0U,0U};
    while (state_ == ProjectionState::Projecting) {
        if (drawing_) {
            if (used.columns == column_budget) { break; }
            if (target_x_ == previous_x_) {
                merge(static_cast<std::uint16_t>(draw_x_),previous_y_);
                merge(static_cast<std::uint16_t>(draw_x_),target_y_);
            } else {
                // A column owns the row interval from its preceding raster
                // position to its current one. Clamp that interval to this
                // observed segment, so steep adjacent columns stay connected
                // without extrapolating before/after the source record.
                const auto previous_column = std::max(previous_x_,std::int64_t{draw_x_}-1);
                merge(static_cast<std::uint16_t>(draw_x_),segment_y(previous_column));
                merge(static_cast<std::uint16_t>(draw_x_),segment_y(draw_x_));
            }
            used.columns++;
            if (draw_x_ == draw_end_) {
                drawing_ = false;
                previous_x_ = target_x_; previous_y_ = target_y_;
            } else { draw_x_++; }
            continue;
        }
        if (next_sample_ == count_) {
            state_ = ProjectionState::Complete; output_->available = true; break;
        }
        if (used.samples == sample_budget) { break; }
        const auto sample = samples_[next_sample_];
        used.samples++;
        if (sample > 4095U) { state_ = ProjectionState::Invalid; output_->available = false; break; }
        target_x_ = sample_x(next_sample_); target_y_ = sample_y(sample); next_sample_++;
        if (!have_previous_) {
            previous_x_ = target_x_; previous_y_ = target_y_; have_previous_ = true;
        }
        const auto first = std::max<std::int64_t>(0,previous_x_);
        const auto last = std::min<std::int64_t>(view_.width-1U,target_x_);
        if (first <= last) {
            draw_x_ = static_cast<std::int32_t>(first); draw_end_ = static_cast<std::int32_t>(last);
            drawing_ = true;
            if (target_x_ - previous_x_ > 1) { output_->interpolated = true; }
        } else { previous_x_ = target_x_; previous_y_ = target_y_; }
    }
    used.state = state_;
    return used;
}

void WaveformProjector::cancel() noexcept
{
    if (state_ == ProjectionState::Projecting && output_ != nullptr) { output_->available = false; }
    state_ = ProjectionState::Idle;
    samples_ = nullptr; output_ = nullptr; drawing_ = false;
}
} // namespace dso
