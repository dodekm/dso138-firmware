#include "capture_model.hpp"
#include "capture_internal.hpp"
#include "instrument_codes.hpp"
#include "dso_calibration_contract.hpp"
#include "dso_voltage_math.hpp"

#include <QString>
#include <algorithm>

namespace dso
{
namespace
{

using capture_detail::errorResult;
using capture_detail::maxCode;
using capture_detail::maxSamples;

bool validCalibration(const Metadata &m)
{
    return m.calibrationKind == instrument_code::code(instrument_code::CalibrationKind::Unavailable) ||
           (instrument_code::code_is_in_range(
                m.calibrationKind, instrument_code::CalibrationKind::Nominal,
                instrument_code::CalibrationKind::Measured) &&
            calibration_contract::profile_values_valid(m.zeroCodeQ16, m.gainQ16, m.gainShift));
}

} // namespace

bool validateMetadata(const Metadata &m, QString *error)
{
    if (m.token == 0 || m.count == 0 || m.count > maxSamples || m.pretrigger >= m.count)
    {
        return errorResult(error,
                           QStringLiteral("Invalid frame token, sample count or trigger index."));
    }
    if (m.timerHz == 0 || m.divider == 0)
    {
        return errorResult(error, QStringLiteral("Invalid sample timing."));
    }
    if (!instrument_code::code_is_in_range(
            m.coupling, instrument_code::InputCoupling::Unknown,
            instrument_code::InputCoupling::Ground) ||
        !instrument_code::code_is_in_range(
            m.triggerMode, instrument_code::TriggerMode::Normal,
            instrument_code::TriggerMode::Single) ||
        !instrument_code::code_is_in_range(
            m.triggerEdge, instrument_code::TriggerEdge::Rising,
            instrument_code::TriggerEdge::Falling) ||
        (m.flags & instrument_code::capture_flags::ReservedMask) != 0)
    {
        return errorResult(error, QStringLiteral("Unsupported capture metadata or flags."));
    }
    if (m.extendedSettingsValid > 1 ||
        (m.extendedSettingsValid != 0 && m.triggerLevel > maxCode))
    {
        return errorResult(error, QStringLiteral("Invalid extended capture settings."));
    }
    if (!validCalibration(m))
    {
        return errorResult(error,
                           QStringLiteral("Calibration metadata is outside firmware bounds."));
    }
    if (error)
    {
        error->clear();
    }
    return true;
}

bool validateCapture(const Capture &c, QString *error)
{
    if (!validateMetadata(c.metadata, error))
    {
        return false;
    }
    if (c.raw.size() != c.metadata.count)
    {
        return errorResult(error,
                           QStringLiteral("Sample count does not match the capture metadata."));
    }
    if (std::any_of(c.raw.begin(), c.raw.end(), [](std::uint16_t v) { return v > maxCode; }))
    {
        return errorResult(error, QStringLiteral("A sample exceeds the 12-bit ADC range."));
    }
    return true;
}

double sampleTime(const Metadata &m, std::size_t index)
{
    return (static_cast<double>(index) - m.pretrigger) *
           (static_cast<double>(m.divider) / m.timerHz);
}

std::optional<std::int64_t> sampleMicrovolts(const Metadata &m, std::uint16_t code)
{
    if (m.calibrationKind == instrument_code::code(instrument_code::CalibrationKind::Unavailable) ||
        !validCalibration(m) || code > maxCode)
    {
        return std::nullopt;
    }
    // Calibration bounds keep this signed product below 2^60.
    const std::int64_t delta = static_cast<std::int64_t>(code) * 65536 - m.zeroCodeQ16;
    const auto product = delta * static_cast<std::int64_t>(m.gainQ16);
    return detail::round_gain_product(product, m.gainShift);
}

} // namespace dso
