#include "window_format.hpp"
#include "instrument_codes.hpp"
#include "instrument_labels.hpp"
#include "measurements.hpp"
#include "protocol.hpp"

#include <cmath>
#include <utility>

#include <QStringList>

namespace dso::window_format
{
QString scaledVoltage(double uv)
{
    const double a = std::abs(uv);
    if (a >= 1e6)
        return QString::number(uv / 1e6, 'f', 3) + QStringLiteral(" V");
    if (a >= 1e3)
        return QString::number(uv / 1e3, 'f', 3) + QStringLiteral(" mV");
    return QString::number(a < 0.05 ? 0.0 : uv, 'f', 1) + QStringLiteral(" µV");
}
QString rateText(double rate)
{
    if (rate >= 1e6)
        return QStringLiteral("%1 MS/s").arg(rate / 1e6, 0, 'g', 5);
    if (rate >= 1e3)
        return QStringLiteral("%1 kS/s").arg(rate / 1e3, 0, 'g', 5);
    return QStringLiteral("%1 S/s").arg(rate, 0, 'g', 5);
}
QString measurementValue(std::optional<double> microvolts, std::optional<double> codes)
{
    if (microvolts)
        return scaledVoltage(*microvolts);
    if (codes)
        return QStringLiteral("%1 ADC").arg(*codes, 0, 'f', 1);
    return QStringLiteral("—");
}
QString frequencyText(double frequency)
{
    if (frequency >= 1e6)
        return QStringLiteral("%1 MHz").arg(frequency / 1e6, 0, 'g', 5);
    if (frequency >= 1e3)
        return QStringLiteral("%1 kHz").arg(frequency / 1e3, 0, 'g', 5);
    return QStringLiteral("%1 Hz").arg(frequency, 0, 'g', 5);
}
QString periodText(double seconds)
{
    if (seconds >= 1)
        return QStringLiteral("%1 s").arg(seconds, 0, 'g', 5);
    if (seconds >= 1e-3)
        return QStringLiteral("%1 ms").arg(seconds * 1e3, 0, 'g', 5);
    return QStringLiteral("%1 µs").arg(seconds * 1e6, 0, 'g', 5);
}
QString periodicQuality(MeasurementFrequencyStatus status)
{
    switch (status)
    {
    case MeasurementFrequencyStatus::Valid: return {};
    case MeasurementFrequencyStatus::InvalidInput: return QStringLiteral("Invalid capture");
    case MeasurementFrequencyStatus::InvalidTiming: return QStringLiteral("Invalid sample timing");
    case MeasurementFrequencyStatus::Clipped: return QStringLiteral("ADC clipping · periodic readings unavailable");
    case MeasurementFrequencyStatus::TooFewCycles: return QStringLiteral("Too few complete cycles");
    case MeasurementFrequencyStatus::LowAmplitude: return QStringLiteral("Signal below periodic detection threshold");
    case MeasurementFrequencyStatus::TooFast: return QStringLiteral("Too few samples per period");
    case MeasurementFrequencyStatus::Unstable: return QStringLiteral("Unstable crossings · check signal or timebase");
    }
    return {};
}
QString triggerModeText(quint8 mode)
{
    return QString::fromLatin1(instrument_label::trigger_mode_name(mode));
}
QString triggerEdgeText(quint8 edge)
{
    return edge == instrument_code::code(instrument_code::TriggerEdge::Rising)
               ? QStringLiteral("rising")
               : QStringLiteral("falling");
}
QString couplingText(quint8 coupling)
{
    return coupling == instrument_code::code(instrument_code::InputCoupling::Dc)
               ? QStringLiteral("DC")
           : coupling == instrument_code::code(instrument_code::InputCoupling::Ac)
               ? QStringLiteral("AC")
           : coupling == instrument_code::code(instrument_code::InputCoupling::Ground)
               ? QStringLiteral("GND")
               : QStringLiteral("unknown");
}
QString timebaseText(quint32 microseconds)
{
    if (microseconds >= 1000000)
        return QStringLiteral("%1 s/div").arg(static_cast<double>(microseconds) / 1000000.0, 0, 'g', 4);
    if (microseconds >= 1000)
        return QStringLiteral("%1 ms/div").arg(static_cast<double>(microseconds) / 1000.0, 0, 'g', 4);
    return QStringLiteral("%1 µs/div").arg(microseconds);
}

QString instrumentSettingsText(const protocol::Settings &s,
                               protocol::SettingsAvailability availability, bool connected)
{
    if (availability == protocol::SettingsAvailability::Unknown)
        return connected ? QStringLiteral("Waiting for firmware readback…")
                         : QStringLiteral("Offline · settings not available");
    if (availability == protocol::SettingsAvailability::Legacy)
        return QStringLiteral(
            "Legacy firmware · capture download remains available; live settings readback is unsupported.");

