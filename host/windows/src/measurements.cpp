#include "measurements.hpp"

#include "capture_model.hpp"
#include "instrument_codes.hpp"
#include "dso_adc_contract.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

namespace dso
{
namespace
{
struct FrequencyEstimate
{
    std::optional<double> frequencyHz;
    std::optional<double> periodSeconds;
    std::optional<double> dutyCycleFraction;
    MeasurementFrequencyStatus status{MeasurementFrequencyStatus::TooFewCycles};
};

double median(std::vector<double> values)
{
    const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2U);
    std::nth_element(values.begin(), middle, values.end());
    if (values.size() % 2U != 0U)
    {
        return *middle;
    }
    const double upper = *middle;
    const double lower = *std::max_element(values.begin(), middle);
    return (lower + upper) / 2.0;
}

double crossingPosition(std::size_t index, double previous, double current, double threshold)
{
    const double difference = current - previous;
    if (difference == 0.0)
    {
        return static_cast<double>(index);
    }
    return static_cast<double>(index - 1U) + (threshold - previous) / difference;
}

FrequencyEstimate estimateFrequency(const Capture &capture, std::uint16_t low,
                                    std::uint16_t high)
{
    FrequencyEstimate result;
    if (capture.metadata.timerHz == 0U || capture.metadata.divider == 0U)
    {
        result.status = MeasurementFrequencyStatus::InvalidTiming;
        return result;
    }
    if (low == 0U || high == adc_contract::MaximumCode)
    {
        result.status = MeasurementFrequencyStatus::Clipped;
        return result;
    }
    const double amplitude = static_cast<double>(high - low);
    if (amplitude < 8.0)
    {
        result.status = MeasurementFrequencyStatus::LowAmplitude;
        return result;
    }

    const double midpoint = (static_cast<double>(low) + high) / 2.0;
    const double hysteresis = std::max(2.0, amplitude * 0.08);
    const double lowThreshold = midpoint - hysteresis;
    const double highThreshold = midpoint + hysteresis;
    bool armedLow = capture.raw.front() <= lowThreshold;
    bool highState = false;
    std::optional<double> risingCandidate;
    std::optional<double> fallingCandidate;
    std::optional<double> activeRising;
    std::vector<double> risingEdges;
    std::vector<double> pulseWidths;
    risingEdges.reserve(capture.raw.size() / 4U);
    pulseWidths.reserve(capture.raw.size() / 4U);

    for (std::size_t i = 1U; i < capture.raw.size(); ++i)
    {
        const double previous = capture.raw[i - 1U];
        const double current = capture.raw[i];
        if (!highState)
        {
            if (current <= lowThreshold)
            {
                armedLow = true;
                risingCandidate.reset();
            }
            else if (armedLow && previous < midpoint && current >= midpoint)
            {
                risingCandidate = crossingPosition(i, previous, current, midpoint);
            }
            if (armedLow && risingCandidate && current >= highThreshold)
            {
                activeRising = risingCandidate;
                risingEdges.push_back(*risingCandidate);
                risingCandidate.reset();
                armedLow = false;
                highState = true;
            }
        }
        else
        {
            if (previous > midpoint && current <= midpoint)
            {
                fallingCandidate = crossingPosition(i, previous, current, midpoint);
            }
            if (fallingCandidate && current <= lowThreshold)
            {
                if (activeRising)
                {
                    pulseWidths.push_back(*fallingCandidate - *activeRising);
                }
                fallingCandidate.reset();
                activeRising.reset();
                highState = false;
                armedLow = true;
            }
        }
    }

    if (risingEdges.size() < 3U)
    {
        result.status = MeasurementFrequencyStatus::TooFewCycles;
        return result;
    }
    std::vector<double> periods;
    periods.reserve(risingEdges.size() - 1U);
    for (std::size_t i = 1U; i < risingEdges.size(); ++i)
    {
        periods.push_back(risingEdges[i] - risingEdges[i - 1U]);
    }
    const double periodSamples = median(periods);
    if (periodSamples < 4.0)
    {
        result.status = MeasurementFrequencyStatus::TooFast;
        return result;
    }
    for (const double period : periods)
    {
        if (std::abs(period - periodSamples) > periodSamples * 0.12)
        {
            result.status = MeasurementFrequencyStatus::Unstable;
            return result;
        }
    }
    if (pulseWidths.size() < periods.size())
    {
        result.status = MeasurementFrequencyStatus::TooFewCycles;
        return result;
    }

    const double timerHz = capture.metadata.timerHz;
    const double sampleInterval = static_cast<double>(capture.metadata.divider) / timerHz;
    result.periodSeconds = periodSamples * sampleInterval;
    result.frequencyHz = 1.0 / *result.periodSeconds;
    std::vector<double> duties;
    duties.reserve(periods.size());
    for (std::size_t i = 0U; i < periods.size(); ++i)
    {
        if (pulseWidths[i] <= 0.0 || pulseWidths[i] >= periods[i])
        {
            result.status = MeasurementFrequencyStatus::Unstable;
            result.frequencyHz.reset();
            result.periodSeconds.reset();
            return result;
        }
        duties.push_back(pulseWidths[i] / periods[i]);
    }
    result.dutyCycleFraction = median(std::move(duties));
    result.status = MeasurementFrequencyStatus::Valid;
    return result;
}

} // namespace

