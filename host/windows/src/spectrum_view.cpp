#include "spectrum_view.hpp"

#include "capture.hpp"
#include "spectrum.hpp"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QStringList>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <utility>

namespace dso
{
namespace
{

constexpr QColor background{17, 27, 44};
constexpr QColor grid{49, 67, 88};
constexpr QColor muted{151, 171, 190};
constexpr QColor cyan{80, 215, 232};
constexpr QColor amber{255, 183, 77};
constexpr QColor invalidColor{230, 135, 118};

double niceStep(double range, int target)
{
    if (!(range > 0.0) || !std::isfinite(range))
    {
        return 1.0;
    }
    const double raw = range / static_cast<double>(std::max(1, target));
    const double power = std::pow(10.0, std::floor(std::log10(raw)));
    const double fraction = raw / power;
    const double factor = fraction <= 1.0 ? 1.0 : fraction <= 2.0 ? 2.0 : fraction <= 5.0 ? 5.0 : 10.0;
    return factor * power;
}

QString frequencyLabel(double frequency)
{
    const double magnitude = std::abs(frequency);
    if (magnitude >= 1.0e6)
    {
        return QStringLiteral("%1 MHz").arg(frequency / 1.0e6, 0, 'g', 4);
    }
    if (magnitude >= 1.0e3)
    {
        return QStringLiteral("%1 kHz").arg(frequency / 1.0e3, 0, 'g', 4);
    }
    return QStringLiteral("%1 Hz").arg(frequency, 0, 'g', 4);
}

QString amplitudeLabel(double amplitude, bool calibrated, bool includePeakSuffix = true)
{
    const double magnitude = std::abs(amplitude);
    const QString suffix = includePeakSuffix ? QStringLiteral(" pk") : QString();
    if (!calibrated)
    {
        return QStringLiteral("%1 codes%2")
            .arg(amplitude, 0, 'g', 4)
            .arg(suffix);
    }
    if (magnitude >= 1.0e6)
    {
        return QStringLiteral("%1 V%2")
            .arg(amplitude / 1.0e6, 0, 'g', 4)
            .arg(suffix);
    }
    if (magnitude >= 1.0e3)
    {
        return QStringLiteral("%1 mV%2")
            .arg(amplitude / 1.0e3, 0, 'g', 4)
            .arg(suffix);
    }
    return QStringLiteral("%1 µV%2")
        .arg(amplitude, 0, 'g', 4)
        .arg(suffix);
}

QString windowName(SpectrumWindow window)
{
    switch (window)
    {
    case SpectrumWindow::Rectangular:
        return QStringLiteral("Rectangular");
    case SpectrumWindow::Hann:
        return QStringLiteral("Hann");
    case SpectrumWindow::Blackman:
        return QStringLiteral("Blackman");
    }
    return QStringLiteral("Hann");
}

} // namespace

class SpectrumPlotWidget final : public QWidget
{
  public:
    explicit SpectrumPlotWidget(QWidget *parent = nullptr) : QWidget(parent)
    {
        setObjectName(QStringLiteral("spectrumPlot"));
        setAccessibleName(QStringLiteral("Single-sided frequency spectrum"));
        setMinimumSize(280, 180);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setAutoFillBackground(false);
    }

    void setSpectrum(SpectrumResult spectrum)
    {
        spectrum_ = std::move(spectrum);
        emptyMessage_.clear();
        update();
    }

    void setEmptyMessage(QString message)
    {
        spectrum_ = {};
        emptyMessage_ = std::move(message);
        update();
    }

  protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.fillRect(rect(), background);

