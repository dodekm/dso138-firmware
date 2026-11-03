#include "window.hpp"

#include <QApplication>
#include <QCommandLineParser>
#include <QFont>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QStyleFactory>

namespace
{

QIcon studioIcon()
{
    QIcon icon;
    for (const int size : {16, 24, 32, 48, 64, 128})
    {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.scale(size / 64.0, size / 64.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#172438"));
        painter.drawRoundedRect(QRectF(1, 1, 62, 62), 13, 13);
        QPainterPath trace;
        trace.moveTo(10, 38);
        trace.lineTo(21, 38);
        trace.lineTo(21, 23);
        trace.lineTo(36, 23);
        trace.lineTo(36, 38);
        trace.lineTo(53, 38);
        painter.setPen(QPen(QColor("#50d7e8"), 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(trace);
        painter.end();
        icon.addPixmap(pixmap);
    }
    return icon;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("DSO138 Studio"));
    QApplication::setApplicationVersion(QStringLiteral(DSO138_STUDIO_VERSION));
    QApplication::setOrganizationName(QStringLiteral("DSO138 Firmware Project"));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QApplication::setFont(QFont(QStringLiteral("Segoe UI"), 10));
    QApplication::setWindowIcon(studioIcon());

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Control, view, measure and export DSO138 oscilloscope captures."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("capture"),
                                 QStringLiteral("Optional .dso138 capture file"));
    parser.process(app);

    dso::MainWindow window;
    window.show();
    const auto arguments = parser.positionalArguments();
    if (!arguments.isEmpty())
    {
        window.openCaptureFile(arguments.front());
    }
    return app.exec();
}
