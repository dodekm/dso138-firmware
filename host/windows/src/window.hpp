#pragma once

#include "capture.hpp"
#include "session.hpp"

#include <QMainWindow>
#include <deque>
#include <memory>
#include <vector>

class QAction;
class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QCloseEvent;
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

    void buildUi();
    void refreshPorts();
    void toggleConnection();
    void updateControls();
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
    protocol::Settings currentInstrumentSettings_{};
    protocol::SettingsAvailability currentSettingsAvailability_{protocol::SettingsAvailability::Unknown};
    protocol::SettingsAvailability populatedSettingsAvailability_{protocol::SettingsAvailability::Unknown};
    quint8 populatedCatalogueId_{0xff};
    quint8 populatedCatalogueCount_{0xff};
    QLabel *calibration_{};
    QLabel *peakToPeak_{};
    QLabel *minimum_{};
    QLabel *maximum_{};
    QLabel *mean_{};
    QLabel *rms_{};
    QLabel *acRms_{};
    QLabel *frequency_{};
    QLabel *period_{};
    QLabel *duty_{};
    QLabel *measurementQuality_{};
    QLabel *cursorDelta_{};
    QLabel *status_{};
    QLabel *liveTiming_{};
    QProgressBar *progress_{};
    QAction *openAction_{};
    QAction *demoAction_{};
    QAction *saveAction_{};
    QAction *exportAction_{};
    QAction *freezeAction_{};
    std::vector<Record> history_;
    std::deque<PendingRecord> pendingHistory_;
    std::uint32_t previousDisplayEpoch_{};
    bool haveDisplayEpoch_{};
    int liveFramesReceived_{};
    bool freezeDisplay_{};
    bool connecting_{};
    bool replaceOnNextFrame_{true};
    bool closePending_{};
};

} // namespace dso
