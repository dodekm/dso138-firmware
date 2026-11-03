#include "capture.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <type_traits>

namespace dso
{
namespace
{

constexpr std::uint16_t maxCode = 4095;
constexpr std::uint16_t maxSamples = 1024;
constexpr qint64 maxFileBytes = 32768;
constexpr auto format = "DSO138 capture";

bool errorResult(QString *error, QString reason)
{
    if (error)
    {
        *error = std::move(reason);
    }
    return false;
}

bool validCalibration(const Metadata &m)
{
    return m.calibrationKind == 0 ||
           (m.calibrationKind <= 2 && m.zeroCodeQ16 >= 0 && m.zeroCodeQ16 <= maxCode * 65536 &&
            m.gainQ16 != 0 && m.gainShift <= 2 && (m.gainShift == 0 || m.gainQ16 >= 0x80000000U));
}

template <class Meta, class Visitor> void visitMetadata(Meta &m, Visitor visit, bool includeExtended = true)
{
    visit("token", m.token);
    visit("count", m.count);
    visit("pretrigger", m.pretrigger);
    visit("timerHz", m.timerHz);
    visit("divider", m.divider);
    visit("acquisitionEpoch", m.acquisitionEpoch);
    visit("triggerSequence", m.triggerSequence);
    visit("physicalGeneration", m.physicalGeneration);
    visit("microvoltsPerDivision", m.microvoltsPerDivision);
    visit("zeroCodeQ16", m.zeroCodeQ16);
    visit("gainQ16", m.gainQ16);
    visit("gainShift", m.gainShift);
    visit("calibrationKind", m.calibrationKind);
    visit("coupling", m.coupling);
    visit("triggerMode", m.triggerMode);
    visit("triggerEdge", m.triggerEdge);
    visit("flags", m.flags);
    if (includeExtended)
    {
        visit("timebaseIndex", m.timebaseIndex);
        visit("timebaseCatalogueId", m.timebaseCatalogueId);
        visit("triggerLevel", m.triggerLevel);
        visit("extendedSettingsValid", m.extendedSettingsValid);
        visit("hostIntervalBeforeMs", m.hostIntervalBeforeMs);
    }
}

template <class T> bool readInteger(QJsonValue input, T &result)
{
    const double value = input.toDouble(std::numeric_limits<double>::quiet_NaN());
    if (!std::isfinite(value) || std::trunc(value) != value ||
        value < std::numeric_limits<T>::lowest() || value > std::numeric_limits<T>::max())
    {
        return false;
    }
    result = static_cast<T>(value);
    return true;
}

bool writeFile(const QString &path, const QByteArray &bytes, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
    {
        return errorResult(error, file.errorString());
    }
    if (error)
    {
        error->clear();
    }
    return true;
}

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
    if (low == 0U || high == maxCode)
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
    if (m.coupling > 3 || m.triggerMode > 2 || m.triggerEdge > 1 || (m.flags & 0xf7U) != 0)
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
    if (m.calibrationKind == 0 || !validCalibration(m) || code > maxCode)
    {
        return std::nullopt;
    }
    // The validated 28-bit delta times a 32-bit gain fits in uint64_t.
    const std::int64_t delta = static_cast<std::int64_t>(code) * 65536 - m.zeroCodeQ16;
    const std::uint64_t magnitude = static_cast<std::uint64_t>(std::abs(delta)) * m.gainQ16;
    const unsigned bits = 32U - m.gainShift;
    const auto rounded =
        static_cast<std::int64_t>((magnitude + (std::uint64_t{1} << (bits - 1))) >> bits);
    return delta < 0 ? -rounded : rounded;
}

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
    result.adcClipped = *low == 0U || *high == maxCode;
    result.calibrationAvailable = c.metadata.calibrationKind != 0U;

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

Capture demoCapture()
{
    Capture capture;
    auto &m = capture.metadata;
    m.token = 1;
    m.count = 512;
    m.pretrigger = 128;
    m.timerHz = 72000000;
    m.divider = 720;
    m.acquisitionEpoch = 1;
    m.triggerSequence = 128;
    m.physicalGeneration = 1;
    m.microvoltsPerDivision = 1000000;
    m.zeroCodeQ16 = 2048 * 65536;
    m.gainQ16 = 65536000;
    m.calibrationKind = 1;
    m.coupling = 1;
    m.flags = 8;
    capture.raw.reserve(m.count);
    for (std::size_t i = 0; i < m.count; ++i)
    {
        const double radians = 2.0 * std::numbers::pi * 5 * static_cast<double>(i) / m.count;
        capture.raw.push_back(
            static_cast<std::uint16_t>(std::lround(2048 + 900 * std::sin(radians))));
    }
    return capture;
}

bool saveCapture(const QString &path, const Capture &capture, QString *error)
{
    if (!validateCapture(capture, error))
    {
        return false;
    }
    QJsonObject metadata;
    visitMetadata(capture.metadata, [&metadata](const char *name, auto value) {
        metadata.insert(QString::fromLatin1(name), static_cast<qint64>(value));
    });
    QJsonArray samples;
    for (const auto code : capture.raw)
    {
        samples.append(static_cast<int>(code));
    }
    const QJsonObject root{
        {"format", format}, {"version", 2}, {"metadata", metadata}, {"raw", samples}};
    return writeFile(path, QJsonDocument(root).toJson(QJsonDocument::Compact), error);
}

std::optional<Capture> loadCapture(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        errorResult(error, file.errorString());
        return std::nullopt;
    }
    const QByteArray bytes = file.read(maxFileBytes + 1);
    if (bytes.size() > maxFileBytes || !file.atEnd())
    {
        errorResult(error, QStringLiteral("Capture file exceeds the size limit."));
        return std::nullopt;
    }
    const auto root = QJsonDocument::fromJson(bytes).object();
    const int version = root.value("version").toInt(-1);
    if (root.value("format").toString() != format || (version != 1 && version != 2) ||
        !root.value("metadata").isObject() || !root.value("raw").isArray())
    {
        errorResult(error, QStringLiteral("Unsupported or malformed DSO138 capture file."));
        return std::nullopt;
    }

