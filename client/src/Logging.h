#pragma once

#include <QString>

// File logging for field debugging: everything qDebug/qInfo/qWarning prints
// goes to <AppData>/logs/signal.log (with one .old rotation), and GStreamer's
// warnings/errors go to gstreamer.log in the same folder. Users grab the
// folder via the settings dialog's "Открыть папку с логами" button.
namespace Logging {

// Install the handler and set up GStreamer env vars. Call FIRST in main(),
// before anything touches GStreamer.
void install();

// Directory the logs live in (created on install()).
QString directory();

} // namespace Logging
