#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

class QString;

namespace dso
{

inline constexpr std::uint32_t UnavailableHostIntervalMs{0xffffffffU};

struct Metadata
{
    std::uint32_t token{};
    std::uint16_t count{}; // Number of raw ADC samples in the capture.
    std::uint16_t pretrigger{}; // Sample index at the trigger; this maps to t = 0.
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
    std::uint8_t flags{}; // See instrument_code::capture_flags.
    std::uint8_t timebaseIndex{};
    std::uint8_t timebaseCatalogueId{};
    std::uint16_t triggerLevel{};
    std::uint8_t extendedSettingsValid{};
    // Host-side interval between record publications; the sentinel means
    // unavailable or saturated and is not the instrument sample period.
    std::uint32_t hostIntervalBeforeMs{UnavailableHostIntervalMs};

    bool operator==(const Metadata &) const = default;
};

struct Capture
{
    Metadata metadata;
    // Owned raw 12-bit ADC codes; valid records contain metadata.count samples.
    std::vector<std::uint16_t> raw;
};

bool validateMetadata(const Metadata &, QString *error = nullptr);
bool validateCapture(const Capture &, QString *error = nullptr);
double sampleTime(const Metadata &, std::size_t index);
std::optional<std::int64_t> sampleMicrovolts(const Metadata &, std::uint16_t code);

} // namespace dso
