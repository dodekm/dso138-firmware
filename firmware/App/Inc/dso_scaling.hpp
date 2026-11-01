#ifndef DSO_SCALING_HPP
#define DSO_SCALING_HPP

#include "dso_acquisition.hpp"
#include "dso_display.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace dso {

enum class CalibrationKind : std::uint8_t { Unavailable, Nominal, Measured };
struct VoltageCalibration final {
    std::int32_t zero_code_q16;
    std::uint32_t microvolts_per_code_q16; // Mantissa; effective Q16 gain is multiplied by 2^gain_shift.
    CalibrationKind kind;
    std::uint8_t gain_shift{};
};
static_assert(sizeof(VoltageCalibration) == 12U && alignof(VoltageCalibration) == 4U);
static_assert(offsetof(VoltageCalibration, gain_shift) == 9U);
[[nodiscard]] bool calibration_valid(VoltageCalibration calibration) noexcept;
// Nearest integer microvolt; half ties are rounded away from zero. No default
// board gain or voltage reference is inferred. Failure leaves output unchanged.
[[nodiscard]] bool code_to_microvolts(std::uint16_t code, VoltageCalibration calibration,
                                     std::int64_t& microvolts) noexcept;

inline constexpr std::uint8_t ScopeTimebaseCount{24U};
#if defined(DSO_PROJECTION_COLUMN_CAPACITY)
inline constexpr std::uint16_t WaveformColumnCapacity{DSO_PROJECTION_COLUMN_CAPACITY};
#else
inline constexpr std::uint16_t WaveformColumnCapacity{DisplayWidth};
#endif
static_assert(WaveformColumnCapacity >= 2U && WaveformColumnCapacity <= DisplayWidth,
              "Projection capacity must fit the LCD");
// 10,20,50 us/div ... 100,200,500 s/div; invalid index returns0.
[[nodiscard]] std::uint32_t scope_timebase_us(std::uint8_t index) noexcept;
enum class ScopeTimebaseSupport : std::uint8_t { Invalid, CircularDma, NeedsSlowBackend, Rolling };
struct ScopeTimebasePlan final {
    TimebasePlan timing;
    ScopeTimebaseSupport support;
};
// Fixed current72MHz/12MHz7.5cycle profile; cap500k samples/s. PSC/ARR chosen
// from exact requested full-screen duration and1024-point record. Slow settings
// can be represented by TIM2 but are explicitly NOT supported by the current
// DWT/half-ring monitor. Failure leaves out unchanged. Configuration-time only.
[[nodiscard]] bool plan_scope_timebase(std::uint8_t index, ScopeTimebasePlan& out) noexcept;

struct ProjectionSettings final {
    std::uint32_t time_us_per_div;
    std::uint16_t width;
    std::uint16_t height;
    std::uint16_t pixels_per_div;
    std::uint16_t trigger_position_permille;
    std::int16_t vertical_offset_pixels; // Positive moves the zero line down.
    std::int16_t horizontal_offset_samples; // Positive views later source data.
};
struct ProjectionColumn final {
    std::int16_t top;
    std::int16_t bottom;
    bool valid;
    bool clipped;
};
struct WaveformProjection final {
    std::array<ProjectionColumn, WaveformColumnCapacity> columns{};
    std::uint16_t width{};
    std::uint16_t height{};
    bool available{};
    bool interpolated{}; // Lines between sparse samples; never extra samples.
    bool raw_codes{}; // Explicit Unavailable calibration: no volts inferred.
};
// Same unclipped mapping used by projector; explicit Unavailable selects raw
// codes, not volts. Coordinates are relative to the viewport. Failure leaves y
// unchanged. Physical nominal sensitivity must remain in10mV..5V/div bounds.
[[nodiscard]] bool project_code_y(std::uint16_t code, const FrameMetadata& metadata,
                                 VoltageCalibration calibration, const ProjectionSettings& view,
                                 std::int32_t& y) noexcept;
// Position of the captured trigger sample after sample panning. Shares the
// projector's exact rational signed floor, including fractional negative x.
// Unclipped/exclusive-right-edge coordinates; failure leaves x unchanged.
[[nodiscard]] bool project_trigger_x(const FrameMetadata& metadata,
                                    const ProjectionSettings& view, std::int64_t& x) noexcept;
enum class ProjectionState : std::uint8_t { Idle, Projecting, Complete, Invalid };
struct ProjectionStep final {
    ProjectionState state;
    std::uint16_t samples;
    std::uint16_t columns;
};

// Owns only traversal state; output storage belongs to the caller. begin clears
// at most320 output columns. Source/output remain alive and exclusively owned
// through Complete/Invalid/cancel; no shared DMA or mutable renderer buffer.
// Metadata/calibration/view are copied on begin, so HOLD uses its frozen gain
// label/timebase even after present physical switches or requests change.
class WaveformProjector final {
public:
    WaveformProjector() = default;
    WaveformProjector(const WaveformProjector&) = delete;
    WaveformProjector& operator=(const WaveformProjector&) = delete;
    WaveformProjector(WaveformProjector&&) = delete;
    WaveformProjector& operator=(WaveformProjector&&) = delete;
    [[nodiscard]] bool begin(const std::uint16_t* samples, std::uint16_t count,
                             const FrameMetadata& metadata, VoltageCalibration calibration,
                             ProjectionSettings view, WaveformProjection& output) noexcept;
    [[nodiscard]] ProjectionStep step(std::uint16_t sample_budget,
                                      std::uint16_t column_budget) noexcept;
    void cancel() noexcept;
    [[nodiscard]] ProjectionState state() const noexcept { return state_; }
private:
    [[nodiscard]] std::int64_t sample_x(std::uint16_t index) const noexcept;
    [[nodiscard]] std::int64_t sample_y(std::uint16_t sample) const noexcept;
    [[nodiscard]] std::int64_t segment_y(std::int64_t column) const noexcept;
    void merge(std::uint16_t column, std::int64_t y) noexcept;
    const std::uint16_t* samples_{};
    WaveformProjection* output_{};
    // Minimal immutable geometry copied only after begin() validates the full
    // frame: the traversal uses no other FrameMetadata fields.
    std::uint16_t pretrigger_{};
    std::uint32_t physical_microvolts_per_div_{};
    std::uint32_t timer_hz_{};
    std::uint32_t divider_{};
    VoltageCalibration calibration_{};
    ProjectionSettings view_{};
    ProjectionState state_{ProjectionState::Idle};
    std::uint16_t count_{};
    std::uint16_t next_sample_{};
    std::int64_t previous_x_{};
    std::int64_t previous_y_{};
    std::int64_t target_x_{};
    std::int64_t target_y_{};
    std::int32_t draw_x_{};
    std::int32_t draw_end_{};
    bool have_previous_{};
    bool drawing_{};
};

} // namespace dso
#endif
