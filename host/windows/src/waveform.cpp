#include "waveform.hpp"
#include "capture_model.hpp"
#include "instrument_codes.hpp"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <limits>

namespace dso
{
namespace
{
constexpr QColor background{17, 27, 44};
constexpr QColor grid{49, 67, 88};
constexpr QColor muted{151, 171, 190};
constexpr QColor cyan{80, 215, 232};
constexpr QColor amber{255, 183, 77};
constexpr QColor triggerColor{168, 153, 244};
constexpr double minimumSpan = 1e-12;

double niceStep(double range, int target)
{
    if (!(range > 0.0) || !std::isfinite(range))
        return 1.0;
    const double raw = range / target;
    const double power = std::pow(10.0, std::floor(std::log10(raw)));
    const double fraction = raw / power;
    const double factor = fraction <= 1.0   ? 1.0
                          : fraction <= 2.0 ? 2.0
                          : fraction <= 5.0 ? 5.0
                                            : 10.0;
    return factor * power;
}
QString timeLabel(double seconds)
{
    const double a = std::abs(seconds);
    if (a == 0.0)
        return QStringLiteral("0");
    if (a < 1e-6)
        return QString::number(seconds * 1e9, 'g', 4) + QStringLiteral(" ns");
    if (a < 1e-3)
        return QString::number(seconds * 1e6, 'g', 4) + QStringLiteral(" µs");
    if (a < 1.0)
        return QString::number(seconds * 1e3, 'g', 4) + QStringLiteral(" ms");
    return QString::number(seconds, 'g', 4) + QStringLiteral(" s");
}
QString voltageLabel(double uv)
{
    const double a = std::abs(uv);
    if (a >= 1e6)
        return QString::number(uv / 1e6, 'g', 4) + QStringLiteral(" V");
    if (a >= 1e3)
        return QString::number(uv / 1e3, 'g', 4) + QStringLiteral(" mV");
    return QString::number(uv, 'g', 4) + QStringLiteral(" µV");
}
QString deltaVoltageLabel(double uv)
{
    const double a = std::abs(uv);
    const QString prefix = uv > 0 ? QStringLiteral("+") : QString();
    if (a >= 1e6)
        return prefix + QString::number(uv / 1e6, 'g', 4) + QStringLiteral(" V");
    if (a >= 1e3)
        return prefix + QString::number(uv / 1e3, 'g', 4) + QStringLiteral(" mV");
    return prefix + QString::number(uv, 'g', 4) + QStringLiteral(" µV");
}
std::size_t nearestSample(const Capture &capture, double time)
{
    const auto &m = capture.metadata;
    if (m.divider == 0 || m.timerHz == 0 || !std::isfinite(time))
        return 0;
    const auto end = capture.raw.size() - 1;
    const double index = std::clamp(time * m.timerHz / m.divider + m.pretrigger,
                                    0.0, static_cast<double>(end));
    const auto left = static_cast<std::size_t>(index);
    const auto right = std::min(left + 1, end);
    return std::abs(sampleTime(m, right) - time) < std::abs(sampleTime(m, left) - time)
               ? right
               : left;
}
} // namespace

struct WaveformWidget::PlotArea
{
    QRectF rect;
};

WaveformWidget::WaveformWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(320, 220);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAutoFillBackground(false);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void WaveformWidget::setCapture(std::shared_ptr<const Capture> capture, ViewPolicy view)
{
    bool preserve = false;
    if (view == ViewPolicy::Preserve && capture_ && capture)
    {
        const auto &previous = capture_->metadata;
        const auto &next = capture->metadata;
        preserve = previous.count == next.count && previous.pretrigger == next.pretrigger &&
                   previous.timerHz == next.timerHz && previous.divider == next.divider &&
                   previous.extendedSettingsValid == next.extendedSettingsValid &&
                   previous.timebaseIndex == next.timebaseIndex &&
                   previous.coupling == next.coupling &&
                   previous.microvoltsPerDivision == next.microvoltsPerDivision &&
                   previous.calibrationKind == next.calibrationKind &&
                   previous.zeroCodeQ16 == next.zeroCodeQ16 && previous.gainQ16 == next.gainQ16 &&
                   previous.gainShift == next.gainShift;
    }
    capture_ = std::move(capture);
    if (preserve)
    {
        updateCursorDelta();
        update();
    }
    else
    {
        fit();
    }
}

void WaveformWidget::fit()
{
    if (!capture_ || capture_->raw.empty())
    {
        fullTimeStart_ = timeStart_ = 0.0;
        fullTimeSpan_ = timeSpan_ = 1.0;
        fullValueCenter_ = valueCenter_ = 0.0;
        fullValueSpan_ = valueSpan_ = 1.0;
        update();
        return;
    }
    const auto &raw = capture_->raw;
    physicalUnits_ = sampleMicrovolts(capture_->metadata, raw.front()).has_value();
    double t0 = sampleTime(capture_->metadata, 0), t1 = t0;
    double low = std::numeric_limits<double>::infinity(), high = -low;
    for (std::size_t i = 0; i < raw.size(); ++i)
    {
        const double t = sampleTime(capture_->metadata, i);
        if (std::isfinite(t))
        {
            t0 = std::min(t0, t);
            t1 = std::max(t1, t);
        }
        const double value =
            physicalUnits_ ? sampleMicrovolts(capture_->metadata, raw[i]).value_or(raw[i]) : raw[i];
        low = std::min(low, value);
        high = std::max(high, value);
    }
    const double sampleDt =
        raw.size() > 1
            ? std::abs(sampleTime(capture_->metadata, 1) - sampleTime(capture_->metadata, 0))
            : 0.0;
    fullTimeStart_ = t0 - (t1 == t0 ? std::max(sampleDt, 1e-6) * .5 : 0.0);
    fullTimeSpan_ = std::max(t1 - t0, std::max(sampleDt, 1e-6));
    if (!std::isfinite(low) || !std::isfinite(high))
        low = high = 0.0;
    double amplitude = high - low;
    const double pad = amplitude > 0.0 ? amplitude * .12 : std::max(std::abs(high) * .12, 1000.0);
    fullValueCenter_ = (low + high) * .5;
    fullValueSpan_ = std::max(amplitude + 2.0 * pad, 2000.0);
    timeStart_ = fullTimeStart_;
    timeSpan_ = fullTimeSpan_;
    valueCenter_ = fullValueCenter_;
    valueSpan_ = fullValueSpan_;
    cursorTimes_[0] = t0;
    cursorTimes_[1] = t0 + (t1 - t0) * .5;
    updateCursorDelta();
    update();
}

void WaveformWidget::setCursorsVisible(bool visible)
{
    if (cursorsVisible_ == visible)
        return;
    cursorsVisible_ = visible;
    emit cursorsVisibleChanged(visible);
    updateCursorDelta();
    update();
}
WaveformWidget::PlotArea WaveformWidget::plotArea() const
{
    return {QRectF(74, 24, std::max(1, width() - 94), std::max(1, height() - 72))};
}
double WaveformWidget::xForTime(double t, const PlotArea &a) const
{
    return a.rect.left() + (t - timeStart_) / timeSpan_ * a.rect.width();
}
double WaveformWidget::timeForX(double x, const PlotArea &a) const
{
    return timeStart_ + (x - a.rect.left()) / a.rect.width() * timeSpan_;
}
double WaveformWidget::yForValue(double uv, const PlotArea &a) const
{
    return a.rect.bottom() - (uv - (valueCenter_ - valueSpan_ * .5)) / valueSpan_ * a.rect.height();
}

void WaveformWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), background);
    const auto a = plotArea();
    const QRectF r = a.rect;
    p.setPen(QPen(QColor(72, 91, 114), 1));
    p.drawRoundedRect(r, 4, 4);
    const double xstep = niceStep(timeSpan_, 8), ystep = niceStep(valueSpan_, 8);
    QFont axis = font();
    axis.setPointSizeF(std::max(8.0, font().pointSizeF() - 1));
    p.setFont(axis);
    p.setPen(QPen(grid, 1, Qt::DotLine));
    const double firstT = std::ceil(timeStart_ / xstep) * xstep;
    for (double t = firstT; t < timeStart_ + timeSpan_; t += xstep)
    {
        const double x = xForTime(t, a);
        p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
        p.setPen(muted);
        p.drawText(QRectF(x - 40, r.bottom() + 7, 80, 18), Qt::AlignHCenter | Qt::AlignTop,
                   timeLabel(t));
        p.setPen(QPen(grid, 1, Qt::DotLine));
    }
    const double lowV = valueCenter_ - valueSpan_ * .5, firstV = std::ceil(lowV / ystep) * ystep;
    for (double v = firstV; v < lowV + valueSpan_; v += ystep)
    {
        const double y = yForValue(v, a);
        p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
        p.setPen(muted);
        p.drawText(QRectF(2, y - 9, 65, 18), Qt::AlignRight | Qt::AlignVCenter,
                   physicalUnits_ ? voltageLabel(v) : QStringLiteral("%1 ADC").arg(v, 0, 'g', 4));
        p.setPen(QPen(grid, 1, Qt::DotLine));
    }
    if (!capture_ || capture_->raw.empty())
    {
        p.setPen(QColor(187, 202, 216));
        QFont title = font();
        title.setPointSize(12);
        title.setWeight(QFont::DemiBold);
        p.setFont(title);
        p.drawText(r, Qt::AlignCenter, QStringLiteral("No waveform captured"));
        p.setPen(muted);
        p.setFont(axis);
        p.drawText(QRectF(r.left(), r.center().y() + 25, r.width(), 22), Qt::AlignHCenter,
                   QStringLiteral("Connect the scope and acquire a trace"));
        return;
    }
    const auto &raw = capture_->raw;
    const auto &metadata = capture_->metadata;
    QPolygonF trace;
    double verticalTravel = 0.0;
    if (metadata.timerHz != 0 && metadata.divider != 0 &&
        sampleTime(metadata, 0) <= timeStart_ + timeSpan_ &&
        sampleTime(metadata, raw.size() - 1) >= timeStart_)
    {
        const auto edgeIndex = [&](double t) {
            return std::clamp(t * metadata.timerHz / metadata.divider + metadata.pretrigger,
                              0.0, static_cast<double>(raw.size() - 1));
        };
        // Keep the samples on both sides of the viewport so boundary segments
        // are still drawn up to the clip rectangle.
        const auto first = static_cast<std::size_t>(std::floor(edgeIndex(timeStart_)));
        const auto last = static_cast<std::size_t>(std::ceil(edgeIndex(timeStart_ + timeSpan_)));
        trace.reserve(static_cast<qsizetype>(last - first + 1));
        for (std::size_t i = first; i <= last; ++i)
        {
            const double value = physicalUnits_ ? sampleMicrovolts(metadata, raw[i]).value_or(raw[i])
                                                : raw[i];
            const double y = yForValue(value, a);
            if (!trace.isEmpty())
                verticalTravel += std::abs(y - trace.back().y());
            trace.append(QPointF(xForTime(sampleTime(metadata, i), a), y));
        }
    }
    p.save();
    p.setClipRect(r.adjusted(1, 1, -1, -1));
    if (timeStart_ <= 0 && timeStart_ + timeSpan_ >= 0)
    {
        const double x = xForTime(0, a);
        p.setPen(QPen(triggerColor, 1, Qt::DashLine));
        p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
    }
    // Dense traces repeatedly cross the same pixels. Use the raster engine's
    // thin polyline path for those traces, keeping all visible raw samples.
    const bool dense = verticalTravel > 24.0 * r.height();
    if (trace.size() > 1)
    {
        p.setRenderHint(QPainter::Antialiasing, !dense);
        p.setPen(QPen(cyan, 0));
        p.drawPolyline(trace);
        p.setRenderHint(QPainter::Antialiasing, true);
    }
    if (cursorsVisible_)
    {
        for (int i = 0; i < 2; ++i)
        {
            const double x = xForTime(cursorTimes_[i], a);
            p.setPen(QPen(amber, 1.4, Qt::DashLine));
            p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
            p.setPen(amber);
            QPolygonF flag{QPointF(x, r.top()), QPointF(x + (i ? -1 : 1) * 10, r.top()),
                           QPointF(x + (i ? -1 : 1) * 6, r.top() + 10)};
            p.setBrush(amber);
            p.drawPolygon(flag);
            p.setFont(axis);
            p.drawText(QRectF(x + (i ? -22 : 8), r.top() + 2, 18, 16), Qt::AlignCenter,
                       i ? QStringLiteral("B") : QStringLiteral("A"));
        }
    }
    p.restore();
    if (timeStart_ <= 0 && timeStart_ + timeSpan_ >= 0)
    {
        const double x = xForTime(0, a);
        p.setPen(triggerColor);
        const QString marker =
            (capture_->metadata.flags & instrument_code::capture_flags::EdgeTriggered) != 0
                ? QStringLiteral("T")
                : QStringLiteral("0");
        p.drawText(QRectF(x - 15, r.top() - 20, 30, 18), Qt::AlignCenter, marker);
    }
    p.setPen(muted);
    p.drawText(QRectF(r.left(), height() - 25, r.width(), 18), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("TIME GRID  %1").arg(timeLabel(xstep)));
    if (cursorsVisible_)
    {
        p.setPen(amber);
        p.drawText(
            QRectF(r.right() - 220, height() - 25, 215, 18), Qt::AlignRight | Qt::AlignVCenter,
            QStringLiteral("Δt  %1").arg(timeLabel(std::abs(cursorTimes_[1] - cursorTimes_[0]))));
    }
}

