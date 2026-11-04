#include "window.hpp"
#include "spectrum_view.hpp"
#include "trigger_controls.hpp"
#include "waveform.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QMenuBar>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSerialPortInfo>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <cmath>
#include <QStringList>

namespace dso
{
namespace
{
constexpr std::size_t maxHistory = 100;

QString scaledVoltage(double uv)
{
    const double a = std::abs(uv);
    if (a >= 1e6)
        return QString::number(uv / 1e6, 'f', 3) + QStringLiteral(" V");
    if (a >= 1e3)
        return QString::number(uv / 1e3, 'f', 3) + QStringLiteral(" mV");
    return QString::number(a < 0.05 ? 0.0 : uv, 'f', 1) + QStringLiteral(" µV");
}
QString rateText(double rate)
{
    if (rate >= 1e6)
        return QStringLiteral("%1 MS/s").arg(rate / 1e6, 0, 'g', 5);
    if (rate >= 1e3)
        return QStringLiteral("%1 kS/s").arg(rate / 1e3, 0, 'g', 5);
    return QStringLiteral("%1 S/s").arg(rate, 0, 'g', 5);
}
QString measurementValue(std::optional<double> microvolts, std::optional<double> codes)
{
    if (microvolts)
        return scaledVoltage(*microvolts);
    if (codes)
        return QStringLiteral("%1 ADC").arg(*codes, 0, 'f', 1);
    return QStringLiteral("—");
}
QString frequencyText(double frequency)
{
    if (frequency >= 1e6)
        return QStringLiteral("%1 MHz").arg(frequency / 1e6, 0, 'g', 5);
    if (frequency >= 1e3)
        return QStringLiteral("%1 kHz").arg(frequency / 1e3, 0, 'g', 5);
    return QStringLiteral("%1 Hz").arg(frequency, 0, 'g', 5);
}
QString periodText(double seconds)
{
    if (seconds >= 1)
        return QStringLiteral("%1 s").arg(seconds, 0, 'g', 5);
    if (seconds >= 1e-3)
        return QStringLiteral("%1 ms").arg(seconds * 1e3, 0, 'g', 5);
    return QStringLiteral("%1 µs").arg(seconds * 1e6, 0, 'g', 5);
}
QString periodicQuality(MeasurementFrequencyStatus status)
{
    switch (status)
    {
    case MeasurementFrequencyStatus::Valid: return {};
    case MeasurementFrequencyStatus::InvalidInput: return QStringLiteral("Invalid capture");
    case MeasurementFrequencyStatus::InvalidTiming: return QStringLiteral("Invalid sample timing");
    case MeasurementFrequencyStatus::Clipped: return QStringLiteral("ADC clipping · periodic readings unavailable");
    case MeasurementFrequencyStatus::TooFewCycles: return QStringLiteral("Too few complete cycles");
    case MeasurementFrequencyStatus::LowAmplitude: return QStringLiteral("Signal below periodic detection threshold");
    case MeasurementFrequencyStatus::TooFast: return QStringLiteral("Too few samples per period");
    case MeasurementFrequencyStatus::Unstable: return QStringLiteral("Unstable crossings · check signal or timebase");
    }
    return {};
}
QString timebaseText(quint32 microseconds)
{
    if (microseconds >= 1000000)
        return QStringLiteral("%1 s/div").arg(static_cast<double>(microseconds) / 1000000.0, 0, 'g', 4);
    if (microseconds >= 1000)
        return QStringLiteral("%1 ms/div").arg(static_cast<double>(microseconds) / 1000.0, 0, 'g', 4);
    return QStringLiteral("%1 µs/div").arg(microseconds);
}
constexpr std::array<quint32, 18> classicTimebases{
    10, 20, 50, 100, 200, 500, 1000, 2000, 5000,
    10000, 20000, 50000, 100000, 200000, 500000, 1000000, 2000000, 5000000};
QFrame *card(QWidget *parent = nullptr)
{
    auto *f = new QFrame(parent);
    f->setObjectName(QStringLiteral("card"));
    f->setFrameShape(QFrame::NoFrame);
    return f;
}
QLabel *caption(const QString &text, QWidget *parent = nullptr)
{
    auto *l = new QLabel(text, parent);
    l->setObjectName(QStringLiteral("caption"));
    return l;
}
QLabel *valueLabel(const QString &text, QWidget *parent = nullptr)
{
    auto *l = new QLabel(text, parent);
    l->setObjectName(QStringLiteral("measureValue"));
    return l;
}
} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), session_()
{
    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(QStringLiteral("DSO138 Studio"));
    setMinimumSize(820, 510);
    resize(1180, 760);
    qRegisterMetaType<dso::Capture>("dso::Capture");
    buildUi();
    connect(&session_, &Session::connectionChanged, this, [this](bool connected) {
        connecting_ = false;
        updateControls();
        linkState_->setText(connected ? QStringLiteral("CONNECTED") : QStringLiteral("OFFLINE"));
        linkState_->setProperty("connected", connected);
        linkState_->style()->unpolish(linkState_);
        linkState_->style()->polish(linkState_);
    });
    connect(&session_, &Session::busyChanged, this, [this](bool busy) {
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
    });
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
    connect(&session_, &Session::frameReady, this, [this](Capture frame) {
        const QString source = session_.liveCaptureActive()
                                   ? QStringLiteral("Live successive capture")
                               : session_.repeatedCaptureActive()
                                   ? QStringLiteral("Repeated Single")
                                   : QStringLiteral("Live capture");
        const auto metadata = frame.metadata;
        const bool preserveView = session_.liveCaptureActive() && liveFramesReceived_ > 0 &&
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
            ++liveFramesReceived_;
        const auto &m = metadata;
        if (session_.liveCaptureActive() && m.hostIntervalBeforeMs != 0xffffffffU)
        {
            const double updateRate = m.hostIntervalBeforeMs == 0
                                          ? 0.0
                                          : 1000.0 / static_cast<double>(m.hostIntervalBeforeMs);
            const std::uint32_t epochStep = haveDisplayEpoch_ ? m.acquisitionEpoch - previousDisplayEpoch_ : 0;
            liveTiming_->setText(QStringLiteral("%1 records/s · %2 ms · epoch +%3")
                                 .arg(updateRate, 0, 'g', 4)
                                 .arg(m.hostIntervalBeforeMs)
                                 .arg(haveDisplayEpoch_ ? QString::number(epochStep) : QStringLiteral("—")));
            liveTiming_->show();
        }
        previousDisplayEpoch_ = m.acquisitionEpoch;
        haveDisplayEpoch_ = true;
    });
    connect(&session_, &Session::liveStateChanged, this, [this](bool active, const QString &description) {
        liveTiming_->hide();
        live_->setText(active ? QStringLiteral("Stop live") : QStringLiteral("Start live"));
        live_->setProperty("accent", active ? "danger" : "primary");
        live_->style()->unpolish(live_);
        live_->style()->polish(live_);
        if (active)
        {
            haveDisplayEpoch_ = false;
            liveFramesReceived_ = 0;
            freezeDisplay_ = false;
            const QSignalBlocker blocker(freezeAction_);
            freezeAction_->setChecked(false);
            report(QStringLiteral("LIVE · %1 mode · successive captures; HOLD is required. No continuous sample stream.")
                       .arg(description));
        }
        else if (!description.isEmpty())
            report(description);
        updateControls();
    });
    connect(&session_, &Session::instrumentSettingsChanged, this,
            [this](protocol::Settings s, protocol::SettingsAvailability availability) {
                currentInstrumentSettings_ = s;
                currentSettingsAvailability_ = availability;
                populateSettingsControls(s, availability);
                triggerControls_->setInstrumentSettings(s, availability);
                updateControls();
                if (availability == protocol::SettingsAvailability::Unknown)
                {
                    instrumentSettings_->setText(session_.connected()
                                                     ? QStringLiteral("Waiting for firmware readback…")
                                                     : QStringLiteral("Offline · settings not available"));
                    return;
                }
                if (availability == protocol::SettingsAvailability::Legacy)
                {
                    instrumentSettings_->setText(QStringLiteral(
                        "Legacy firmware · capture download remains available; live settings readback is unsupported."));
                    return;
                }

                const QString state = s.uiState == 0   ? QStringLiteral("HOLD")
                                      : s.uiState == 1 ? QStringLiteral("Waiting for controls")
                                      : s.uiState == 2 ? QStringLiteral("Waiting for backend")
                                      : s.uiState == 3 ? QStringLiteral("Arm pending")
                                      : s.uiState == 4 ? QStringLiteral("Acquiring")
                                      : s.uiState == 5 ? QStringLiteral("Frame ready")
                                                       : QStringLiteral("Fault");
                const QString runHold = (s.flags & 1U) != 0 ? QStringLiteral("RUN intent")
                                                            : QStringLiteral("HOLD");
                const QString mode = s.triggerMode == 0   ? QStringLiteral("Normal")
                                     : s.triggerMode == 1 ? QStringLiteral("Auto")
                                                          : QStringLiteral("Single");
                const QString edge = s.triggerEdge == 0 ? QStringLiteral("rising")
                                                        : QStringLiteral("falling");
                const QString coupling = s.coupling == 1   ? QStringLiteral("DC")
                                         : s.coupling == 2 ? QStringLiteral("AC")
                                         : s.coupling == 3 ? QStringLiteral("GND")
                                                           : QStringLiteral("unknown");
                QStringList writable;
                const std::array<const char *, 6> names{"timebase", "trigger mode", "trigger edge",
                                                        "trigger level", "trigger position", "HOLD local RUN"};
                for (int bit = 0; bit < static_cast<int>(names.size()); ++bit)
                    if ((s.writableMask & (1U << bit)) != 0)
                        writable.push_back(QString::fromLatin1(names[static_cast<std::size_t>(bit)]));
                const QString writeSummary = writable.isEmpty()
                                                 ? QStringLiteral("none")
                                                 : writable.join(QStringLiteral(", "));
                const QString sampleRate = s.timerHz == 0
                                               ? QStringLiteral("unavailable")
                                               : rateText(static_cast<double>(s.timerHz) / s.divider);
                const QString stale = availability == protocol::SettingsAvailability::Stale
                                          ? QStringLiteral("STALE · last acknowledged readback\n")
                                          : QString();
                instrumentSettings_->setText(
                    stale + QStringLiteral("Run: %1 · state %2 · revision %3\nTimebase: %4 µs/div · sample rate %5\nTrigger: %6 · %7 · %8 ADC · position %9‰\nInput: %10 · %11/div · hardware switches read-only\nRemote writable: %12 · writes admissible: %13")
                               .arg(runHold, state)
                               .arg(s.revision)
                               .arg(s.timebaseMicrosecondsPerDivision)
                               .arg(sampleRate)
                               .arg(mode, edge)
                               .arg(s.triggerLevel)
                               .arg(s.triggerPositionPermille)
                               .arg(coupling)
                               .arg(scaledVoltage(s.microvoltsPerDivision))
                               .arg(writeSummary)
                               .arg((s.flags & 4U) != 0 ? QStringLiteral("yes") : QStringLiteral("no")));
            });
    refreshPorts();
    updateControls();
    showDemo();
}

