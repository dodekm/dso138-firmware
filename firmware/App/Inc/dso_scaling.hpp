#ifndef DSO_SCALING_HPP
#define DSO_SCALING_HPP

#include "dso_projection_limits.hpp"
#include "dso_timebase_contract.hpp"
#include "dso_timebase.hpp"
#include "dso_voltage_calibration.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace dso {

struct FrameMetadata;

inline constexpr std::uint8_t ScopeTimebaseCount{timebase_contract::ClassicTimebaseCatalogueEntries};
// 10,20,50 us/div ... 1,2,5 s/div; invalid index returns0.
[[nodiscard]] std::uint32_t scope_timebase_us(std::uint8_t index) noexcept;
enum class ScopeTimebaseSupport : std::uint8_t { Invalid, CircularDma };
struct ScopeTimebasePlan final {
    TimebasePlan timing;
    ScopeTimebaseSupport support;
};
// Fixed72MHz/12MHz7.5cycle profile; cap500k samples/s. All catalog entries
// fit the supported circular-DMA backend. Failure leaves out unchanged.
// Configuration-time only.
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
    [[nodiscard]] std::int32_t sample_y(std::uint16_t sample) const noexcept;
    [[nodiscard]] std::int32_t segment_y(std::int64_t column) const noexcept;
    void merge(std::uint16_t column, std::int32_t y) noexcept;
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
    std::int32_t previous_y_{};
    std::int64_t target_x_{};
    std::int32_t target_y_{};
    std::int32_t draw_x_{};
    std::int32_t draw_end_{};
    bool have_previous_{};
    bool drawing_{};
};

} // namespace dso
#endif
