#include "window.hpp"
#include "capture_model.hpp"
#include "measurements.hpp"
#include "spectrum_view.hpp"
#include "waveform.hpp"
#include "window_format.hpp"
#include "instrument_codes.hpp"
#include "dso_timebase_contract.hpp"

#include <QAbstractItemView>
#include <QComboBox>
#include <QEvent>
#include <QLabel>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTimer>
#include <algorithm>
#include <iterator>
#include <utility>

namespace dso
{
using window_format::classicTimebases;
using window_format::couplingText;
using window_format::frequencyText;
using window_format::maxHistory;
using window_format::measurementValue;
using window_format::periodText;
using window_format::periodicQuality;
using window_format::rateText;
using window_format::scaledVoltage;
using window_format::timebaseText;
using window_format::triggerEdgeText;
using window_format::triggerModeText;

void MainWindow::onFrameReady(Capture frame)
{
    const QString source = session_.liveCaptureActive()
                               ? QStringLiteral("Live successive capture")
                           : session_.repeatedCaptureActive()
                               ? QStringLiteral("Repeated Single")
                               : QStringLiteral("Live capture");
    const auto metadata = frame.metadata;
    const bool preserveView = session_.liveCaptureActive() && liveDisplay_.receivedFrames > 0 &&
                              !freezeDisplay_;
    if (records_->view()->isVisible() || !pendingHistory_.empty())
    {
        if (pendingHistory_.size() == maxHistory)
            pendingHistory_.pop_front();
        pendingHistory_.push_back({std::move(frame), source, replaceOnNextFrame_, preserveView});
    }
    else
    {
        addRecord(std::move(frame), source, replaceOnNextFrame_, !freezeDisplay_, preserveView);
        if (!session_.liveCaptureActive())
            report(QStringLiteral("Capture added to history (%1 of %2).")
                       .arg(history_.size())
                       .arg(maxHistory));
    }
    replaceOnNextFrame_ = false;
    if (session_.liveCaptureActive())
        ++liveDisplay_.receivedFrames;
    const auto &m = metadata;
    if (session_.liveCaptureActive() && m.hostIntervalBeforeMs != UnavailableHostIntervalMs)
    {
        const double updateRate = m.hostIntervalBeforeMs == 0
                                      ? 0.0
                                      : 1000.0 / static_cast<double>(m.hostIntervalBeforeMs);
        const std::uint32_t epochStep = liveDisplay_.hasPreviousAcquisitionEpoch
                                            ? m.acquisitionEpoch - liveDisplay_.previousAcquisitionEpoch
                                            : 0;
        liveTiming_->setText(QStringLiteral("%1 records/s · %2 ms · epoch +%3")
                             .arg(updateRate, 0, 'g', 4)
                             .arg(m.hostIntervalBeforeMs)
                             .arg(liveDisplay_.hasPreviousAcquisitionEpoch
                                      ? QString::number(epochStep)
                                      : QStringLiteral("—")));
        liveTiming_->show();
    }
    liveDisplay_.previousAcquisitionEpoch = m.acquisitionEpoch;
    liveDisplay_.hasPreviousAcquisitionEpoch = true;
}

void MainWindow::showRecord(int index, bool preserveView)
{
    if (index < 0 || index >= static_cast<int>(history_.size()))
        return;
    const auto &rec = history_[static_cast<std::size_t>(index)];
    if (preserveView)
        plot_->setCapture(rec.capture, WaveformWidget::ViewPolicy::Preserve);
    else
        plot_->setCapture(rec.capture);
    if (plots_->currentWidget() == spectrum_)
        spectrum_->setCapture(rec.capture);
    source_->setText(rec.source);
    const auto &m = rec.capture->metadata;
    const double sampleRate =
        (m.timerHz && m.divider) ? static_cast<double>(m.timerHz) / m.divider : 0.0;
    const QString triggerMode = triggerModeText(m.triggerMode);
    const QString triggerEdge = (m.flags & instrument_code::capture_flags::EdgeTriggered) == 0
                                    ? QStringLiteral("no edge")
                                : triggerEdgeText(m.triggerEdge);
    const QString frozenSettings = m.extendedSettingsValid != 0
                                       ? QStringLiteral("%1 · trigger level %2 ADC")
                                             .arg(m.timebaseCatalogueId == timebase_contract::ClassicCatalogueId &&
                                                          m.timebaseIndex < classicTimebases.size()
                                                      ? timebaseText(classicTimebases[m.timebaseIndex])
                                                      : QStringLiteral("Timebase index %1 (catalogue %2)")
                                                            .arg(m.timebaseIndex)
                                                            .arg(m.timebaseCatalogueId))
                                             .arg(m.triggerLevel)
                                       : QStringLiteral("Timebase and trigger level unavailable");
    const QString triggerPosition = m.count > 1
                                        ? QStringLiteral("Trigger sample %1/%2 · %3% pre-trigger")
                                              .arg(m.pretrigger)
                                              .arg(m.count - 1)
                                              .arg(100.0 * m.pretrigger / (m.count - 1), 0, 'f', 1)
                                        : QStringLiteral("Trigger position unavailable for one sample");
    const QString interval = m.hostIntervalBeforeMs == UnavailableHostIntervalMs
                                  ? QStringLiteral("Host interval before this record: —")
                                  : QStringLiteral("Host interval before this record: %1 ms")
                                        .arg(m.hostIntervalBeforeMs);
    recordInfo_->setText(QStringLiteral("%1 samples  ·  %2 pre-trigger\n%3  ·  epoch %4\n%5 / %6\n%7\n%8\n%9")
                             .arg(rec.capture->raw.size())
                             .arg(m.pretrigger)
                             .arg(rateText(sampleRate))
                             .arg(m.acquisitionEpoch)
                             .arg(triggerMode)
                             .arg(triggerEdge)
                             .arg(frozenSettings)
                             .arg(triggerPosition)
                             .arg(interval));
    const QString cal = m.calibrationKind == instrument_code::code(instrument_code::CalibrationKind::Measured)
                            ? QStringLiteral("Measured calibration")
                        : m.calibrationKind == instrument_code::code(instrument_code::CalibrationKind::Nominal)
                            ? QStringLiteral("Nominal calibration")
                                                 : QStringLiteral("Unavailable · raw ADC only");
    const QString coupling = couplingText(m.coupling);
    analysisLabels_.calibration->setText(
        QStringLiteral("%1\n%2/div  ·  %3")
            .arg(cal, scaledVoltage(m.microvoltsPerDivision), coupling));
    const auto stats = measure(*rec.capture);
    analysisLabels_.peakToPeak->setText(
        measurementValue(stats.vppMicrovolts, stats.rawVpp));
    analysisLabels_.minimum->setText(
        measurementValue(stats.minimumMicrovolts, stats.rawMinimum));
    analysisLabels_.maximum->setText(
        measurementValue(stats.maximumMicrovolts, stats.rawMaximum));
    analysisLabels_.mean->setText(
        measurementValue(stats.meanMicrovolts, stats.rawMean));
    analysisLabels_.rms->setText(
        measurementValue(stats.totalRmsMicrovolts, stats.rawTotalRms));
    analysisLabels_.acRms->setText(
        measurementValue(stats.acRmsMicrovolts, stats.rawAcRms));
    analysisLabels_.frequency->setText(
        stats.frequencyHz ? frequencyText(*stats.frequencyHz) : QStringLiteral("—"));
    analysisLabels_.period->setText(
        stats.periodSeconds ? periodText(*stats.periodSeconds) : QStringLiteral("—"));
    analysisLabels_.duty->setText(stats.dutyCycleFraction
        ? QStringLiteral("%1%").arg(100.0 * *stats.dutyCycleFraction, 0, 'f', 1)
        : QStringLiteral("—"));
    const auto quality = periodicQuality(stats.frequencyStatus);
    analysisLabels_.quality->setText(quality);
    analysisLabels_.quality->setVisible(!quality.isEmpty());
    updateControls();
}

void MainWindow::addRecord(Capture capture, QString source, bool replace, bool selectLatest,
                           bool preserveView)
{
    std::shared_ptr<const Capture> keepDisplayed;
    if (!selectLatest && records_->currentIndex() >= 0 &&
        records_->currentIndex() < static_cast<int>(history_.size()))
        keepDisplayed = history_[static_cast<std::size_t>(records_->currentIndex())].capture;
    auto shared = std::make_shared<const Capture>(std::move(capture));
    const bool demoPinned = freezeDisplay_ && keepDisplayed && !history_.empty() &&
                            keepDisplayed == history_.front().capture;
    if (replace && !demoPinned && !history_.empty() &&
        history_.front().source == QStringLiteral("Built-in demonstration waveform"))
    {
        history_.erase(history_.begin());
        const QSignalBlocker blocker(records_);
        records_->removeItem(0);
    }
    history_.insert(history_.begin(), Record{std::move(shared), std::move(source)});
    const auto titleFor = [](const Record &record) {
        const auto epoch = record.capture->metadata.acquisitionEpoch;
        if (record.source == QStringLiteral("Built-in demonstration waveform"))
            return QStringLiteral("Demo");
        if (record.source == QStringLiteral("Live successive capture"))
            return QStringLiteral("Live %1").arg(epoch);
        if (record.source == QStringLiteral("Repeated Single"))
            return QStringLiteral("Repeat %1").arg(epoch);
        if (record.source == QStringLiteral("Live capture"))
            return QStringLiteral("Capture %1").arg(epoch);
        return record.source;
    };
    {
        const QSignalBlocker blocker(records_);
        const auto &record = history_.front();
        records_->insertItem(0, QStringLiteral("%1 · %2 samples")
                                    .arg(titleFor(record))
                                    .arg(record.capture->raw.size()));
    }
    if (history_.size() > maxHistory)
    {
        std::size_t evict = history_.size() - 1;
        if (keepDisplayed && history_.back().capture == keepDisplayed)
            evict = history_.size() - 2;
        history_.erase(history_.begin() + static_cast<std::ptrdiff_t>(evict));
        const QSignalBlocker blocker(records_);
        records_->removeItem(static_cast<int>(evict));
    }
    int selected = 0;
    if (keepDisplayed)
    {
        const auto found = std::find_if(history_.begin(), history_.end(), [&keepDisplayed](const Record &r) {
            return r.capture == keepDisplayed;
        });
        if (found != history_.end())
            selected = static_cast<int>(std::distance(history_.begin(), found));
    }
    {
        const QSignalBlocker blocker(records_);
        records_->setCurrentIndex(selected);
    }
    if (selectLatest || !keepDisplayed)
        showRecord(selected, preserveView);
}

void MainWindow::flushPendingHistory()
{
    if (pendingHistory_.empty() || records_->view()->isVisible())
        return;
    const bool preserveView = pendingHistory_.back().preserveView;
    for (auto &record : pendingHistory_)
        addRecord(std::move(record.capture), std::move(record.source), record.replaceDemo, false);
    pendingHistory_.clear();
    if (!freezeDisplay_)
    {
        const QSignalBlocker blocker(records_);
        records_->setCurrentIndex(0);
        showRecord(0, preserveView);
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Hide && records_ && watched == records_->view())
    {
        // The popup may close before QComboBox commits the activated item.
        QTimer::singleShot(0, this, &MainWindow::flushPendingHistory);
    }
    return QMainWindow::eventFilter(watched, event);
}

} // namespace dso
