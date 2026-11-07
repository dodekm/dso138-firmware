#include "trigger_controls.hpp"
#include "instrument_codes.hpp"
#include "dso_adc_contract.hpp"
#include "dso_settings_contract.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

namespace dso
{

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
    edge_->addItem(QStringLiteral("Rising"),
                   static_cast<int>(instrument_code::code(instrument_code::TriggerEdge::Rising)));
    edge_->addItem(QStringLiteral("Falling"),
                   static_cast<int>(instrument_code::code(instrument_code::TriggerEdge::Falling)));
    form->addRow(QStringLiteral("Edge"), edge_);
    level_ = new QSpinBox(details_);
    level_->setRange(0, adc_contract::MaximumCode);
    level_->setSuffix(QStringLiteral(" ADC"));
    level_->setAccessibleName(QStringLiteral("Trigger level in ADC codes"));
    form->addRow(QStringLiteral("Level"), level_);
    position_ = new QDoubleSpinBox(details_);
    position_->setDecimals(1);
    position_->setRange(0.0, settings_contract::TriggerPositionPermilleMaximum / 10.0);
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
        emit triggerRequested(edge_->currentData().toInt(), level_->value(), positionPermille());
    });
    refreshActions();
}

void TriggerControls::setInstrumentSettings(const protocol::Settings &settings,
                                             protocol::SettingsAvailability availability)
{
    const bool hadReadback = availability_ == protocol::SettingsAvailability::Supported ||
                             availability_ == protocol::SettingsAvailability::Stale;
    const bool hasReadback = availability == protocol::SettingsAvailability::Supported ||
                             availability == protocol::SettingsAvailability::Stale;
    const bool changed = hadReadback != hasReadback || settings.triggerEdge != applied_.triggerEdge ||
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
    if (interactive_ == interactive)
        return;
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
    edge_->setCurrentIndex(edge_->findData(static_cast<int>(applied_.triggerEdge)));
    level_->setValue(applied_.triggerLevel);
    position_->setValue(applied_.triggerPositionPermille / 10.0);
    refreshActions();
}

void TriggerControls::refreshActions()
{
    const bool present = availability_ == protocol::SettingsAvailability::Supported ||
                         availability_ == protocol::SettingsAvailability::Stale;
    const bool fresh = availability_ == protocol::SettingsAvailability::Supported;
    const bool canHold = present &&
                         (applied_.writableMask & protocol::settings_writable::LocalRunPause) != 0;
    hold_->setVisible(canHold);
    hold_->setEnabled(fresh && interactive_ &&
                      (applied_.flags & protocol::settings_flags::LocalRunPauseAdmissibleNow) != 0);
    const bool canEdit = present &&
                         (applied_.writableMask & protocol::settings_writable::TriggerFields) ==
                             protocol::settings_writable::TriggerFields;
    expand_->setVisible(canEdit);
    details_->setVisible(canEdit && expand_->isChecked());
    const bool writable = fresh && interactive_ &&
                          (applied_.flags & protocol::settings_flags::WriteAdmissibleNow) != 0;
    details_->setEnabled(writable);
    apply_->setEnabled(writable && (edge_->currentData().toInt() != applied_.triggerEdge ||
                                    level_->value() != applied_.triggerLevel ||
                                    positionPermille() != applied_.triggerPositionPermille));
}

} // namespace dso
