#pragma once

#include "protocol.hpp"

#include <QWidget>

class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QSpinBox;

namespace dso
{

class TriggerControls final : public QWidget
{
    Q_OBJECT

  public:
    explicit TriggerControls(QWidget *parent = nullptr);
    void setInstrumentSettings(const protocol::Settings &settings,
                               protocol::SettingsAvailability availability);
    void setInteractive(bool interactive);

  signals:
    void holdRequested();
    void triggerRequested(int edge, int level, int positionPermille);

  private:
    void refreshActions();
    void showAppliedTrigger();
    int positionPermille() const;

    protocol::Settings applied_{};
    protocol::SettingsAvailability availability_{protocol::SettingsAvailability::Unknown};
    bool interactive_{};
    QWidget *details_{};
    QPushButton *hold_{};
    QPushButton *expand_{};
    QPushButton *apply_{};
    QComboBox *edge_{};
    QSpinBox *level_{};
    QDoubleSpinBox *position_{};
};

} // namespace dso
