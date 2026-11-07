#include "window.hpp"
#include "window_format.hpp"
#include "trigger_controls.hpp"
#include "instrument_codes.hpp"
#include "serial_transport.hpp"
#include "dso_timebase_contract.hpp"

#include <QAction>
#include <QCloseEvent>
#include <QComboBox>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStyle>
#include <QTimer>
#include <algorithm>

namespace dso
{
namespace
{
constexpr int shutdownCancellationWaitMs{1800};
}

using window_format::classicTimebases;
using window_format::rateText;
using window_format::scaledVoltage;
using window_format::timebaseText;
using window_format::triggerModeText;

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), session_{makeSerialTransport()}
{
    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(QStringLiteral("DSO138 Studio"));
    setMinimumSize(820, 510);
    resize(1180, 760);
    qRegisterMetaType<dso::Capture>("dso::Capture");
    buildUi();
    connect(&session_, &Session::connectionChanged, this, &MainWindow::onConnectionChanged);
    connect(&session_, &Session::busyChanged, this, &MainWindow::onBusyChanged);
    connect(&session_, &Session::message, this, [this](const QString &m) {
        if (!m.isEmpty())
            report(m);
    });
    connect(&session_, &Session::sessionInfo, this, [this](const QString &m) {
        if (!m.isEmpty())
            report(m);
    });
    connect(&session_, &Session::progress, this, [this](int done, int total) {
        progress_->setRange(0, std::max(1, total));
        progress_->setValue(done);
        progress_->setFormat(QStringLiteral("%1 / %2").arg(done).arg(total));
    });
    connect(&session_, &Session::frameReady, this, &MainWindow::onFrameReady);
    connect(&session_, &Session::liveStateChanged, this, &MainWindow::onLiveStateChanged);
    connect(&session_, &Session::instrumentSettingsChanged, this,
        &MainWindow::onInstrumentSettingsChanged);
    refreshPorts();
    updateControls();
    showDemo();
}

void MainWindow::onBusyChanged(bool busy)
{
    if (!busy && !session_.connected() && connecting_)
    {
        connecting_ = false;
        linkState_->setText(QStringLiteral("OFFLINE"));
    }
    if (!busy && progress_->value() == 0)
        progress_->setFormat(QStringLiteral("Ready"));
    updateControls();
    if (closePending_ && !busy)
    {
        session_.disconnectPort();
        closePending_ = false;
        QTimer::singleShot(0, this, &QWidget::close);
    }
}

void MainWindow::onConnectionChanged(bool connected)
{
    connecting_ = false;
    updateControls();
    linkState_->setText(connected ? QStringLiteral("CONNECTED") : QStringLiteral("OFFLINE"));
    linkState_->setProperty("connected", connected);
    linkState_->style()->unpolish(linkState_);
    linkState_->style()->polish(linkState_);
}

void MainWindow::onLiveStateChanged(bool active, const QString &description)
{
    liveTiming_->hide();
    live_->setText(active ? QStringLiteral("Stop live") : QStringLiteral("Start live"));
    live_->setProperty("accent", active ? "danger" : "primary");
    live_->style()->unpolish(live_);
    live_->style()->polish(live_);
    if (active)
    {
        liveDisplay_.hasPreviousAcquisitionEpoch = false;
        liveDisplay_.receivedFrames = 0;
        freezeDisplay_ = false;
        const QSignalBlocker blocker(freezeAction_);
        freezeAction_->setChecked(false);
        report(QStringLiteral("LIVE · %1 mode · successive captures; HOLD is required. No continuous sample stream.")
                   .arg(description));
    }
    else if (!description.isEmpty())
        report(description);
    updateControls();
}

void MainWindow::onInstrumentSettingsChanged(
    protocol::Settings s, protocol::SettingsAvailability availability)
{
    instrumentSettingsState_.current = s;
    instrumentSettingsState_.availability = availability;
    populateSettingsControls(s, availability);
    triggerControls_->setInstrumentSettings(s, availability);
    updateControls();
    instrumentSettings_->setText(window_format::instrumentSettingsText(
        s, availability, session_.connected()));
}