void MainWindow::buildUi()
{
    setStyleSheet(QStringLiteral(R"QSS(
        QMainWindow, QWidget { background:#111b2c; color:#dce7f2; font-family:"Segoe UI"; font-size:10pt; }
        QMenuBar { background:#141f31; border-bottom:1px solid #293b52; padding:4px; }
        QMenuBar::item { padding:6px 10px; border-radius:4px; } QMenuBar::item:selected,QMenu { background:#1b2a40; }
        QMenu { border:1px solid #354a64; padding:5px; } QMenu::item { padding:6px 26px; } QMenu::item:selected { background:#2b4662; }
        QLabel { background:transparent; }
        QFrame#card { background:#172438; border:1px solid #293b52; border-radius:8px; }
        QFrame#toolbar { background:#141f31; border-bottom:1px solid #293b52; }
        QLabel#brand { font-size:16pt; font-weight:700; color:#f3f7fb; }
        QLabel#caption { color:#93a9bf; font-size:9pt; }
        QLabel#measureValue { color:#f3f7fb; font-size:12pt; font-weight:600; }
        QLabel[metricValue="true"] { color:#f3f7fb; font-weight:600; }
        QLabel#measurementQuality { color:#f4be7f; font-size:9pt; }
        QTabWidget#analysisTabs::pane,QTabWidget#sidebarTabs::pane { border:0; }
        QTabBar::tab { color:#aabbd0; background:#172438; border-bottom:2px solid #293b52; padding:6px 16px; }
        QTabBar::tab:selected { color:#eef7ff; background:#20364d; border-bottom-color:#50d7e8; }
        QTabBar::tab:hover { color:#eef7ff; background:#243b53; }
        QLabel#statusBadge { border:1px solid #475b73; border-radius:10px; padding:4px 9px; color:#b8c6d4; font-size:8pt; font-weight:700; }
        QLabel#statusBadge[connected="true"] { color:#9de4c5; border-color:#347b69; background:#173a38; }
        QComboBox,QSpinBox,QDoubleSpinBox { background:#101a29; border:1px solid #354a64; border-radius:5px; padding:6px 8px; min-height:22px; }
        QComboBox:focus,QSpinBox:focus,QDoubleSpinBox:focus { border-color:#50d7e8; }
        QComboBox:disabled,QSpinBox:disabled,QDoubleSpinBox:disabled { color:#8296ac; border-color:#293b52; }
        QPushButton { background:#22364e; border:1px solid #354e69; border-radius:5px; padding:7px 12px; font-weight:600; }
        QPushButton:hover { background:#2a4661; } QPushButton:pressed { background:#172a3e; }
        QPushButton:disabled { color:#63768b; background:#182538; border-color:#27384d; }
        QPushButton[accent="primary"] { background:#087f91; border-color:#169db0; color:white; } QPushButton[accent="primary"]:hover { background:#0a91a3; }
        QPushButton[accent="danger"] { color:#ffc3a5; border-color:#805044; }
        QPushButton[accent]:disabled { color:#63768b; background:#182538; border-color:#27384d; }
        QProgressBar { background:#101a29; border:0; border-radius:4px; text-align:center; color:#b9c9d9; height:8px; }
        QProgressBar::chunk { background:#50d7e8; border-radius:4px; }
        QStatusBar { background:#101a29; color:#91a5ba; border-top:1px solid #293b52; }
        QScrollBar:vertical { background:#111b2c; width:9px; } QScrollBar::handle:vertical { background:#354a64; border-radius:4px; }
    )QSS"));
    auto *file = menuBar()->addMenu(QStringLiteral("&File"));
    openAction_ = file->addAction(QStringLiteral("&Open capture…"));
    openAction_->setShortcut(QKeySequence::Open);
    connect(openAction_, &QAction::triggered, this, &MainWindow::chooseOpen);
    saveAction_ = file->addAction(QStringLiteral("&Save capture…"));
    saveAction_->setShortcut(QKeySequence::Save);
    connect(saveAction_, &QAction::triggered, this, [this] { saveSelected(false); });
    exportAction_ = file->addAction(QStringLiteral("Export &CSV…"));
    exportAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+E")));
    connect(exportAction_, &QAction::triggered, this, [this] { saveSelected(true); });
    file->addSeparator();
    demoAction_ = file->addAction(QStringLiteral("Load &demo waveform"));
    demoAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+D")));
    connect(demoAction_, &QAction::triggered, this, &MainWindow::showDemo);
    file->addSeparator();
    auto *exitAction = file->addAction(QStringLiteral("E&xit"));
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);
    auto *view = menuBar()->addMenu(QStringLiteral("&View"));
    auto *fitAction = view->addAction(QStringLiteral("Fit waveform"));
    fitAction->setShortcut(QKeySequence(QStringLiteral("F")));
    connect(fitAction, &QAction::triggered, this, [this] {
        if (plot_)
            plot_->fit();
    });
    auto *cursors = view->addAction(QStringLiteral("Show cursors"));
    cursors->setCheckable(true);
    freezeAction_ = view->addAction(QStringLiteral("Freeze live display"));
    freezeAction_->setCheckable(true);
    freezeAction_->setEnabled(false);
    connect(freezeAction_, &QAction::toggled, this, [this](bool frozen) {
        freezeDisplay_ = frozen;
        if (!frozen && session_.liveCaptureActive() && !history_.empty())
        {
            const QSignalBlocker blocker(records_);
            records_->setCurrentIndex(0);
            showRecord(0);
        }
        report(frozen ? QStringLiteral("Display frozen · successive live records continue in history")
                      : QStringLiteral("Display follows the newest live capture"));
        updateControls();
    });
    auto *central = new QWidget(this);
    setCentralWidget(central);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(14, 10, 14, 10);
    root->setSpacing(10);
    auto *top = new QFrame;
    top->setObjectName(QStringLiteral("toolbar"));
    auto *topLayout = new QHBoxLayout(top);
    topLayout->setContentsMargins(12, 8, 12, 8);
    topLayout->setSpacing(8);
    auto *brand = new QLabel(QStringLiteral("DSO138 Studio"));
    brand->setObjectName(QStringLiteral("brand"));
    topLayout->addWidget(brand);
    topLayout->addSpacing(12);
    linkState_ = new QLabel(QStringLiteral("OFFLINE"));
    linkState_->setObjectName(QStringLiteral("statusBadge"));
    topLayout->addWidget(linkState_);
    topLayout->addStretch();
    ports_ = new QComboBox;
    ports_->setObjectName(QStringLiteral("portSelector"));
    ports_->setMinimumWidth(160);
    ports_->setToolTip(QStringLiteral("Select the scope serial port"));
    topLayout->addWidget(ports_);
    auto *refresh = new QPushButton(QStringLiteral("↻"));
    refresh->setObjectName(QStringLiteral("refreshPorts"));
    refresh->setToolTip(QStringLiteral("Refresh serial ports"));
    refresh->setAccessibleName(QStringLiteral("Refresh ports"));
    refresh->setFixedWidth(38);
    topLayout->addWidget(refresh);
    connect(refresh, &QPushButton::clicked, this, &MainWindow::refreshPorts);
    connect(ports_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { updateControls(); });
    connect_ = new QPushButton(QStringLiteral("Connect"));
    connect_->setObjectName(QStringLiteral("connectButton"));
    topLayout->addWidget(connect_);
    connect(connect_, &QPushButton::clicked, this, &MainWindow::toggleConnection);
    root->addWidget(top);
    auto *actions = card();
    auto *actionLayout = new QHBoxLayout(actions);
    actionLayout->setContentsMargins(12, 8, 12, 8);
    actionLayout->setSpacing(8);
    auto *section = caption(QStringLiteral("ACQUIRE"));
    actionLayout->addWidget(section);
    actionLayout->addSpacing(7);
    download_ = new QPushButton(QStringLiteral("Download held"));
    download_->setObjectName(QStringLiteral("downloadButton"));
    actionLayout->addWidget(download_);
    connect(download_, &QPushButton::clicked, this, [this] {
        replaceOnNextFrame_ = true;
        beginOperation(QStringLiteral("Downloading held capture…"));
        session_.downloadHeld();
    });
    single_ = new QPushButton(QStringLiteral("Single"));
    single_->setObjectName(QStringLiteral("singleButton"));
    single_->setProperty("accent", "primary");
    actionLayout->addWidget(single_);
    connect(single_, &QPushButton::clicked, this, [this] {
        replaceOnNextFrame_ = true;
        beginOperation(QStringLiteral("Waiting for single capture…"));
        session_.captureSingle(timeoutSeconds_->value() * 1000);
    });
    series_ = new QPushButton(QStringLiteral("Repeat singles…"));
    series_->setObjectName(QStringLiteral("repeatButton"));
    series_->setToolTip(QStringLiteral("Request a bounded series of separate legacy Single captures"));
    actionLayout->addWidget(series_);
    connect(series_, &QPushButton::clicked, this, &MainWindow::configureSeries);
    live_ = new QPushButton(QStringLiteral("Start live"));
    live_->setObjectName(QStringLiteral("liveButton"));
    live_->setProperty("accent", "primary");
    live_->setToolTip(QStringLiteral("Repeated successive captures using the scope's selected Auto, Normal, or Single trigger mode"));
    connect(live_, &QPushButton::clicked, this, [this] {
        if (session_.liveCaptureActive())
        {
            session_.stopLive();
            return;
        }
        replaceOnNextFrame_ = true;
        session_.startLive(timeoutSeconds_->value() * 1000);
    });
    cancel_ = new QPushButton(QStringLiteral("Cancel"));
    cancel_->setObjectName(QStringLiteral("cancelButton"));
    cancel_->setProperty("accent", "danger");
    actionLayout->addWidget(cancel_);
    connect(cancel_, &QPushButton::clicked, &session_, &Session::cancel);
    progress_ = new QProgressBar;
    progress_->setObjectName(QStringLiteral("captureProgress"));
    progress_->setFixedWidth(100);
    progress_->setRange(0, 1);
    progress_->setValue(0);
    progress_->setFormat(QStringLiteral("Ready"));
    actionLayout->addWidget(progress_);
    actionLayout->addStretch();
    auto *timeoutLabel = caption(QStringLiteral("TIMEOUT"));
    timeoutSeconds_ = new QSpinBox;
    timeoutSeconds_->setObjectName(QStringLiteral("captureTimeout"));
    timeoutSeconds_->setRange(1, 600);
    timeoutSeconds_->setValue(3);
    timeoutSeconds_->setSuffix(QStringLiteral(" s"));
    timeoutSeconds_->setToolTip(QStringLiteral("Time limit for each Single or repeated capture"));
    timeoutLabel->setBuddy(timeoutSeconds_);
    timeoutSeconds_->setAccessibleName(QStringLiteral("Per-capture timeout"));
    actionLayout->addWidget(timeoutLabel);
    actionLayout->addWidget(timeoutSeconds_);
    root->addWidget(actions);
    auto *liveActions = card();
    auto *liveLayout = new QHBoxLayout(liveActions);
    liveLayout->setContentsMargins(10, 6, 10, 6);
    liveLayout->setSpacing(9);
    liveLayout->addWidget(live_);
    auto *guidance = caption(QStringLiteral(
        "Successive captures · selected trigger mode · HOLD required · no continuous sample stream"));
    guidance->setObjectName(QStringLiteral("holdGuidance"));
    liveLayout->addWidget(guidance, 1);
    root->addWidget(liveActions);
    auto *content = new QHBoxLayout;
    content->setSpacing(10);
    root->addLayout(content, 1);
    plot_ = new WaveformWidget;
    plot_->setObjectName(QStringLiteral("waveformPlot"));
    plot_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    spectrum_ = new SpectrumView;
    plots_ = new QTabWidget;
    plots_->setObjectName(QStringLiteral("analysisTabs"));
    plots_->setAccessibleName(QStringLiteral("Capture analysis views"));
    plots_->setDocumentMode(true);
    plots_->tabBar()->setDrawBase(false);
    plots_->addTab(plot_, QStringLiteral("&Waveform"));
    plots_->addTab(spectrum_, QStringLiteral("&Spectrum"));
    content->addWidget(plots_, 1);
    connect(plots_, &QTabWidget::currentChanged, this, [this] {
        if (plots_->currentWidget() != spectrum_ || !records_)
            return;
        const int selected = records_->currentIndex();
        if (selected >= 0 && static_cast<std::size_t>(selected) < history_.size())
            spectrum_->setCapture(history_[static_cast<std::size_t>(selected)].capture);
    });
    connect(plot_, &WaveformWidget::cursorDeltaChanged, this,
            [this](const QString &text) { cursorDelta_->setText(text); });
    connect(cursors, &QAction::toggled, plot_, &WaveformWidget::setCursorsVisible);
    connect(plot_, &WaveformWidget::cursorsVisibleChanged, cursors, &QAction::setChecked);
    auto *sidebarTabs = new QTabWidget;
    sidebarTabs->setObjectName(QStringLiteral("sidebarTabs"));
    sidebarTabs->setAccessibleName(QStringLiteral("Instrument and measurements panels"));
    sidebarTabs->setDocumentMode(true);
    sidebarTabs->tabBar()->setDrawBase(false);
    sidebarTabs->setMinimumWidth(245);
    sidebarTabs->setMaximumWidth(325);
    content->addWidget(sidebarTabs);
    const auto sidebarPage = [sidebarTabs](const QString &title, const QString &id) {
        auto *scroll = new QScrollArea;
        scroll->setObjectName(id);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto *page = new QWidget;
        auto *layout = new QVBoxLayout(page);
        layout->setContentsMargins(0, 4, 4, 0);
        layout->setSpacing(9);
        scroll->setWidget(page);
        sidebarTabs->addTab(scroll, title);
        return layout;
    };
    auto *sideLayout = sidebarPage(QStringLiteral("Instrument"), QStringLiteral("instrumentSidebarScroll"));
    auto *measurementLayout = sidebarPage(QStringLiteral("Measurements"), QStringLiteral("measurementSidebarScroll"));
    auto *instrumentCard = card();
    auto *ic = new QVBoxLayout(instrumentCard);
    ic->setContentsMargins(12, 10, 12, 10);
    ic->addWidget(caption(QStringLiteral("CURRENT INSTRUMENT SETTINGS")));
    instrumentSettings_ = caption(QStringLiteral("Offline · settings not available"));
    instrumentSettings_->setObjectName(QStringLiteral("currentInstrumentSettings"));
    instrumentSettings_->setWordWrap(true);
    ic->addWidget(instrumentSettings_);
    auto *timebaseRow = new QHBoxLayout;
    timebaseRow->addWidget(caption(QStringLiteral("Timebase")));
    timebaseRow->addStretch();
    timebaseControl_ = new QComboBox;
    timebaseControl_->setObjectName(QStringLiteral("instrumentTimebase"));
    timebaseControl_->setAccessibleName(QStringLiteral("Instrument timebase"));
    timebaseControl_->addItem(QStringLiteral("Unavailable"));
    timebaseRow->addWidget(timebaseControl_, 1);
    ic->addLayout(timebaseRow);
    connect(timebaseControl_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index < 0 || currentSettingsAvailability_ != protocol::SettingsAvailability::Supported)
            return;
        const int acknowledged = currentInstrumentSettings_.timebaseIndex;
        if (index == acknowledged)
            return;
        {
            const QSignalBlocker blocker(timebaseControl_);
            timebaseControl_->setCurrentIndex(acknowledged);
        }
        session_.setTimebase(index);
    });
    auto *modeRow = new QHBoxLayout;
    modeRow->addWidget(caption(QStringLiteral("Trigger mode")));
    modeRow->addStretch();
    triggerModeControl_ = new QComboBox;
    triggerModeControl_->setObjectName(QStringLiteral("instrumentTriggerMode"));
    triggerModeControl_->setAccessibleName(QStringLiteral("Instrument trigger mode"));
    triggerModeControl_->addItem(QStringLiteral("Unavailable"));
    triggerModeControl_->setCurrentIndex(0);
    modeRow->addWidget(triggerModeControl_, 1);
    ic->addLayout(modeRow);
    connect(triggerModeControl_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int index) {
                if (index < 0 || currentSettingsAvailability_ != protocol::SettingsAvailability::Supported)
                    return;
                const int mode = triggerModeControl_->itemData(index).toInt();
                if (mode == currentInstrumentSettings_.triggerMode)
                    return;
                {
                    const QSignalBlocker blocker(triggerModeControl_);
                    const int acknowledged = triggerModeControl_->findData(currentInstrumentSettings_.triggerMode);
                    triggerModeControl_->setCurrentIndex(acknowledged);
                }
                session_.setTriggerMode(mode);
            });
    triggerControls_ = new TriggerControls(instrumentCard);
    ic->addWidget(triggerControls_);
    connect(triggerControls_, &TriggerControls::triggerRequested, &session_, &Session::setTrigger);
    connect(triggerControls_, &TriggerControls::holdRequested, &session_, &Session::holdLocalRun);
    sideLayout->addWidget(instrumentCard);
    auto *recordCard = card();
    auto *rc = new QVBoxLayout(recordCard);
    rc->setContentsMargins(12, 10, 12, 10);
    rc->addWidget(caption(QStringLiteral("CAPTURE")));
    records_ = new QComboBox;
    records_->view()->installEventFilter(this);
    records_->setObjectName(QStringLiteral("captureSelector"));
    rc->addWidget(records_);
    connect(records_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (session_.liveCaptureActive() || !pendingHistory_.empty())
        {
            freezeDisplay_ = index > 0;
            const QSignalBlocker blocker(freezeAction_);
            freezeAction_->setChecked(freezeDisplay_);
        }
        showRecord(index);
    });
    source_ = caption(QStringLiteral("No capture selected"));
    source_->setObjectName(QStringLiteral("captureSource"));
    source_->setWordWrap(true);
    rc->addWidget(source_);
    recordInfo_ = caption(QStringLiteral("—"));
    recordInfo_->setObjectName(QStringLiteral("captureMetadata"));
    recordInfo_->setWordWrap(true);
    rc->addWidget(recordInfo_);
    sideLayout->addWidget(recordCard);
    auto *measureCard = card();
    auto *mc = new QVBoxLayout(measureCard);
    mc->setContentsMargins(12, 10, 12, 10);
    mc->setSpacing(5);
    mc->addWidget(caption(QStringLiteral("MEASUREMENTS")));
    calibration_ = caption(QStringLiteral("—"));
    calibration_->setAccessibleName(QStringLiteral("Capture calibration"));
    calibration_->setWordWrap(true);
    mc->addWidget(calibration_);
    auto addMetric = [&](const QString &label, const QString &initial, QLabel *&out,
                         const QString &id) {
        auto *row = new QHBoxLayout;
        row->addWidget(caption(label));
        row->addStretch();
        out = valueLabel(initial);
        out->setObjectName(id);
        out->setAccessibleName(label);
        out->setProperty("metricValue", true);
        row->addWidget(out);
        mc->addLayout(row);
    };
    addMetric(QStringLiteral("Peak to peak"), QStringLiteral("—"), peakToPeak_,
              QStringLiteral("measureVpp"));
    addMetric(QStringLiteral("Minimum"), QStringLiteral("—"), minimum_, QStringLiteral("measureMinimum"));
    addMetric(QStringLiteral("Maximum"), QStringLiteral("—"), maximum_, QStringLiteral("measureMaximum"));
    addMetric(QStringLiteral("Mean"), QStringLiteral("—"), mean_, QStringLiteral("measureMean"));
    addMetric(QStringLiteral("Total RMS"), QStringLiteral("—"), rms_, QStringLiteral("measureRms"));
    addMetric(QStringLiteral("AC RMS"), QStringLiteral("—"), acRms_, QStringLiteral("measureAcRms"));
    addMetric(QStringLiteral("Frequency"), QStringLiteral("—"), frequency_,
              QStringLiteral("measureFrequency"));
    addMetric(QStringLiteral("Period"), QStringLiteral("—"), period_, QStringLiteral("measurePeriod"));
    addMetric(QStringLiteral("Duty cycle"), QStringLiteral("—"), duty_, QStringLiteral("measureDuty"));
    measurementQuality_ = caption(QString());
    measurementQuality_->setObjectName(QStringLiteral("measurementQuality"));
    measurementQuality_->setWordWrap(true);
    mc->addWidget(measurementQuality_);
    auto *crow = new QVBoxLayout;
    crow->setSpacing(3);
    crow->addWidget(caption(QStringLiteral("CURSORS  A ↔ B")));
    auto *cursorHelp = caption(QStringLiteral("Drag A/B or use arrows. Shift selects B."));
    cursorHelp->setWordWrap(true);
    crow->addWidget(cursorHelp);
    cursorDelta_ = caption(QStringLiteral("Enable cursors to measure Δt and ΔV"));
    cursorDelta_->setObjectName(QStringLiteral("cursorDelta"));
    cursorDelta_->setWordWrap(true);
    cursorDelta_->setMinimumHeight(36);
    crow->addWidget(cursorDelta_);
    mc->addLayout(crow);
    sideLayout->addStretch();
    measurementLayout->addWidget(measureCard);
    measurementLayout->addStretch();
    status_ = new QLabel(QStringLiteral("Ready"));
    status_->setObjectName(QStringLiteral("statusMessage"));
    status_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    statusBar()->addWidget(status_, 1);
    liveTiming_ = new QLabel;
    liveTiming_->setObjectName(QStringLiteral("liveTiming"));
    liveTiming_->setToolTip(QStringLiteral(
        "Host record delivery rate and interval. The epoch step identifies device sequence changes; "
        "it does not count dropped USB records or missing ADC samples."));
    statusBar()->addPermanentWidget(liveTiming_);
    liveTiming_->hide();
}

void MainWindow::refreshPorts()
{
    if (session_.connected())
        return;
    const QString previous = ports_->currentData().toString();
    ports_->clear();
    const auto list = QSerialPortInfo::availablePorts();
    if (list.isEmpty())
        ports_->addItem(QStringLiteral("No serial ports found"), QString());
    else
        ports_->addItem(QStringLiteral("Select a COM port…"), QString());
    for (const auto &p : list)
    {
        const QString description =
            p.description().isEmpty() ? QStringLiteral("Serial device") : p.description();
        ports_->addItem(QStringLiteral("%1  ·  %2").arg(p.portName(), description), p.portName());
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
                      (online && !busy && currentSettingsAvailability_ == protocol::SettingsAvailability::Supported &&
                       (currentInstrumentSettings_.flags & 0x0eU) == 0x0eU &&
                       currentInstrumentSettings_.uiState == 0));
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
    const bool settingsSupported = currentSettingsAvailability_ == protocol::SettingsAvailability::Supported;
    const bool writesAdmissible = settingsSupported && currentInstrumentSettings_.uiState == 0 &&
                                  (currentInstrumentSettings_.flags & 0x04U) != 0;
    const bool knownTimebase = currentInstrumentSettings_.catalogueId == 1 &&
                               currentInstrumentSettings_.catalogueCount != 0 &&
                               currentInstrumentSettings_.timebaseIndex < classicTimebases.size();
    timebaseControl_->setEnabled(!busy && settingsSupported && writesAdmissible && knownTimebase &&
                                 (currentInstrumentSettings_.writableMask & 0x01U) != 0);
    triggerModeControl_->setEnabled(!busy && settingsSupported && writesAdmissible &&
                                    (currentInstrumentSettings_.writableMask & 0x02U) != 0);
    triggerControls_->setInteractive(online && !busy);
}

void MainWindow::populateSettingsControls(const protocol::Settings &settings,
                                          protocol::SettingsAvailability availability)
{
    const QSignalBlocker timebaseBlocker(timebaseControl_);
    const QSignalBlocker modeBlocker(triggerModeControl_);
    const bool supported = availability == protocol::SettingsAvailability::Supported ||
                           availability == protocol::SettingsAvailability::Stale;
    const bool catalogueChanged = populatedSettingsAvailability_ != availability ||
                                  populatedCatalogueId_ != settings.catalogueId ||
                                  populatedCatalogueCount_ != settings.catalogueCount;
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
    populatedSettingsAvailability_ = availability;
    populatedCatalogueId_ = settings.catalogueId;
    populatedCatalogueCount_ = settings.catalogueCount;
    if (supported)
    {
        if (settings.catalogueId == 1)
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
        triggerModeControl_->addItem(QStringLiteral("Normal"), 0);
        triggerModeControl_->addItem(QStringLiteral("Auto"), 1);
        triggerModeControl_->addItem(QStringLiteral("Single"), 2);
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
void MainWindow::showDemo()
{
    addRecord(demoCapture(), QStringLiteral("Built-in demonstration waveform"), true);
    report(QStringLiteral("Demo waveform loaded."));
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
    const QString triggerMode = m.triggerMode == 0   ? QStringLiteral("Normal")
                                : m.triggerMode == 1 ? QStringLiteral("Auto")
                                                     : QStringLiteral("Single");
    const QString triggerEdge = (m.flags & 8U) == 0  ? QStringLiteral("no edge")
                                : m.triggerEdge == 0 ? QStringLiteral("rising")
                                                     : QStringLiteral("falling");
    const QString frozenSettings = m.extendedSettingsValid != 0
                                       ? QStringLiteral("%1 · trigger level %2 ADC")
                                             .arg(m.timebaseCatalogueId == 1 && m.timebaseIndex < classicTimebases.size()
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
    const QString interval = m.hostIntervalBeforeMs == 0xffffffffU
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
    const QString cal = m.calibrationKind == 2   ? QStringLiteral("Measured calibration")
                        : m.calibrationKind == 1 ? QStringLiteral("Nominal calibration")
                                                 : QStringLiteral("Unavailable · raw ADC only");
    const QString coupling = m.coupling == 1   ? QStringLiteral("DC")
                             : m.coupling == 2 ? QStringLiteral("AC")
                             : m.coupling == 3 ? QStringLiteral("GND")
                                               : QStringLiteral("unknown");
    calibration_->setText(QStringLiteral("%1\n%2/div  ·  %3")
                              .arg(cal, scaledVoltage(m.microvoltsPerDivision), coupling));
    const auto stats = measure(*rec.capture);
    peakToPeak_->setText(measurementValue(stats.vppMicrovolts, stats.rawVpp));
    minimum_->setText(measurementValue(stats.minimumMicrovolts, stats.rawMinimum));
    maximum_->setText(measurementValue(stats.maximumMicrovolts, stats.rawMaximum));
    mean_->setText(measurementValue(stats.meanMicrovolts, stats.rawMean));
    rms_->setText(measurementValue(stats.totalRmsMicrovolts, stats.rawTotalRms));
    acRms_->setText(measurementValue(stats.acRmsMicrovolts, stats.rawAcRms));
    frequency_->setText(stats.frequencyHz ? frequencyText(*stats.frequencyHz) : QStringLiteral("—"));
    period_->setText(stats.periodSeconds ? periodText(*stats.periodSeconds) : QStringLiteral("—"));
    duty_->setText(stats.dutyCycleFraction
                       ? QStringLiteral("%1%").arg(100.0 * *stats.dutyCycleFraction, 0, 'f', 1)
                       : QStringLiteral("—"));
    const auto quality = periodicQuality(stats.frequencyStatus);
    measurementQuality_->setText(quality);
    measurementQuality_->setVisible(!quality.isEmpty());
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
void MainWindow::beginOperation(const QString &text)
{
    report(text);
    progress_->setValue(0);
    progress_->setFormat(QStringLiteral("Working…"));
}
void MainWindow::configureSeries()
{
    QDialog d(this);
    d.setWindowTitle(QStringLiteral("Repeat independent Single captures"));
    d.setStyleSheet(styleSheet());
    auto *form = new QFormLayout(&d);
    auto *note = caption(QStringLiteral(
        "Legacy-compatible mode: each item is a separate Single capture with acquisition and transfer gaps."));
    note->setWordWrap(true);
    form->addRow(note);
    auto *count = new QSpinBox(&d);
    count->setRange(1, 100);
    count->setValue(10);
    count->setObjectName(QStringLiteral("repeatCount"));
    auto *interval = new QSpinBox(&d);
    interval->setRange(0, 60000);
    interval->setSingleStep(100);
    interval->setValue(500);
    interval->setSuffix(QStringLiteral(" ms"));
    interval->setObjectName(QStringLiteral("repeatInterval"));
    form->addRow(QStringLiteral("Number of captures"), count);
    form->addRow(QStringLiteral("Interval"), interval);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted)
        return;
    replaceOnNextFrame_ = false;
    beginOperation(QStringLiteral("Starting repeated Single captures…"));
    session_.captureRepeated(count->value(), timeoutSeconds_->value() * 1000, interval->value());
}
void MainWindow::chooseOpen()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Open capture"), {},
        QStringLiteral("Versioned DSO138 captures (*.dso138);;All files (*)"));
    if (!path.isEmpty())
        openCaptureFile(path);
}
void MainWindow::openCaptureFile(const QString &path)
{
    QString error;
    auto capture = loadCapture(path, &error);
    if (!capture)
    {
        report(QStringLiteral("Could not open capture: %1").arg(error));
        return;
    }
    addRecord(std::move(*capture), QFileInfo(path).fileName(), false);
    report(QStringLiteral("Opened %1").arg(path));
}
void MainWindow::saveSelected(bool csv)
{
    const int i = records_->currentIndex();
    if (i < 0 || i >= static_cast<int>(history_.size()))
        return;
    const auto selectedCapture = history_[static_cast<std::size_t>(i)].capture;
    const QString filter =
        csv ? QStringLiteral("CSV files (*.csv)") : QStringLiteral("DSO138 capture (*.dso138)");
    QFileDialog dialog(this, csv ? QStringLiteral("Export capture as CSV")
                                : QStringLiteral("Save capture"), {}, filter);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setDefaultSuffix(csv ? QStringLiteral("csv") : QStringLiteral("dso138"));
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
        return;
    const QString path = dialog.selectedFiles().front();
    QString error;
    const auto &c = *selectedCapture;
    const bool ok = csv ? exportCsv(path, c, &error) : saveCapture(path, c, &error);
    report(ok ? QStringLiteral("Saved %1").arg(path)
              : QStringLiteral("Could not save capture: %1").arg(error));
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
        QTimer::singleShot(1800, this, [this] {
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