        const QRectF outer = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        painter.setPen(QPen(QColor(72, 91, 114), 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(outer, 4.0, 4.0);

        const double leftMargin = width() < 380 ? 58.0 : 74.0;
        const double bottomMargin = height() < 250 ? 36.0 : 48.0;
        const QRectF plot = outer.adjusted(leftMargin, 24.0, -14.0, -bottomMargin);
        if (plot.width() < 60.0 || plot.height() < 50.0)
        {
            drawEmpty(painter, outer, QStringLiteral("Expand the spectrum panel to view the plot."));
            return;
        }

        if (spectrum_.status == SpectrumStatus::InvalidInput ||
            spectrum_.amplitudeCodesPeak.empty() || spectrum_.frequencyHz.empty())
        {
            drawEmpty(painter, plot, emptyMessage_.isEmpty()
                                          ? QStringLiteral("Spectrum unavailable for this capture.")
                                          : emptyMessage_);
            return;
        }

        const bool calibrated = !spectrum_.amplitudeMicrovoltsPeak.empty();
        const auto &amplitudes = calibrated ? spectrum_.amplitudeMicrovoltsPeak
                                            : spectrum_.amplitudeCodesPeak;
        const double nyquist = spectrum_.sampleRateHz * 0.5;
        double top = 0.0;
        for (const double amplitude : amplitudes)
        {
            if (std::isfinite(amplitude))
            {
                top = std::max(top, amplitude);
            }
        }
        if (!(nyquist > 0.0) || !(top > 0.0) || !std::isfinite(nyquist))
        {
            drawEmpty(painter, plot, QStringLiteral("No spectral energy to display."));
            return;
        }
        top *= 1.08;
        const double xStep = niceStep(nyquist, 6);
        const double yStep = niceStep(top, 5);
        QFont axis = font();
        axis.setPointSizeF(std::max(8.0, font().pointSizeF() - 1.0));
        painter.setFont(axis);
        painter.setPen(QPen(grid, 1.0, Qt::DotLine));

        for (double frequency = 0.0; frequency <= nyquist + xStep * 0.01; frequency += xStep)
        {
            const double x = plot.left() + frequency / nyquist * plot.width();
            painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
            painter.setPen(muted);
            constexpr double labelWidth = 88.0;
            const double labelLeft = std::clamp(x - labelWidth / 2.0, 0.0, width() - labelWidth);
            painter.drawText(QRectF(labelLeft, plot.bottom() + 5.0, labelWidth, 17.0),
                             Qt::AlignHCenter | Qt::AlignTop, frequencyLabel(frequency));
            painter.setPen(QPen(grid, 1.0, Qt::DotLine));
        }
        for (double amplitude = 0.0; amplitude <= top + yStep * 0.01; amplitude += yStep)
        {
            const double y = plot.bottom() - amplitude / top * plot.height();
            painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
            painter.setPen(muted);
            painter.drawText(QRectF(2.0, y - 9.0, leftMargin - 8.0, 18.0),
                             Qt::AlignRight | Qt::AlignVCenter,
                             amplitudeLabel(amplitude, calibrated, false));
            painter.setPen(QPen(grid, 1.0, Qt::DotLine));
        }

        painter.setPen(QColor(187, 202, 216));
        painter.drawText(QRectF(plot.left(), 5.0, plot.width(), 16.0),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         calibrated ? QStringLiteral("FFT · calibrated peak amplitude")
                                    : QStringLiteral("FFT · ADC codes peak"));
        painter.drawText(QRectF(plot.left(), height() - 19.0, plot.width(), 16.0),
                         Qt::AlignCenter, QStringLiteral("Frequency"));

        QPolygonF trace;
        const std::size_t count = std::min(spectrum_.frequencyHz.size(), amplitudes.size());
        trace.reserve(static_cast<qsizetype>(count));
        for (std::size_t bin = 0U; bin < count; ++bin)
        {
            const double frequency = spectrum_.frequencyHz[bin];
            const double amplitude = amplitudes[bin];
            if (!std::isfinite(frequency) || !std::isfinite(amplitude))
            {
                continue;
            }
            trace.append(QPointF(plot.left() + frequency / nyquist * plot.width(),
                                 plot.bottom() - std::clamp(amplitude / top, 0.0, 1.0) * plot.height()));
        }
        painter.save();
        painter.setClipRect(plot.adjusted(-1.0, -1.0, 1.0, 1.0));
        painter.setPen(QPen(cyan, 0));
        painter.drawPolyline(trace);

        const std::size_t markerCount = std::min<std::size_t>(3U, spectrum_.dominantPeaks.size());
        for (std::size_t i = 0U; i < markerCount; ++i)
        {
            const auto &peak = spectrum_.dominantPeaks[i];
            const double amplitude = calibrated ? peak.amplitudeMicrovoltsPeak.value_or(0.0)
                                                : peak.amplitudeCodesPeak;
            if (!(peak.frequencyHz >= 0.0 && peak.frequencyHz <= nyquist) || amplitude <= 0.0)
            {
                continue;
            }
            const QPointF point(plot.left() + peak.frequencyHz / nyquist * plot.width(),
                                plot.bottom() - std::clamp(amplitude / top, 0.0, 1.0) * plot.height());
            painter.setPen(QPen(background, 1.5));
            painter.setBrush(amber);
            painter.drawEllipse(point, 3.4, 3.4);
            painter.setPen(amber);
            const double labelX = point.x() + 24.0 > plot.right() ? point.x() - 18.0 : point.x() + 5.0;
            painter.drawText(QRectF(labelX, std::max(plot.top(), point.y() - 18.0), 16.0, 16.0),
                             Qt::AlignLeft | Qt::AlignVCenter,
                             QStringLiteral("%1").arg(i + 1U));
        }
        painter.restore();

        if (spectrum_.status == SpectrumStatus::Clipped)
        {
            const QRectF warning = outer.adjusted(8.0, 8.0, -8.0, -8.0);
            painter.setPen(invalidColor);
            painter.setBrush(QColor(44, 34, 42, 230));
            painter.drawRoundedRect(QRectF(warning.left(), warning.top(), warning.width(), 22.0), 3.0, 3.0);
            painter.drawText(QRectF(warning.left() + 6.0, warning.top() + 2.0, warning.width() - 12.0, 18.0),
                             Qt::AlignLeft | Qt::AlignVCenter,
                             width() < 420
                                 ? QStringLiteral("CLIPPED · amplitudes may be unreliable")
                                 : QStringLiteral("ADC clipping detected · amplitudes may be unreliable"));
        }
    }

  private:
    static void drawEmpty(QPainter &painter, const QRectF &area, const QString &message)
    {
        QFont title = painter.font();
        title.setPointSizeF(std::max(9.0, title.pointSizeF()));
        title.setWeight(QFont::DemiBold);
        painter.setFont(title);
        painter.setPen(QColor(187, 202, 216));
        painter.drawText(area.adjusted(10.0, 10.0, -10.0, -10.0),
                         Qt::AlignCenter | Qt::TextWordWrap, message);
    }

    SpectrumResult spectrum_;
    QString emptyMessage_;
};

SpectrumView::SpectrumView(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("spectrumView"));
    setMinimumSize(320, 260);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(7);