Measurements measure(const Capture &c)
{
    Measurements result;
    if (!validateCapture(c))
    {
        result.frequencyStatus = MeasurementFrequencyStatus::InvalidInput;
        return result;
    }
    const auto [low, high] = std::minmax_element(c.raw.begin(), c.raw.end());
    const double count = static_cast<double>(c.raw.size());
    result.rawMinimum = *low;
    result.rawMaximum = *high;
    result.rawVpp = static_cast<double>(*high - *low);
    result.adcClipped = *low == 0U || *high == adc_contract::MaximumCode;
    result.calibrationAvailable = c.metadata.calibrationKind !=
                                  instrument_code::code(instrument_code::CalibrationKind::Unavailable);

    double mean = 0.0;
    double rawCenteredSquares = 0.0;
    double rawSquares = 0;
    double voltsMean = 0.0;
    double voltsCenteredSquares = 0.0;
    double voltsSquares = 0;
    std::size_t sampleIndex = 0U;
    for (const auto sample : c.raw)
    {
        const double rawValue = sample;
        const double rawDelta = rawValue - mean;
        mean += rawDelta / static_cast<double>(++sampleIndex);
        rawCenteredSquares += rawDelta * (rawValue - mean);
        rawSquares += rawValue * rawValue;
        if (const auto microvolts = sampleMicrovolts(c.metadata, sample))
        {
            const auto voltage = static_cast<double>(*microvolts);
            const double voltageDelta = voltage - voltsMean;
            voltsMean += voltageDelta / static_cast<double>(sampleIndex);
            voltsCenteredSquares += voltageDelta * (voltage - voltsMean);
            voltsSquares += voltage * voltage;
        }
    }
    result.rawMean = mean;
    result.rawTotalRms = std::sqrt(rawSquares / count);
    result.rawAcRms = std::sqrt(std::max(0.0, rawCenteredSquares / count));
    if (result.calibrationAvailable)
    {
        result.minimumMicrovolts = static_cast<double>(*sampleMicrovolts(c.metadata, *low));
        result.maximumMicrovolts = static_cast<double>(*sampleMicrovolts(c.metadata, *high));
        result.vppMicrovolts = static_cast<double>(*sampleMicrovolts(c.metadata, *high) -
                                                   *sampleMicrovolts(c.metadata, *low));
        result.meanMicrovolts = voltsMean;
        result.totalRmsMicrovolts = std::sqrt(voltsSquares / count);
        result.acRmsMicrovolts = std::sqrt(std::max(0.0, voltsCenteredSquares / count));
    }
    const auto frequency = estimateFrequency(c, *low, *high);
    result.frequencyStatus = frequency.status;
    result.frequencyHz = frequency.frequencyHz;
    result.periodSeconds = frequency.periodSeconds;
    result.dutyCycleFraction = frequency.dutyCycleFraction;
    return result;
}


} // namespace dso
