#pragma once

#include <QWidget>
#include <memory>

class QComboBox;
class QLabel;

namespace dso
{

struct Capture;
class SpectrumPlotWidget;

class SpectrumView final : public QWidget
{
  public:
    explicit SpectrumView(QWidget *parent = nullptr);
    void setCapture(std::shared_ptr<const Capture> capture);

  private:
    void recompute();

    std::shared_ptr<const Capture> capture_;
    QComboBox *windowSelector_{};
    QComboBox *dcSelector_{};
    QLabel *summary_{};
    SpectrumPlotWidget *plot_{};
};

} // namespace dso