void WaveformWidget::mousePressEvent(QMouseEvent *e)
{
    setFocus();
    const auto a = plotArea();
    lastMouse_ = e->pos();
    if (cursorsVisible_ && e->button() == Qt::LeftButton)
    {
        const double d0 = std::abs(e->position().x() - xForTime(cursorTimes_[0], a)),
                     d1 = std::abs(e->position().x() - xForTime(cursorTimes_[1], a));
        if (std::min(d0, d1) < 14)
        {
            draggingCursor_ = d0 <= d1 ? 0 : 1;
            return;
        }
    }
    if (e->button() == Qt::LeftButton || e->button() == Qt::MiddleButton)
        panning_ = true;
}
void WaveformWidget::mouseMoveEvent(QMouseEvent *e)
{
    const auto a = plotArea();
    if (draggingCursor_ >= 0)
    {
        moveCursor(draggingCursor_, timeForX(e->position().x(), a));
        return;
    }
    if (panning_)
    {
        const QPoint delta = e->pos() - lastMouse_;
        timeStart_ -= delta.x() / a.rect.width() * timeSpan_;
        valueCenter_ += delta.y() / a.rect.height() * valueSpan_;
        lastMouse_ = e->pos();
        const double timeLimit = fullTimeSpan_ * 1000.0;
        timeStart_ = std::clamp(timeStart_, fullTimeStart_ - timeLimit,
                                fullTimeStart_ + fullTimeSpan_ + timeLimit - timeSpan_);
        const double valueLimit = fullValueSpan_ * 1000.0;
        valueCenter_ =
            std::clamp(valueCenter_, fullValueCenter_ - valueLimit, fullValueCenter_ + valueLimit);
        update();
    }
}
void WaveformWidget::mouseReleaseEvent(QMouseEvent *)
{
    draggingCursor_ = -1;
    panning_ = false;
}
void WaveformWidget::wheelEvent(QWheelEvent *e)
{
    const auto a = plotArea();
    const double factor = std::pow(.82, e->angleDelta().y() / 120.0);
    const double anchor = timeForX(e->position().x(), a);
    const double ratio = (anchor - timeStart_) / timeSpan_;
    const double newSpan = std::clamp(
        timeSpan_ * factor, std::max(fullTimeSpan_ / 10000.0, minimumSpan), fullTimeSpan_ * 1000.0);
    timeStart_ = anchor - ratio * newSpan;
    timeSpan_ = newSpan;
    const double yRatio = (a.rect.bottom() - e->position().y()) / a.rect.height();
    const double anchorV = valueCenter_ + (yRatio - .5) * valueSpan_;
    valueSpan_ = std::clamp(valueSpan_ * factor, std::max(fullValueSpan_ / 10000.0, 1.0),
                            fullValueSpan_ * 1000.0);
    valueCenter_ = anchorV - (yRatio - .5) * valueSpan_;
    const double timeLimit = fullTimeSpan_ * 1000.0;
    timeStart_ = std::clamp(timeStart_, fullTimeStart_ - timeLimit,
                            fullTimeStart_ + fullTimeSpan_ + timeLimit - timeSpan_);
    const double valueLimit = fullValueSpan_ * 1000.0;
    valueCenter_ =
        std::clamp(valueCenter_, fullValueCenter_ - valueLimit, fullValueCenter_ + valueLimit);
    update();
    e->accept();
}
void WaveformWidget::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_F)
    {
        fit();
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_C)
    {
        setCursorsVisible(!cursorsVisible_);
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_Left || e->key() == Qt::Key_Right)
    {
        const int which = (e->modifiers() & Qt::ShiftModifier) ? 1 : 0;
        const double direction = e->key() == Qt::Key_Right ? 1.0 : -1.0;
        moveCursor(which, cursorTimes_[which] + direction * timeSpan_ * .01);
        e->accept();
        return;
    }
    QWidget::keyPressEvent(e);
}
void WaveformWidget::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    update();
}
void WaveformWidget::moveCursor(int cursor, double time)
{
    const double lo = fullTimeStart_, hi = fullTimeStart_ + fullTimeSpan_;
    cursorTimes_[cursor] = std::clamp(time, lo, hi);
    updateCursorDelta();
    update();
}
void WaveformWidget::updateCursorDelta()
{
    if (!cursorsVisible_)
    {
        emit cursorDeltaChanged(QStringLiteral("Enable cursors to measure Δt and ΔV"));
        return;
    }
    QString valueA = QStringLiteral("—"), valueB = QStringLiteral("—"), delta = QStringLiteral("—");
    if (capture_ && !capture_->raw.empty())
    {
        const auto a = capture_->raw[nearestSample(*capture_, cursorTimes_[0])];
        const auto b = capture_->raw[nearestSample(*capture_, cursorTimes_[1])];
        const auto ua = sampleMicrovolts(capture_->metadata, a),
                   ub = sampleMicrovolts(capture_->metadata, b);
        valueA = ua ? voltageLabel(static_cast<double>(*ua)) : QStringLiteral("%1 ADC").arg(a);
        valueB = ub ? voltageLabel(static_cast<double>(*ub)) : QStringLiteral("%1 ADC").arg(b);
        if (ua && ub)
            delta = deltaVoltageLabel(static_cast<double>(*ub - *ua));
        else
            delta =
                QStringLiteral("%1 ADC codes")
                    .arg(static_cast<int>(b) - static_cast<int>(a));
    }
    emit cursorDeltaChanged(QStringLiteral("Δt %1 · ΔV %2\nA %3 · B %4")
                                .arg(timeLabel(std::abs(cursorTimes_[1] - cursorTimes_[0])))
                                .arg(delta)
                                .arg(valueA)
                                .arg(valueB));
}

} // namespace dso