    auto *selectors = new QWidget(this);
    auto *selectorLayout = new QHBoxLayout(selectors);
    selectorLayout->setContentsMargins(0, 0, 0, 0);
    selectorLayout->setSpacing(8);
    auto *windowLabel = new QLabel(QStringLiteral("Window"), selectors);
    windowSelector_ = new QComboBox(selectors);
    windowSelector_->setObjectName(QStringLiteral("spectrumWindowSelector"));
    windowSelector_->setAccessibleName(QStringLiteral("FFT window"));
    windowSelector_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    windowSelector_->setMinimumContentsLength(9);
    windowLabel->setBuddy(windowSelector_);
    windowLabel->setAccessibleName(QStringLiteral("Window selector label"));
    for (const auto window : {SpectrumWindow::Rectangular, SpectrumWindow::Hann, SpectrumWindow::Blackman})
    {
        windowSelector_->addItem(windowName(window), static_cast<int>(window));
    }
    windowSelector_->setCurrentIndex(1);
    auto *dcLabel = new QLabel(QStringLiteral("Offset"), selectors);
    dcSelector_ = new QComboBox(selectors);
    dcSelector_->setObjectName(QStringLiteral("spectrumDcSelector"));
    dcSelector_->setAccessibleName(QStringLiteral("DC component policy"));
    dcLabel->setBuddy(dcSelector_);
    dcSelector_->addItem(QStringLiteral("Remove mean"), static_cast<int>(SpectrumDcPolicy::Remove));
    dcSelector_->addItem(QStringLiteral("Keep offset"), static_cast<int>(SpectrumDcPolicy::Preserve));
    dcSelector_->setCurrentIndex(0);
    selectorLayout->addWidget(windowLabel);
    selectorLayout->addWidget(windowSelector_, 1);
    selectorLayout->addWidget(dcLabel);
    selectorLayout->addWidget(dcSelector_, 1);
    layout->addWidget(selectors);

