#pragma once

#include "capture_model.hpp"
#include "protocol.hpp"
#include "session.hpp"

#include <QMainWindow>
#include <QString>
#include <QtGlobal>
#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

class QAction;
class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QCloseEvent;
class QEvent;
class QHBoxLayout;
class QVBoxLayout;
class QProgressBar;
class QTabWidget;

namespace dso
{

class WaveformWidget;
class SpectrumView;
class TriggerControls;

class MainWindow final : public QMainWindow
{
  public:
    explicit MainWindow(QWidget *parent = nullptr);
    void openCaptureFile(const QString &path);

  protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

  private:
    struct Record
    {
        std::shared_ptr<const Capture> capture;
        QString source;
    };
    struct PendingRecord
    {
        Capture capture;
        QString source;
        bool replaceDemo;
        bool preserveView;
    };
    // Keeps the latest device snapshot separate from the catalogue currently
    // installed in the controls, avoiding unnecessary combo-box rebuilds.
    struct InstrumentSettingsState final
    {
        static constexpr quint8 UnpopulatedCatalogueValue{0xffU};

        protocol::Settings current{};
        protocol::SettingsAvailability availability{protocol::SettingsAvailability::Unknown};
        protocol::SettingsAvailability populatedAvailability{protocol::SettingsAvailability::Unknown};
        quint8 populatedCatalogueId{UnpopulatedCatalogueValue};
        quint8 populatedCatalogueCount{UnpopulatedCatalogueValue};

        [[nodiscard]] bool catalogueChanged(const protocol::Settings &settings,
                                            protocol::SettingsAvailability nextAvailability) const noexcept
        {
            return populatedAvailability != nextAvailability ||
                   populatedCatalogueId != settings.catalogueId ||
                   populatedCatalogueCount != settings.catalogueCount;
        }

        void rememberCatalogue(const protocol::Settings &settings,
                               protocol::SettingsAvailability nextAvailability) noexcept
        {
            populatedAvailability = nextAvailability;
            populatedCatalogueId = settings.catalogueId;
            populatedCatalogueCount = settings.catalogueCount;
        }
    };
    // Per-live-run presentation counters used to preserve the view and report
    // the acquisition-epoch step for each received record.
    struct LiveDisplayState final
    {
        std::uint32_t previousAcquisitionEpoch{};
        int receivedFrames{};
        bool hasPreviousAcquisitionEpoch{};
    };
    // Non-owning handles for the signal-analysis readout created by the UI.
    struct AnalysisLabels final
    {
        QLabel *calibration{};
        QLabel *peakToPeak{};
        QLabel *minimum{};
        QLabel *maximum{};
        QLabel *mean{};
        QLabel *rms{};
        QLabel *acRms{};
        QLabel *frequency{};
        QLabel *period{};
        QLabel *duty{};
        QLabel *quality{};
        QLabel *cursorDelta{};
    };

    void buildUi();
    void applyStudioStyle();
    QAction *buildMenus();
    void buildAcquisitionControls(QVBoxLayout *root);
    void buildAnalysisPanels(QVBoxLayout *root, QAction *cursors);
    void buildAnalysisViews(QHBoxLayout *content, QAction *cursors);
    QWidget *buildInstrumentCard();
    QWidget *buildCaptureCard();
    QWidget *buildMeasurementCard();
    void buildStatusBar();
    void refreshPorts();
    void toggleConnection();
    void updateControls();
    void onBusyChanged(bool busy);
    void onConnectionChanged(bool connected);
    void onFrameReady(Capture frame);
    void onLiveStateChanged(bool active, const QString &description);
    void onInstrumentSettingsChanged(protocol::Settings settings,
                                    protocol::SettingsAvailability availability);
    void populateSettingsControls(const protocol::Settings &settings,
                                 protocol::SettingsAvailability availability);
    void showDemo();
    void showRecord(int index, bool preserveView = false);
    void addRecord(Capture capture, QString source, bool replace, bool selectLatest = true,
                   bool preserveView = false);
    void flushPendingHistory();
    void beginOperation(const QString &text);
    void configureSeries();
    void chooseOpen();
    void saveSelected(bool csv);
    void report(QString text);

    Session session_;
    // Widget and action pointers below are non-owning handles. Qt's parent tree
    // owns those objects and destroys them with the window.
    WaveformWidget *plot_{};
    QTabWidget *plots_{};
    SpectrumView *spectrum_{};
    TriggerControls *triggerControls_{};
    QComboBox *ports_{};
    QComboBox *records_{};
    QComboBox *timebaseControl_{};
    QComboBox *triggerModeControl_{};
    QPushButton *connect_{};
    QPushButton *download_{};
    QPushButton *single_{};
    QPushButton *series_{};
    QPushButton *live_{};
    QPushButton *cancel_{};
    QSpinBox *timeoutSeconds_{};
    QLabel *linkState_{};
    QLabel *source_{};
    QLabel *recordInfo_{};
    QLabel *instrumentSettings_{};
    InstrumentSettingsState instrumentSettingsState_{};
    AnalysisLabels analysisLabels_{};
    QLabel *status_{};
    QLabel *liveTiming_{};
    QProgressBar *progress_{};
    QAction *openAction_{};
    QAction *demoAction_{};
    QAction *saveAction_{};
    QAction *exportAction_{};
    QAction *freezeAction_{};
    LiveDisplayState liveDisplay_{};
    std::vector<Record> history_;
    std::deque<PendingRecord> pendingHistory_;
    bool freezeDisplay_{};
    bool connecting_{};
    bool replaceOnNextFrame_{true};
    bool closePending_{};
};

} // namespace dso
