#ifndef DSO_SPECTRUM_HPP
#define DSO_SPECTRUM_HPP

#include "dso_fft.hpp"
#include "dso_scaling.hpp"

#include <cstdint>

namespace dso {

inline constexpr std::uint16_t SpectrumLength{512U};
inline constexpr std::uint16_t SpectrumPlotWidth{280U};
inline constexpr std::uint16_t SpectrumPlotTop{22U};
inline constexpr std::uint16_t SpectrumPlotHeight{176U};
inline constexpr std::int32_t SpectrumDbRangeX100{8000};
inline constexpr std::int32_t SpectrumDbTopX100{1000};
inline constexpr std::int32_t SpectrumDbBottomX100{-7000};
static_assert(SpectrumPlotWidth <= WaveformColumnCapacity,
              "Spectrum plot must fit the configured projection buffer");

enum class SpectrumState : std::uint8_t {
    Idle, ClearingProjection, Mean, Preparing, Transforming, Projecting,
    Complete, Invalid, Cancelled
};

struct SpectrumResult final {
    bool available{};
    bool peak_available{};
    std::uint16_t peak_bin{};
    std::int16_t peak_dbfs_x100{-32768};
    std::uint64_t bin_spacing_microhertz{};
    std::uint64_t nyquist_microhertz{};
};
static_assert(sizeof(SpectrumResult) == 24U, "Keep spectrum result footprint compact");

class SpectrumAnalyzer final {
public:
    SpectrumAnalyzer() = default;
    SpectrumAnalyzer(const SpectrumAnalyzer&) = delete;
    SpectrumAnalyzer& operator=(const SpectrumAnalyzer&) = delete;
    SpectrumAnalyzer(SpectrumAnalyzer&&) = delete;
    SpectrumAnalyzer& operator=(SpectrumAnalyzer&&) = delete;

    // Analyzes one uniformly sampled 512-sample slice from the immutable frame.
    // Uses mean removal and a periodic Hann window. Input frame, projection and
    // exclusive workspace remain owned through completion or cancel; workspace
    // is overwritten in place and its borrow is released automatically at done.
    [[nodiscard]] bool begin(const std::uint16_t* samples, std::uint16_t count,
                             std::uint16_t start, const FrameMetadata& frame,
                             std::int32_t* work, std::uint16_t capacity_words,
                             WaveformProjection& projection) noexcept;
    [[nodiscard]] SpectrumState step(std::uint16_t max_operations) noexcept;
    void cancel() noexcept;
    [[nodiscard]] SpectrumState state() const noexcept { return state_; }
    [[nodiscard]] const SpectrumResult* result() const noexcept
    { return state_ == SpectrumState::Complete ? &result_ : nullptr; }
    // Returns zero unless a completed analysis found a nonzero peak above DC.
    [[nodiscard]] std::uint64_t peak_frequency_microhertz() const noexcept;

private:
    void reset() noexcept;
    void mean_one() noexcept;
    void prepare_one() noexcept;
    void project_one() noexcept;
    [[nodiscard]] std::int32_t dbfs_x100(std::uint32_t log_power_q12,
                                         std::uint16_t bin) const noexcept;
    [[nodiscard]] std::int32_t ordinate(std::int32_t dbfs) const noexcept;
    [[nodiscard]] std::uint64_t frequency_microhertz(std::uint16_t bin) const noexcept;

    const std::uint16_t* samples_{};
    std::int32_t* work_{};
    WaveformProjection* projection_{};
    SpectrumResult result_{};
    dsp::RealFft fft_{};
    SpectrumState state_{SpectrumState::Idle};
    std::uint16_t start_{};
    std::uint16_t capacity_words_{};
    std::uint16_t index_{};
    std::uint16_t last_x_{};
    std::int16_t last_y_{};
    std::uint32_t sample_sum_{};
    std::uint32_t peak_score_{};
    std::int32_t packed_real_{};
    std::uint32_t timer_hz_{};
    std::uint32_t divider_{};
    std::int32_t mean_q16_{};
    bool have_last_point_{};
};

} // namespace dso

#endif // DSO_SPECTRUM_HPP
