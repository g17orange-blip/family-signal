#include "Logging.h"
#include "MainWindow.h"

#include <QApplication>
#include <QFile>
#include <QIcon>
#include <QStringList>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("signal"));
    QCoreApplication::setApplicationName(QStringLiteral("signal"));
    Logging::install();   // before anything touches GStreamer
    app.setWindowIcon(QIcon(QStringLiteral(":/signal.png")));

    // Embedded stylesheet — turns the default light theme into a dark one
    // closer in feel to Telegram's dark mode.
    QFile qss(QStringLiteral(":/style.qss"));
    if (qss.open(QIODevice::ReadOnly)) {
        app.setStyleSheet(QString::fromUtf8(qss.readAll()));
    }

    const bool demoMode = app.arguments().contains(QStringLiteral("--demo"));

    MainWindow w;
    if (demoMode) {
        w.startDemo();
    } else {
        // First launch shows the setup wizard; cancelling it exits cleanly.
        if (!w.ensureConfigured()) return 0;
        w.start();
    }
    w.show();
    return app.exec();
}