    const auto metadata = root.value("metadata").toObject();
    const auto samples = root.value("raw").toArray();
    Capture capture;
    bool metadataOk = true;
    visitMetadata(capture.metadata, [&metadata, &metadataOk](const char *name, auto &field) {
        metadataOk = readInteger(metadata.value(QString::fromLatin1(name)), field) && metadataOk;
    }, version >= 2);
    if (!metadataOk || samples.size() != capture.metadata.count || samples.size() > maxSamples)
    {
        errorResult(error, QStringLiteral("Invalid capture metadata or sample count."));
        return std::nullopt;
    }
    capture.raw.reserve(capture.metadata.count);
    for (const auto value : samples)
    {
        std::uint16_t code{};
        if (!readInteger(value, code) || code > maxCode)
        {
            errorResult(error, QStringLiteral("Invalid ADC sample."));
            return std::nullopt;
        }
        capture.raw.push_back(code);
    }
    return validateCapture(capture, error) ? std::optional<Capture>(std::move(capture))
                                           : std::nullopt;
}

bool exportCsv(const QString &path, const Capture &capture, QString *error)
{
    if (!validateCapture(capture, error))
    {
        return false;
    }
    QByteArray bytes =
        "time_seconds,raw_code,voltage_microvolts,acquisition_epoch,pretrigger,timer_hz,timer_divider,trigger_sequence,physical_generation,coupling,calibration_kind,microvolts_per_division,zero_code_q16,gain_q16,gain_shift,trigger_mode,trigger_edge,capture_flags,timebase_catalogue_id,timebase_index,trigger_level_adc,host_interval_before_ms\n";
    for (std::size_t i = 0; i < capture.raw.size(); ++i)
    {
        bytes += QByteArray::number(sampleTime(capture.metadata, i), 'g', 17) + ',';
        bytes += QByteArray::number(capture.raw[i]) + ',';
        if (const auto microvolts = sampleMicrovolts(capture.metadata, capture.raw[i]))
        {
            bytes += QByteArray::number(static_cast<qlonglong>(*microvolts));
        }
        bytes += ',';
        bytes += QByteArray::number(capture.metadata.acquisitionEpoch);
        bytes += ',';
        bytes += QByteArray::number(capture.metadata.pretrigger) + ',' +
                 QByteArray::number(capture.metadata.timerHz) + ',' +
                 QByteArray::number(capture.metadata.divider) + ',' +
                 QByteArray::number(capture.metadata.triggerSequence) + ',' +
                 QByteArray::number(capture.metadata.physicalGeneration) + ',' +
                 QByteArray::number(capture.metadata.coupling) + ',' +
                 QByteArray::number(capture.metadata.calibrationKind) + ',' +
                 QByteArray::number(capture.metadata.microvoltsPerDivision) + ',' +
                 QByteArray::number(capture.metadata.zeroCodeQ16) + ',' +
                 QByteArray::number(capture.metadata.gainQ16) + ',' +
                 QByteArray::number(capture.metadata.gainShift) + ',' +
                 QByteArray::number(capture.metadata.triggerMode) + ',' +
                 QByteArray::number(capture.metadata.triggerEdge) + ',' +
                 QByteArray::number(capture.metadata.flags) + ',';
        if (capture.metadata.extendedSettingsValid != 0)
            bytes += QByteArray::number(capture.metadata.timebaseCatalogueId);
        bytes += ',';
        if (capture.metadata.extendedSettingsValid != 0)
            bytes += QByteArray::number(capture.metadata.timebaseIndex);
        bytes += ',';
        if (capture.metadata.extendedSettingsValid != 0)
            bytes += QByteArray::number(capture.metadata.triggerLevel);
        bytes += ',';
        if (capture.metadata.hostIntervalBeforeMs != 0xffffffffU)
            bytes += QByteArray::number(capture.metadata.hostIntervalBeforeMs);
        bytes += '\n';
    }
    return writeFile(path, bytes, error);
}

} // namespace dso
