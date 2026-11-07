#pragma once

#include <cstdint>
#include <optional>

namespace dso
{

struct Capture;

enum class MeasurementFrequencyStatus : std::uint8_t
{
    Valid,
    InvalidInput,
    InvalidTiming,
    Clipped,
    TooFewCycles,
    LowAmplitude,
    TooFast,
    Unstable
};

struct Measurements
{
    // Raw statistics are expressed in ADC codes. Calibrated values
    // below use microvolts; periodic results use Hz, seconds and a fraction.
    std::optional<double> rawMinimum;
    std::optional<double> rawMaximum;
    std::optional<double> rawVpp;
    std::optional<double> rawMean;
    std::optional<double> rawTotalRms;
    std::optional<double> rawAcRms;
    std::optional<double> minimumMicrovolts;
    std::optional<double> maximumMicrovolts;
    std::optional<double> vppMicrovolts;
    std::optional<double> meanMicrovolts;
    std::optional<double> totalRmsMicrovolts;
    std::optional<double> acRmsMicrovolts;
    std::optional<double> frequencyHz;
    std::optional<double> periodSeconds;
    // Fraction from 0.0 to 1.0 when period and edge timing are reliable.
    std::optional<double> dutyCycleFraction;
    MeasurementFrequencyStatus frequencyStatus{MeasurementFrequencyStatus::TooFewCycles};
    bool adcClipped{};
    bool calibrationAvailable{};
};

// Computes measurements from an immutable raw capture and its calibration.
Measurements measure(const Capture &);

} // namespace dso