    summary_ = new QLabel(QStringLiteral("Select a capture to analyze."), this);
    summary_->setObjectName(QStringLiteral("spectrumSummary"));
    summary_->setWordWrap(true);
    summary_->setAccessibleName(QStringLiteral("Spectrum status and dominant peaks"));
    summary_->setAccessibleDescription(QStringLiteral("Capture quality, FFT settings and strongest frequency peaks."));
    layout->addWidget(summary_);

    plot_ = new SpectrumPlotWidget(this);
    layout->addWidget(plot_, 1);

    connect(windowSelector_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { recompute(); });
    connect(dcSelector_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { recompute(); });
}

void SpectrumView::setCapture(std::shared_ptr<const Capture> capture)
{
    if (capture_ == capture)
        return;
    capture_ = std::move(capture);
    recompute();
}

void SpectrumView::recompute()
{
    if (!capture_)
    {
        summary_->setText(QStringLiteral("Select a capture to analyze."));
        summary_->setStyleSheet(QString());
        plot_->setEmptyMessage(QStringLiteral("No capture selected. Acquire or open a capture to view its spectrum."));
        return;
    }

    const auto window = static_cast<SpectrumWindow>(windowSelector_->currentData().toInt());
    const auto dcPolicy = static_cast<SpectrumDcPolicy>(dcSelector_->currentData().toInt());
    auto result = analyzeSpectrum(*capture_, window, dcPolicy, 8U);
    if (result.status == SpectrumStatus::InvalidInput)
    {
        summary_->setText(QStringLiteral("Spectrum unavailable · invalid capture samples or timing."));
        summary_->setStyleSheet(QStringLiteral("color: #e68776;"));
        plot_->setEmptyMessage(QStringLiteral("The capture record failed validation, so no spectrum was calculated."));
        return;
    }

    const bool calibrated = result.calibrationAvailable;
    QStringList fields{
        QStringLiteral("%1 samples").arg(result.sampleCount),
        QStringLiteral("%1 sample rate").arg(frequencyLabel(result.sampleRateHz)),
        QStringLiteral("%1/bin").arg(frequencyLabel(result.binSpacingHz)),
        QStringLiteral("%1 window gain").arg(QString::number(result.coherentGain, 'f', 3)),
        result.dcRemoved ? QStringLiteral("mean removed") : QStringLiteral("offset retained"),
        calibrated ? QStringLiteral("calibrated units") : QStringLiteral("raw ADC codes")};
    if (result.status == SpectrumStatus::Clipped)
    {
        fields.prepend(QStringLiteral("CLIPPED · amplitudes may be unreliable"));
        summary_->setStyleSheet(QStringLiteral("color: #ffb74d;"));
    }
    else
    {
        summary_->setStyleSheet(QString());
    }
    if (!result.dcRemoved)
    {
        const double dcAmplitude = calibrated ? result.dcAmplitudeMicrovolts.value_or(0.0)
                                              : result.dcAmplitudeCodes;
        fields.push_back(QStringLiteral("DC component: %1")
                             .arg(amplitudeLabel(dcAmplitude, calibrated, false)));
    }
    if (!result.dominantPeaks.empty())
    {
        QStringList peaks;
        const std::size_t shown = std::min<std::size_t>(3U, result.dominantPeaks.size());
        for (std::size_t i = 0U; i < shown; ++i)
        {
            const auto &peak = result.dominantPeaks[i];
            const double amplitude = calibrated ? peak.amplitudeMicrovoltsPeak.value_or(0.0)
                                                : peak.amplitudeCodesPeak;
            peaks.push_back(QStringLiteral("%1  %2 at %3")
                                .arg(i + 1U)
                                .arg(amplitudeLabel(amplitude, calibrated))
                                .arg(frequencyLabel(peak.frequencyHz)));
        }
        fields.push_back(QStringLiteral("peaks: %1").arg(peaks.join(QStringLiteral(" · "))));
    }
    summary_->setText(fields.join(QStringLiteral("  ·  ")));
    plot_->setSpectrum(std::move(result));
}

} // namespace dso