    const QString state = s.uiState == instrument_code::code(instrument_code::UiState::Hold)
                              ? QStringLiteral("HOLD")
                          : s.uiState == instrument_code::code(instrument_code::UiState::WaitingForControls)
                              ? QStringLiteral("Waiting for controls")
                          : s.uiState == instrument_code::code(instrument_code::UiState::WaitingForBackend)
                              ? QStringLiteral("Waiting for backend")
                          : s.uiState == instrument_code::code(instrument_code::UiState::ArmPending)
                              ? QStringLiteral("Arm pending")
                          : s.uiState == instrument_code::code(instrument_code::UiState::Acquiring)
                              ? QStringLiteral("Acquiring")
                          : s.uiState == instrument_code::code(instrument_code::UiState::FrameReady)
                              ? QStringLiteral("Frame ready")
                              : QStringLiteral("Fault");
    const QString runHold = (s.flags & protocol::settings_flags::RunIntent) != 0
                                ? QStringLiteral("RUN intent")
                                : QStringLiteral("HOLD");
    const QString mode = triggerModeText(s.triggerMode);
    const QString edge = triggerEdgeText(s.triggerEdge);
    const QString coupling = couplingText(s.coupling);
    QStringList writable;
    const std::array<std::pair<quint8, const char *>, 6> writableNames{{
        {protocol::settings_writable::Timebase, "timebase"},
        {protocol::settings_writable::TriggerMode, "trigger mode"},
        {protocol::settings_writable::TriggerEdge, "trigger edge"},
        {protocol::settings_writable::TriggerLevel, "trigger level"},
        {protocol::settings_writable::TriggerPosition, "trigger position"},
        {protocol::settings_writable::LocalRunPause, "HOLD local RUN"},
    }};
    for (const auto &[mask, name] : writableNames)
        if ((s.writableMask & mask) != 0)
            writable.push_back(QString::fromLatin1(name));
    const QString writeSummary = writable.isEmpty() ? QStringLiteral("none")
                                                     : writable.join(QStringLiteral(", "));
    const QString sampleRate = s.timerHz == 0
                                   ? QStringLiteral("unavailable")
                                   : rateText(static_cast<double>(s.timerHz) / s.divider);
    const QString stale = availability == protocol::SettingsAvailability::Stale
                              ? QStringLiteral("STALE · last acknowledged readback\n")
                              : QString();
    return stale + QStringLiteral("Run: %1 · state %2 · revision %3\nTimebase: %4 µs/div · sample rate %5\nTrigger: %6 · %7 · %8 ADC · position %9‰\nInput: %10 · %11/div · hardware switches read-only\nRemote writable: %12 · writes admissible: %13")
                        .arg(runHold, state)
                        .arg(s.revision)
                        .arg(s.timebaseMicrosecondsPerDivision)
                        .arg(sampleRate)
                        .arg(mode, edge)
                        .arg(s.triggerLevel)
                        .arg(s.triggerPositionPermille)
                        .arg(coupling)
                        .arg(scaledVoltage(s.microvoltsPerDivision))
                        .arg(writeSummary)
                        .arg((s.flags & protocol::settings_flags::WriteAdmissibleNow) != 0
                                 ? QStringLiteral("yes")
                                 : QStringLiteral("no"));
}
} // namespace dso::window_format
