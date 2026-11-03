#include "spectrum.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace dso
{
namespace
{

constexpr std::size_t maximumSamples = 1024U;
constexpr std::size_t maximumPeaks = 8U;
constexpr std::uint16_t maximumAdcCode = 4095U;

std::size_t nextPowerOfTwo(std::size_t value)
{
    std::size_t result = 1U;
    while (result < value)
    {
        result <<= 1U;
    }
    return result;
}

double windowWeight(std::size_t index, std::size_t count, SpectrumWindow window)
{
    if (window == SpectrumWindow::Rectangular || count < 3U)
    {
        return 1.0;
    }
    const double position = static_cast<double>(index) / static_cast<double>(count - 1U);
    const double phase = 2.0 * std::numbers::pi * position;
    if (window == SpectrumWindow::Hann)
    {
        return 0.5 - 0.5 * std::cos(phase);
    }
    return 0.42 - 0.5 * std::cos(phase) + 0.08 * std::cos(2.0 * phase);
}

void fft(std::vector<std::complex<double>> &values)
{
    const std::size_t count = values.size();
    for (std::size_t i = 1U, reversed = 0U; i < count; ++i)
    {
        std::size_t bit = count >> 1U;
        while ((reversed & bit) != 0U)
        {
            reversed ^= bit;
            bit >>= 1U;
        }
        reversed ^= bit;
        if (i < reversed)
        {
            std::swap(values[i], values[reversed]);
        }
    }

    for (std::size_t length = 2U; length <= count; length <<= 1U)
    {
        const double angle = -2.0 * std::numbers::pi / static_cast<double>(length);
        const std::complex<double> step{std::cos(angle), std::sin(angle)};
        for (std::size_t block = 0U; block < count; block += length)
        {
            std::complex<double> rotation{1.0, 0.0};
            const std::size_t half = length / 2U;
            for (std::size_t offset = 0U; offset < half; ++offset)
            {
                const auto even = values[block + offset];
                const auto odd = values[block + offset + half] * rotation;
                values[block + offset] = even + odd;
                values[block + offset + half] = even - odd;
                rotation *= step;
            }
        }
        if (length == count)
        {
            break; // Avoid unsigned overflow when count is the highest power of two.
        }
    }
}

std::vector<double> singleSidedAmplitude(const std::vector<double> &input,
                                         const std::vector<double> &weights,
                                         std::size_t fftSize, double weightSum,
                                         SpectrumDcPolicy dcPolicy)
{
    std::vector<std::complex<double>> values(fftSize);
    double mean = 0.0;
    if (dcPolicy == SpectrumDcPolicy::Remove)
    {
        for (const double value : input)
        {
            mean += value;
        }
        mean /= static_cast<double>(input.size());
    }
    for (std::size_t i = 0U; i < input.size(); ++i)
    {
        values[i] = {(input[i] - mean) * weights[i], 0.0};
    }
    fft(values);

    const std::size_t binCount = fftSize / 2U + 1U;
    std::vector<double> result(binCount);
    for (std::size_t bin = 0U; bin < binCount; ++bin)
    {
        const bool edgeBin = bin == 0U || (fftSize % 2U == 0U && bin == fftSize / 2U);
        const double oneSidedScale = edgeBin ? 1.0 : 2.0;
        result[bin] = std::abs(values[bin]) * oneSidedScale / weightSum;
    }
    return result;
}

std::vector<SpectrumPeak> selectPeaks(const std::vector<double> &amplitudes,
                                      const std::vector<double> &voltageAmplitudes,
                                      double binSpacing, std::size_t peakLimit)
{
    std::vector<SpectrumPeak> peaks;
    if (amplitudes.size() < 3U || peakLimit == 0U)
    {
        return peaks;
    }
    const auto acBegin = amplitudes.begin() + 1;
    const double maximumAcAmplitude = *std::max_element(acBegin, amplitudes.end());
    // Suppress numerical round-off only; this is not a noise or SNR threshold.
    const double numericalFloor = maximumAcAmplitude * 1.0e-12;
    for (std::size_t bin = 1U; bin + 1U < amplitudes.size(); ++bin)
    {
        if (amplitudes[bin] <= numericalFloor || amplitudes[bin] < amplitudes[bin - 1U] ||
            amplitudes[bin] <= amplitudes[bin + 1U])
        {
            continue;
        }

        double offset = 0.0;
        const double left = std::log(std::max(amplitudes[bin - 1U], 1.0e-300));
        const double center = std::log(amplitudes[bin]);
        const double right = std::log(std::max(amplitudes[bin + 1U], 1.0e-300));
        const double curvature = left - 2.0 * center + right;
        if (curvature != 0.0)
        {
            offset = std::clamp(0.5 * (left - right) / curvature, -0.5, 0.5);
        }
        const double interpolatedAmplitude = std::exp(center - 0.25 * (left - right) * offset);
        SpectrumPeak peak;
        peak.bin = bin;
        peak.frequencyHz = (static_cast<double>(bin) + offset) * binSpacing;
        peak.amplitudeCodesPeak = interpolatedAmplitude;
        if (!voltageAmplitudes.empty())
        {
            const double voltageLeft =
                std::log(std::max(voltageAmplitudes[bin - 1U], 1.0e-300));
            const double voltageCenter = std::log(std::max(voltageAmplitudes[bin], 1.0e-300));
            const double voltageRight =
                std::log(std::max(voltageAmplitudes[bin + 1U], 1.0e-300));
            peak.amplitudeMicrovoltsPeak =
                std::exp(voltageCenter - 0.25 * (voltageLeft - voltageRight) * offset);
        }
        peaks.push_back(peak);
    }
    const std::size_t nyquistBin = amplitudes.size() - 1U;
    if (amplitudes[nyquistBin] > numericalFloor &&
        amplitudes[nyquistBin] > amplitudes[nyquistBin - 1U])
    {
        SpectrumPeak peak;
        peak.bin = nyquistBin;
        peak.frequencyHz = static_cast<double>(nyquistBin) * binSpacing;
        peak.amplitudeCodesPeak = amplitudes[nyquistBin];
        if (!voltageAmplitudes.empty())
        {
            peak.amplitudeMicrovoltsPeak = voltageAmplitudes[nyquistBin];
        }
        peaks.push_back(peak);
    }
    std::sort(peaks.begin(), peaks.end(), [](const SpectrumPeak &a, const SpectrumPeak &b) {
        return a.amplitudeCodesPeak > b.amplitudeCodesPeak;
    });
    if (peaks.size() > peakLimit)
    {
        peaks.resize(peakLimit);
    }
    return peaks;
}

} // namespace

SpectrumResult analyzeSpectrum(const Capture &capture, SpectrumWindow window,
                               SpectrumDcPolicy dcPolicy, std::size_t peakLimit)
{
    SpectrumResult result;
    if (!validateCapture(capture) || capture.raw.empty() || capture.raw.size() > maximumSamples ||
        capture.metadata.timerHz == 0U || capture.metadata.divider == 0U)
    {
        return result;
    }

    result.sampleCount = capture.raw.size();
    result.fftSize = nextPowerOfTwo(result.sampleCount);
    result.sampleRateHz = static_cast<double>(capture.metadata.timerHz) / capture.metadata.divider;
    result.binSpacingHz = result.sampleRateHz / static_cast<double>(result.fftSize);
    result.dcRemoved = dcPolicy == SpectrumDcPolicy::Remove;
    result.calibrationAvailable = capture.metadata.calibrationKind != 0U;

    std::vector<double> weights(result.sampleCount);
    double weightSum = 0.0;
    for (std::size_t i = 0U; i < result.sampleCount; ++i)
    {
        weights[i] = windowWeight(i, result.sampleCount, window);
        weightSum += weights[i];
    }
    if (weightSum <= 0.0)
    {
        return result;
    }
    result.coherentGain = weightSum / static_cast<double>(result.sampleCount);

    std::vector<double> codes;
    codes.reserve(result.sampleCount);
    std::vector<double> microvolts;
    if (result.calibrationAvailable)
    {
        microvolts.reserve(result.sampleCount);
    }
    for (const std::uint16_t code : capture.raw)
    {
        if (code == 0U || code == maximumAdcCode)
        {
            result.status = SpectrumStatus::Clipped;
        }
        codes.push_back(code);
        if (result.calibrationAvailable)
        {
            const auto voltage = sampleMicrovolts(capture.metadata, code);
            if (!voltage)
            {
                return SpectrumResult{};
            }
            microvolts.push_back(static_cast<double>(*voltage));
        }
    }

    result.amplitudeCodesPeak = singleSidedAmplitude(codes, weights, result.fftSize,
                                                      weightSum, dcPolicy);
    if (result.calibrationAvailable)
    {
        result.amplitudeMicrovoltsPeak = singleSidedAmplitude(microvolts, weights,
                                                               result.fftSize, weightSum,
                                                               dcPolicy);
        result.dcAmplitudeMicrovolts = result.amplitudeMicrovoltsPeak.front();
    }
    result.dcAmplitudeCodes = result.amplitudeCodesPeak.front();
    result.frequencyHz.resize(result.amplitudeCodesPeak.size());
    for (std::size_t bin = 0U; bin < result.frequencyHz.size(); ++bin)
    {
        result.frequencyHz[bin] = static_cast<double>(bin) * result.binSpacingHz;
    }
    result.dominantPeaks = selectPeaks(result.amplitudeCodesPeak,
                                       result.amplitudeMicrovoltsPeak,
                                       result.binSpacingHz, std::min(peakLimit, maximumPeaks));
    if (result.status != SpectrumStatus::Clipped)
    {
        result.status = SpectrumStatus::Valid;
    }
    return result;
}

} // namespace dso
