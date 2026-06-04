#include "Logging.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QStandardPaths>
#include <QTextStream>

#include <cstdio>

namespace {

QFile  g_file;
QMutex g_mutex;

constexpr qint64 kMaxLogBytes = 5 * 1024 * 1024;

void messageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg) {
    const char *level = "DBG";
    switch (type) {
        case QtInfoMsg:     level = "INF"; break;
        case QtWarningMsg:  level = "WRN"; break;
        case QtCriticalMsg: level = "ERR"; break;
        case QtFatalMsg:    level = "FTL"; break;
        default: break;
    }
    const QString line = QStringLiteral("%1 %2 %3")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
             QLatin1String(level), msg);

    {
        QMutexLocker lock(&g_mutex);
        if (g_file.isOpen()) {
            QTextStream(&g_file) << line << '\n';
            g_file.flush();
        }
    }
    fprintf(stderr, "%s\n", qPrintable(line));
    if (type == QtFatalMsg) abort();
}

} // namespace

namespace Logging {

QString directory() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .filePath(QStringLiteral("logs"));
}

void install() {
    const QString dir = directory();
    QDir().mkpath(dir);

    // Rotate: keep exactly one previous run's log if it grew large.
    const QString path = QDir(dir).filePath(QStringLiteral("signal.log"));
    if (QFile::exists(path) && QFile(path).size() > kMaxLogBytes) {
        QFile::remove(path + QStringLiteral(".old"));
        QFile::rename(path, path + QStringLiteral(".old"));
    }
    g_file.setFileName(path);
    g_file.open(QIODevice::Append | QIODevice::Text);
    qInstallMessageHandler(messageHandler);

    // GStreamer warnings/errors to their own file — must be set before
    // gst_init. Respect an explicit GST_DEBUG from the environment so a
    // deeper trace can still be requested by hand.
    if (qEnvironmentVariableIsEmpty("GST_DEBUG"))
        qputenv("GST_DEBUG", "2");   // errors + warnings
    if (qEnvironmentVariableIsEmpty("GST_DEBUG_FILE"))
        qputenv("GST_DEBUG_FILE",
                QDir(dir).filePath(QStringLiteral("gstreamer.log")).toUtf8());
    qputenv("GST_DEBUG_NO_COLOR", "1");

    qInfo() << "[log] session started, version" <<
#ifdef SIGNAL_VERSION
        SIGNAL_VERSION;
#else
        "dev";
#endif
}

} // namespace Logging