void MainWindow::refreshPorts()
{
    if (session_.connected())
        return;
    const QString previous = ports_->currentData().toString();
    ports_->clear();
    const auto list = availableSerialPorts();
    if (list.isEmpty())
        ports_->addItem(QStringLiteral("No serial ports found"), QString());
    else
        ports_->addItem(QStringLiteral("Select a COM port…"), QString());
    for (const auto &p : list)
    {
        const QString description =
            p.description.isEmpty() ? QStringLiteral("Serial device") : p.description;
        ports_->addItem(QStringLiteral("%1  ·  %2").arg(p.portName, description), p.portName);
    }
    if (!previous.isEmpty())
    {
        const int i = ports_->findData(previous);
        if (i >= 0)
            ports_->setCurrentIndex(i);
    }
    updateControls();
}
void MainWindow::toggleConnection()
{
    if (session_.connected())
    {
        session_.disconnectPort();
        return;
    }
    const QString port = ports_->currentData().toString();
    if (port.isEmpty())
    {
        report(QStringLiteral("Select an available serial port first."));
        return;
    }
    connecting_ = true;
    updateControls();
    report(QStringLiteral("Connecting to %1…").arg(port));
    session_.connectPort(port);
    if (!session_.busy() && !session_.connected())
    {
        connecting_ = false;
        updateControls();
    }
}
void MainWindow::updateControls()
{
    const bool busy = session_.busy(), connected = session_.connected(), online = connected;
    ports_->setEnabled(!busy && !connected && !connecting_);
    connect_->setEnabled(!busy && !connecting_ &&
                         (connected || !ports_->currentData().toString().isEmpty()));
    connect_->setText(connected     ? QStringLiteral("Disconnect")
                      : connecting_ ? QStringLiteral("Connecting…")
                                    : QStringLiteral("Connect"));
    download_->setEnabled(online && !busy);
    single_->setEnabled(online && !busy);
    series_->setEnabled(online && !busy);
    live_->setEnabled(session_.liveCaptureActive() ||
                      (online && !busy && instrumentSettingsState_.availability == protocol::SettingsAvailability::Supported &&
                       (instrumentSettingsState_.current.flags &
                        protocol::settings_flags::LiveCaptureRequirements) ==
                           protocol::settings_flags::LiveCaptureRequirements &&
                       instrumentSettingsState_.current.uiState ==
                           instrument_code::code(instrument_code::UiState::Hold)));
    freezeAction_->setEnabled(session_.liveCaptureActive());
    cancel_->setEnabled(session_.cancellable() && !session_.liveCaptureActive());
    timeoutSeconds_->setEnabled(!busy);
    if (auto *refresh = findChild<QPushButton *>(QStringLiteral("refreshPorts")))
        refresh->setEnabled(!connected && !busy && !connecting_);
    openAction_->setEnabled(!busy);
    const bool stableLiveRecord = session_.liveCaptureActive() && freezeDisplay_;
    saveAction_->setEnabled(!history_.empty() && (!busy || stableLiveRecord));
    exportAction_->setEnabled(!history_.empty() && (!busy || stableLiveRecord));
    demoAction_->setEnabled(!busy);
    const bool settingsSupported = instrumentSettingsState_.availability == protocol::SettingsAvailability::Supported;
    const bool writesAdmissible = settingsSupported && instrumentSettingsState_.current.uiState ==
                                      instrument_code::code(instrument_code::UiState::Hold) &&
                                  (instrumentSettingsState_.current.flags &
                                   protocol::settings_flags::WriteAdmissibleNow) != 0;
    const bool knownTimebase = instrumentSettingsState_.current.catalogueId ==
                                   timebase_contract::ClassicCatalogueId &&
                               instrumentSettingsState_.current.catalogueCount != 0 &&
                               instrumentSettingsState_.current.timebaseIndex < classicTimebases.size();
    timebaseControl_->setEnabled(!busy && settingsSupported && writesAdmissible && knownTimebase &&
                                 (instrumentSettingsState_.current.writableMask &
                                  protocol::settings_writable::Timebase) != 0);
    triggerModeControl_->setEnabled(!busy && settingsSupported && writesAdmissible &&
                                    (instrumentSettingsState_.current.writableMask &
                                     protocol::settings_writable::TriggerMode) != 0);
    triggerControls_->setInteractive(online && !busy);
}

