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
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QMenuBar>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
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

void MainWindow::buildUi()
{
    applyStudioStyle();
    QAction *cursors = buildMenus();
    auto *central = new QWidget(this);
    setCentralWidget(central);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(14, 10, 14, 10);
    root->setSpacing(10);
    buildAcquisitionControls(root);
    buildAnalysisPanels(root, cursors);
    buildStatusBar();
}

void MainWindow::applyStudioStyle()
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
}

QAction *MainWindow::buildMenus()
{
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
    return cursors;
}

void MainWindow::buildAcquisitionControls(QVBoxLayout *root)
{
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
}

void MainWindow::buildAnalysisPanels(QVBoxLayout *root, QAction *cursors)
{
    auto *content = new QHBoxLayout;
    content->setSpacing(10);
    root->addLayout(content, 1);
    buildAnalysisViews(content, cursors);

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
    auto *instrumentCard = buildInstrumentCard();
    sideLayout->addWidget(instrumentCard);
    auto *recordCard = buildCaptureCard();
    sideLayout->addWidget(recordCard);
    auto *measureCard = buildMeasurementCard();
    sideLayout->addStretch();
    measurementLayout->addWidget(measureCard);
    measurementLayout->addStretch();
}

void MainWindow::buildAnalysisViews(QHBoxLayout *content, QAction *cursors)
{
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
            [this](const QString &text) { analysisLabels_.cursorDelta->setText(text); });
    connect(cursors, &QAction::toggled, plot_, &WaveformWidget::setCursorsVisible);
    connect(plot_, &WaveformWidget::cursorsVisibleChanged, cursors, &QAction::setChecked);
}

QWidget *MainWindow::buildInstrumentCard()
{
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
        if (index < 0 || instrumentSettingsState_.availability != protocol::SettingsAvailability::Supported)
            return;
        const int acknowledged = instrumentSettingsState_.current.timebaseIndex;
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
                if (index < 0 || instrumentSettingsState_.availability != protocol::SettingsAvailability::Supported)
                    return;
                const int mode = triggerModeControl_->itemData(index).toInt();
                if (mode == instrumentSettingsState_.current.triggerMode)
                    return;
                {
                    const QSignalBlocker blocker(triggerModeControl_);
                    const int acknowledged = triggerModeControl_->findData(instrumentSettingsState_.current.triggerMode);
                    triggerModeControl_->setCurrentIndex(acknowledged);
                }
                session_.setTriggerMode(mode);
            });
    triggerControls_ = new TriggerControls(instrumentCard);
    ic->addWidget(triggerControls_);
    connect(triggerControls_, &TriggerControls::triggerRequested, &session_, &Session::setTrigger);
    connect(triggerControls_, &TriggerControls::holdRequested, &session_, &Session::holdLocalRun);
    return instrumentCard;
}

QWidget *MainWindow::buildCaptureCard()
{
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
    return recordCard;
}

QWidget *MainWindow::buildMeasurementCard()
{
    auto *measureCard = card();
    auto *mc = new QVBoxLayout(measureCard);
    mc->setContentsMargins(12, 10, 12, 10);
    mc->setSpacing(5);
    mc->addWidget(caption(QStringLiteral("MEASUREMENTS")));
    analysisLabels_.calibration = caption(QStringLiteral("—"));
    analysisLabels_.calibration->setAccessibleName(QStringLiteral("Capture calibration"));
    analysisLabels_.calibration->setWordWrap(true);
    mc->addWidget(analysisLabels_.calibration);
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
    addMetric(QStringLiteral("Peak to peak"), QStringLiteral("—"),
              analysisLabels_.peakToPeak,
              QStringLiteral("measureVpp"));
    addMetric(QStringLiteral("Minimum"), QStringLiteral("—"),
              analysisLabels_.minimum, QStringLiteral("measureMinimum"));
    addMetric(QStringLiteral("Maximum"), QStringLiteral("—"),
              analysisLabels_.maximum, QStringLiteral("measureMaximum"));
    addMetric(QStringLiteral("Mean"), QStringLiteral("—"),
              analysisLabels_.mean, QStringLiteral("measureMean"));
    addMetric(QStringLiteral("Total RMS"), QStringLiteral("—"),
              analysisLabels_.rms, QStringLiteral("measureRms"));
    addMetric(QStringLiteral("AC RMS"), QStringLiteral("—"),
              analysisLabels_.acRms, QStringLiteral("measureAcRms"));
    addMetric(QStringLiteral("Frequency"), QStringLiteral("—"),
              analysisLabels_.frequency,
              QStringLiteral("measureFrequency"));
    addMetric(QStringLiteral("Period"), QStringLiteral("—"),
              analysisLabels_.period, QStringLiteral("measurePeriod"));
    addMetric(QStringLiteral("Duty cycle"), QStringLiteral("—"),
              analysisLabels_.duty, QStringLiteral("measureDuty"));
    analysisLabels_.quality = caption(QString());
    analysisLabels_.quality->setObjectName(QStringLiteral("measurementQuality"));
    analysisLabels_.quality->setWordWrap(true);
    mc->addWidget(analysisLabels_.quality);
    auto *crow = new QVBoxLayout;
    crow->setSpacing(3);
    crow->addWidget(caption(QStringLiteral("CURSORS  A ↔ B")));
    auto *cursorHelp = caption(QStringLiteral("Drag A/B or use arrows. Shift selects B."));
    cursorHelp->setWordWrap(true);
    crow->addWidget(cursorHelp);
    analysisLabels_.cursorDelta = caption(
        QStringLiteral("Enable cursors to measure Δt and ΔV"));
    analysisLabels_.cursorDelta->setObjectName(QStringLiteral("cursorDelta"));
    analysisLabels_.cursorDelta->setWordWrap(true);
    analysisLabels_.cursorDelta->setMinimumHeight(36);
    crow->addWidget(analysisLabels_.cursorDelta);
    mc->addLayout(crow);
    return measureCard;
}

void MainWindow::buildStatusBar()
{
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
    liveTiming_->hide();}



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

} // namespace dso
