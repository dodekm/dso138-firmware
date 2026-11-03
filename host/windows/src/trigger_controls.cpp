#include "trigger_controls.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

namespace dso
{
namespace
{
constexpr quint8 TriggerWriteMask = 0x1cU;
constexpr quint8 HoldCapability = 0x20U;
constexpr quint8 SettingsWritableNow = 0x04U;
constexpr quint8 HoldAvailableNow = 0x10U;
}

TriggerControls::TriggerControls(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    hold_ = new QPushButton(QStringLiteral("Put scope in HOLD"), this);
    hold_->setObjectName(QStringLiteral("instrumentHold"));
    hold_->setToolTip(QStringLiteral("Pause local Auto or Normal RUN. Use the front panel to resume."));
    layout->addWidget(hold_);
    connect(hold_, &QPushButton::clicked, this, &TriggerControls::holdRequested);

    expand_ = new QPushButton(QStringLiteral("Trigger details"), this);
    expand_->setCheckable(true);
    expand_->setObjectName(QStringLiteral("instrumentTriggerDetails"));
    layout->addWidget(expand_);
    details_ = new QWidget(this);
    auto *form = new QFormLayout(details_);
    form->setContentsMargins(0, 0, 0, 0);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    edge_ = new QComboBox(details_);
    edge_->setAccessibleName(QStringLiteral("Trigger edge"));
    edge_->addItems({QStringLiteral("Rising"), QStringLiteral("Falling")});
    form->addRow(QStringLiteral("Edge"), edge_);
    level_ = new QSpinBox(details_);
    level_->setRange(0, 4095);
    level_->setSuffix(QStringLiteral(" ADC"));
    level_->setAccessibleName(QStringLiteral("Trigger level in ADC codes"));
    form->addRow(QStringLiteral("Level"), level_);
    position_ = new QDoubleSpinBox(details_);
    position_->setDecimals(1);
    position_->setRange(0.0, 100.0);
    position_->setSingleStep(2.5);
    position_->setSuffix(QStringLiteral("%"));
    position_->setAccessibleName(QStringLiteral("Trigger position as percent pretrigger"));
    form->addRow(QStringLiteral("Position"), position_);
    apply_ = new QPushButton(QStringLiteral("Apply trigger"), details_);
    apply_->setObjectName(QStringLiteral("applyInstrumentTrigger"));
    form->addRow(apply_);
    layout->addWidget(details_);
    details_->hide();
    connect(expand_, &QPushButton::toggled, details_, &QWidget::setVisible);
    connect(edge_, &QComboBox::currentIndexChanged, this, [this] { refreshActions(); });
    connect(level_, &QSpinBox::valueChanged, this, [this] { refreshActions(); });
    connect(position_, &QDoubleSpinBox::valueChanged, this, [this] { refreshActions(); });
    connect(apply_, &QPushButton::clicked, this, [this] {
        const int edge = edge_->currentIndex(), level = level_->value(), position = positionPermille();
        showAppliedTrigger();
        emit triggerRequested(edge, level, position);
    });
    refreshActions();
}

void TriggerControls::setInstrumentSettings(const protocol::Settings &settings,
                                             protocol::SettingsAvailability availability)
{
    const bool changed = availability != availability_ || settings.triggerEdge != applied_.triggerEdge ||
                         settings.triggerLevel != applied_.triggerLevel ||
                         settings.triggerPositionPermille != applied_.triggerPositionPermille;
    applied_ = settings;
    availability_ = availability;
    if (changed)
        showAppliedTrigger();
    refreshActions();
}

void TriggerControls::setInteractive(bool interactive)
{
    interactive_ = interactive;
    refreshActions();
}

int TriggerControls::positionPermille() const
{
    return qRound(position_->value() * 10.0);
}

void TriggerControls::showAppliedTrigger()
{
    const QSignalBlocker edgeGuard(edge_), levelGuard(level_), positionGuard(position_);
    edge_->setCurrentIndex(applied_.triggerEdge);
    level_->setValue(applied_.triggerLevel);
    position_->setValue(applied_.triggerPositionPermille / 10.0);
    refreshActions();
}

void TriggerControls::refreshActions()
{
    const bool present = availability_ == protocol::SettingsAvailability::Supported ||
                         availability_ == protocol::SettingsAvailability::Stale;
    const bool fresh = availability_ == protocol::SettingsAvailability::Supported;
    const bool canHold = present && (applied_.writableMask & HoldCapability) != 0;
    hold_->setVisible(canHold);
    hold_->setEnabled(fresh && interactive_ && (applied_.flags & HoldAvailableNow) != 0);
    const bool canEdit = present && (applied_.writableMask & TriggerWriteMask) == TriggerWriteMask;
    expand_->setVisible(canEdit);
    details_->setVisible(canEdit && expand_->isChecked());
    const bool writable = fresh && interactive_ && (applied_.flags & SettingsWritableNow) != 0;
    details_->setEnabled(writable);
    apply_->setEnabled(writable && (edge_->currentIndex() != applied_.triggerEdge ||
                                    level_->value() != applied_.triggerLevel ||
                                    positionPermille() != applied_.triggerPositionPermille));
}

} // namespace dso
