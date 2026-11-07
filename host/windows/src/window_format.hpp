#pragma once

#include "dso_timebase_contract.hpp"

#include <QtGlobal>
#include <QString>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace dso
{
enum class MeasurementFrequencyStatus : std::uint8_t;
namespace protocol
{
struct Settings;
enum class SettingsAvailability : quint8;
}
} // namespace dso

namespace dso::window_format
{
inline constexpr std::size_t maxHistory = 100;
inline constexpr auto classicTimebases = timebase_contract::ClassicMicrosecondsPerDivision;

QString scaledVoltage(double microvolts);
QString rateText(double samplesPerSecond);
QString measurementValue(std::optional<double> microvolts, std::optional<double> codes);
QString frequencyText(double hertz);
QString periodText(double seconds);
QString periodicQuality(MeasurementFrequencyStatus status);
QString triggerModeText(quint8 mode);
QString triggerEdgeText(quint8 edge);
QString couplingText(quint8 coupling);
QString timebaseText(quint32 microseconds);
QString instrumentSettingsText(const protocol::Settings &settings,
                               protocol::SettingsAvailability availability, bool connected);
} // namespace dso::window_format
