#include "capture_io.hpp"
#include "capture_internal.hpp"
#include "capture_model.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <cmath>
#include <limits>
#include <utility>

namespace dso
{
namespace
{

using capture_detail::errorResult;
using capture_detail::format;
using capture_detail::maxCode;
using capture_detail::maxFileBytes;
using capture_detail::maxSamples;

template <class Meta, class Visitor>
void visitMetadata(Meta &m, Visitor visit, bool includeExtended = true)
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

} // namespace

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
        if (capture.metadata.hostIntervalBeforeMs != UnavailableHostIntervalMs)
            bytes += QByteArray::number(capture.metadata.hostIntervalBeforeMs);
        bytes += '\n';
    }
    return writeFile(path, bytes, error);
}

} // namespace dso
