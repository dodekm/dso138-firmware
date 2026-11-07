#pragma once

#include <cstddef>
#include <optional>
#include <vector>

namespace dso
{

struct Capture;

enum class SpectrumWindow
{
    Rectangular,
    Hann,
    Blackman
};

enum class SpectrumDcPolicy
{
    Remove,
    Preserve
};

enum class SpectrumStatus
{
    Valid,
    InvalidInput,
    Clipped
};

struct SpectrumPeak
{
    std::size_t bin{};
    double frequencyHz{};
    double amplitudeCodesPeak{};
    std::optional<double> amplitudeMicrovoltsPeak;
};

struct SpectrumResult
{
    SpectrumStatus status{SpectrumStatus::InvalidInput};
    std::size_t sampleCount{};
    std::size_t fftSize{};
    double sampleRateHz{};
    double binSpacingHz{};
    double coherentGain{};
    bool dcRemoved{};
    bool calibrationAvailable{};
    double dcAmplitudeCodes{};
    std::optional<double> dcAmplitudeMicrovolts;
    std::vector<double> frequencyHz;
    std::vector<double> amplitudeCodesPeak;
    // Empty when the capture has no usable calibration.
    std::vector<double> amplitudeMicrovoltsPeak;
    std::vector<SpectrumPeak> dominantPeaks;
};

// Computes a single-sided peak-amplitude spectrum for up to 1024 raw ADC
// samples. Non-power-of-two records are zero-padded to the next power of two;
// this interpolates the displayed frequency grid but does not add information.
// The amplitude normalization uses the sum of the actual analysis window.
[[nodiscard]] SpectrumResult analyzeSpectrum(
    const Capture &capture, SpectrumWindow window = SpectrumWindow::Hann,
    SpectrumDcPolicy dcPolicy = SpectrumDcPolicy::Remove, std::size_t peakLimit = 8U);

} // namespace dso
