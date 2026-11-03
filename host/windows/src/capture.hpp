#pragma once

#include <QString>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace dso
{

struct Metadata
{
    std::uint32_t token{};
    std::uint16_t count{};
    std::uint16_t pretrigger{};
    std::uint32_t timerHz{};
    std::uint32_t divider{};
    std::uint32_t acquisitionEpoch{};
    std::uint32_t triggerSequence{};
    std::uint32_t physicalGeneration{};
    std::uint32_t microvoltsPerDivision{};
    std::int32_t zeroCodeQ16{};
    std::uint32_t gainQ16{};
    std::uint8_t gainShift{};
    std::uint8_t calibrationKind{};
    std::uint8_t coupling{};
    std::uint8_t triggerMode{};
    std::uint8_t triggerEdge{};
    std::uint8_t flags{};
    std::uint8_t timebaseIndex{};
    std::uint8_t timebaseCatalogueId{};
    std::uint16_t triggerLevel{};
    std::uint8_t extendedSettingsValid{};
    std::uint32_t hostIntervalBeforeMs{0xffffffffU};

    bool operator==(const Metadata &) const = default;
};

struct Capture
{
    Metadata metadata;
    std::vector<std::uint16_t> raw;
};

bool validateMetadata(const Metadata &, QString *error = nullptr);
bool validateCapture(const Capture &, QString *error = nullptr);
double sampleTime(const Metadata &, std::size_t index);
std::optional<std::int64_t> sampleMicrovolts(const Metadata &, std::uint16_t code);

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
    // Fraction from 0.0 to 1.0; present only when period and edge timing are reliable.
    std::optional<double> dutyCycleFraction;
    MeasurementFrequencyStatus frequencyStatus{MeasurementFrequencyStatus::TooFewCycles};
    bool adcClipped{};
    bool calibrationAvailable{};
};
Measurements measure(const Capture &);

Capture demoCapture();
bool saveCapture(const QString &path, const Capture &, QString *error = nullptr);
std::optional<Capture> loadCapture(const QString &path, QString *error = nullptr);
bool exportCsv(const QString &path, const Capture &, QString *error = nullptr);

} // namespace dso