void MainWindow::populateSettingsControls(const protocol::Settings &settings,
                                          protocol::SettingsAvailability availability)
{
    const QSignalBlocker timebaseBlocker(timebaseControl_);
    const QSignalBlocker modeBlocker(triggerModeControl_);
    const bool supported = availability == protocol::SettingsAvailability::Supported ||
                           availability == protocol::SettingsAvailability::Stale;
    const bool catalogueChanged = instrumentSettingsState_.catalogueChanged(settings, availability);
    if (!catalogueChanged)
    {
        const int tb = timebaseControl_->findData(settings.timebaseIndex);
        if (tb >= 0 && timebaseControl_->currentIndex() != tb)
            timebaseControl_->setCurrentIndex(tb);
        const int mode = triggerModeControl_->findData(settings.triggerMode);
        if (mode >= 0 && triggerModeControl_->currentIndex() != mode)
            triggerModeControl_->setCurrentIndex(mode);
        return;
    }
    timebaseControl_->clear();
    triggerModeControl_->clear();
    instrumentSettingsState_.rememberCatalogue(settings, availability);
    if (supported)
    {
        if (settings.catalogueId == timebase_contract::ClassicCatalogueId)
        {
            const int count = std::min<int>(static_cast<int>(classicTimebases.size()),
                                            settings.catalogueCount);
            for (int i = 0; i < count; ++i)
                timebaseControl_->addItem(timebaseText(classicTimebases[static_cast<std::size_t>(i)]), i);
            if (settings.timebaseIndex < count)
                timebaseControl_->setCurrentIndex(settings.timebaseIndex);
            else
            {
                timebaseControl_->addItem(QStringLiteral("%1 · unsupported index")
                                              .arg(timebaseText(settings.timebaseMicrosecondsPerDivision)),
                                          settings.timebaseIndex);
                timebaseControl_->setCurrentIndex(timebaseControl_->count() - 1);
            }
        }
        else
        {
            timebaseControl_->addItem(QStringLiteral("%1 · catalogue %2 (read-only)")
                                          .arg(timebaseText(settings.timebaseMicrosecondsPerDivision))
                                          .arg(settings.catalogueId),
                                      settings.timebaseIndex);
            timebaseControl_->setCurrentIndex(0);
        }
        const auto normalMode = instrument_code::code(instrument_code::TriggerMode::Normal);
        const auto autoMode = instrument_code::code(instrument_code::TriggerMode::Auto);
        const auto singleMode = instrument_code::code(instrument_code::TriggerMode::Single);
        triggerModeControl_->addItem(triggerModeText(normalMode), static_cast<int>(normalMode));
        triggerModeControl_->addItem(triggerModeText(autoMode), static_cast<int>(autoMode));
        triggerModeControl_->addItem(triggerModeText(singleMode), static_cast<int>(singleMode));
        const int modeIndex = triggerModeControl_->findData(settings.triggerMode);
        triggerModeControl_->setCurrentIndex(modeIndex);
    }
    else
    {
        const QString placeholder = availability == protocol::SettingsAvailability::Legacy
                                        ? QStringLiteral("Legacy firmware")
                                        : QStringLiteral("Unavailable");
        timebaseControl_->addItem(placeholder);
        triggerModeControl_->addItem(placeholder);
        triggerModeControl_->setCurrentIndex(0);
    }
}
void MainWindow::beginOperation(const QString &text)
{
    report(text);
    progress_->setValue(0);
    progress_->setFormat(QStringLiteral("Working…"));
}
void MainWindow::report(QString text)
{
    status_->setToolTip(text);
    status_->setText(std::move(text));
}
void MainWindow::closeEvent(QCloseEvent *event)
{
    if (property("shutdownForced").toBool())
    {
        event->accept();
        return;
    }
    if (session_.busy())
    {
        closePending_ = true;
        session_.cancel();
        report(QStringLiteral("Cancelling active operation before closing…"));
        event->ignore();
        QTimer::singleShot(shutdownCancellationWaitMs, this, [this] {
            if (closePending_)
            {
                session_.disconnectPort();
                closePending_ = false;
                setProperty("shutdownForced", true);
                QMainWindow::close();
            }
        });
        return;
    }
    if (session_.connected())
        session_.disconnectPort();
    event->accept();
}

} // namespace dso
