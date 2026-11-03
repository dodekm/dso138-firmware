#pragma once

#include <QWidget>
#include <memory>

namespace dso
{
struct Capture;
}

class QMouseEvent;
class QWheelEvent;

namespace dso
{

class WaveformWidget final : public QWidget
{
    Q_OBJECT
  public:
    enum class ViewPolicy { Fit, Preserve };

    explicit WaveformWidget(QWidget *parent = nullptr);
    void setCapture(std::shared_ptr<const Capture> capture, ViewPolicy view = ViewPolicy::Fit);
    void fit();
    void setCursorsVisible(bool visible);
    [[nodiscard]] bool cursorsVisible() const noexcept
    {
        return cursorsVisible_;
    }

  signals:
    void cursorDeltaChanged(const QString &delta);
    void cursorsVisibleChanged(bool visible);

  protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void resizeEvent(QResizeEvent *) override;

  private:
    struct PlotArea;
    [[nodiscard]] PlotArea plotArea() const;
    [[nodiscard]] double xForTime(double time, const PlotArea &area) const;
    [[nodiscard]] double timeForX(double x, const PlotArea &area) const;
    [[nodiscard]] double yForValue(double microvolts, const PlotArea &area) const;
    void updateCursorDelta();
    void moveCursor(int cursor, double time);

    std::shared_ptr<const Capture> capture_;
    double timeStart_ = 0.0, timeSpan_ = 1.0;
    double valueCenter_ = 0.0, valueSpan_ = 1.0;
    double fullTimeStart_ = 0.0, fullTimeSpan_ = 1.0;
    double fullValueCenter_ = 0.0, fullValueSpan_ = 1.0;
    bool cursorsVisible_ = false;
    bool physicalUnits_ = true;
    double cursorTimes_[2] = {0.0, 0.0};
    int draggingCursor_ = -1;
    bool panning_ = false;
    QPoint lastMouse_;
};

} // namespace dso
