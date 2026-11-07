#include "window.hpp"
#include "capture_io.hpp"
#include "demo_capture.hpp"

#include <QComboBox>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <utility>

namespace dso
{

void MainWindow::showDemo()
{
    addRecord(demoCapture(), QStringLiteral("Built-in demonstration waveform"), true);
    report(QStringLiteral("Demo waveform loaded."));
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

} // namespace dso
