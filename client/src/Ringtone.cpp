#include "Ringtone.h"

extern "C" {
#include <gst/gst.h>
}

namespace {

// One ring cycle: four alternating tones (≈ a classic "tring-tring"),
// then a pause. The table loops until stop().
struct Step { double freq; double volume; int ms; };
const Step kCadence[] = {
    {880.0, 0.8, 250},
    {660.0, 0.8, 250},
    {880.0, 0.8, 250},
    {660.0, 0.8, 250},
    {660.0, 0.0, 1600},   // silence between rings (freq irrelevant)
};
constexpr int kSteps = int(sizeof(kCadence) / sizeof(kCadence[0]));

} // namespace

Ringtone::Ringtone(QObject *parent) : QObject(parent) {
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &Ringtone::applyStep);
}

Ringtone::~Ringtone() {
    stop();
}

void Ringtone::start() {
    if (m_pipeline) return;   // already ringing
    // MainWindow::start() builds a WebRtcSession (and with it gst_init)
    // long before any call can ring; this is just belt-and-braces for
    // other call orders — repeated gst_init calls are no-ops.
    gst_init(nullptr, nullptr);
    GError *err = nullptr;
    m_pipeline = gst_parse_launch(
        "audiotestsrc name=ringsrc is-live=true wave=sine volume=0.0 ! "
        "audioconvert ! audioresample ! autoaudiosink", &err);
    if (!m_pipeline) {
        // No audio output is not worth failing the call UI over — the
        // incoming-call window still shows.
        qWarning("Ringtone: %s", err ? err->message : "pipeline failed");
        if (err) g_error_free(err);
        return;
    }
    if (err) g_error_free(err);   // non-fatal parse warnings
    m_src = gst_bin_get_by_name(GST_BIN(m_pipeline), "ringsrc");
    gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    m_step = 0;
    applyStep();
}

void Ringtone::stop() {
    m_timer.stop();
    if (m_src) {
        gst_object_unref(m_src);
        m_src = nullptr;
    }
    if (m_pipeline) {
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        gst_object_unref(m_pipeline);
        m_pipeline = nullptr;
    }
}

void Ringtone::applyStep() {
    if (!m_src) return;
    const Step &s = kCadence[m_step];
    g_object_set(m_src, "freq", s.freq, "volume", s.volume, nullptr);
    m_step = (m_step + 1) % kSteps;
    m_timer.start(s.ms);
}
