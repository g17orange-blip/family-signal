#pragma once

#include <QObject>
#include <QTimer>

struct _GstElement;
typedef struct _GstElement GstElement;

// Incoming-call ringtone, loud enough to fetch someone from another room.
//
// Generated, not played from a file: a GStreamer audiotestsrc stepped
// through a two-tone cadence (tri-tri … pause … tri-tri) by a QTimer.
// No audio asset to ship, no extra Qt module (QSoundEffect would pull in
// Qt Multimedia just for this), and it exercises the exact audio output
// path a call uses anyway.
class Ringtone : public QObject {
    Q_OBJECT
public:
    explicit Ringtone(QObject *parent = nullptr);
    ~Ringtone() override;

    void start();   // begins the cadence; no-op if already ringing
    void stop();    // silences and tears the pipeline down

private:
    void applyStep();

    GstElement *m_pipeline = nullptr;
    GstElement *m_src      = nullptr;
    QTimer      m_timer;
    int         m_step = 0;
};
