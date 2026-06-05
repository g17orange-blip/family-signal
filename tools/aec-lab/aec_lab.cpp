// aec-lab: measures how well GStreamer's webrtcdsp echo cancellation copes
// with the exact audio topology the signal client uses for calls.
//
// The whole experiment is synthetic — no audio hardware:
//
//   far-end voice (pink noise) ──opus──rtp──jitterbuffer──opus dec──┐
//                                                                   ▼
//        silence (keeps mixer live, as in the client) ────────► audiomixer
//                                                                   │
//                                                            webrtcechoprobe
//                                                                   │
//                                                                  tee
//                                       ┌───────────────────────────┴───┐
//                                       ▼ "speakers"                    ▼ "room"
//                              fakesink sync=true            queue → clip → gain
//                              render-delay=R                → pad offset +D
//                                                                       │
//        silence ("quiet room") ─────────────────────────────► audiomixer ("mic")
//                                                                   │
//                                                               webrtcdsp
//                                                                   │
//                                                            fakesink ("uplink")
//
// D models the full render path delay (driver/device buffer + acoustics):
// the echo of a sample with timestamp t re-enters the mic at t+D. The probe
// predicts t+L from the announced pipeline latency; AEC effectiveness as a
// function of the skew between D and L is exactly what we want to chart.
//
// RMS is measured by pad probes on the dsp's sink (raw echo) and src
// (residual) pads, bucketed per second of stream time. ERLE = raw - residual.
//
// Output (stdout, greppable):
//   SEC <n> raw=<dBFS> out=<dBFS> erle=<dB>
//   STEADY raw=<dBFS> out=<dBFS> erle=<dB>        (mean of the last 5 s)
//   LATENCY announced=<ms>
//
// On machines whose GStreamer lacks webrtcdsp (homebrew) the dsp is replaced
// with identity so the measurement plumbing itself can still be exercised
// (expect erle≈0); the run is marked DSP=absent.

#include <gst/gst.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace {

struct Options {
    int    durationSec  = 20;
    int    jitterMs     = 200;   // rtpjitterbuffer latency, as in the client
    int    renderMs     = 100;   // announced "speaker" device latency
    int    echoDelayMs  = 90;    // actual render-path delay D
    double echoGainDb   = -6.0;  // speaker→mic coupling strength
    bool   clip         = false; // grandpa mode: speakers driven into clipping
    bool   agc          = true;  // webrtcdsp gain-control
    bool   aec          = true;  // echo-cancel (off = control run)
};

// One second of accumulated sample energy on one pad.
struct Bucket {
    double sumSquares = 0.0;
    long   samples    = 0;
};

constexpr int kMaxSeconds = 600;

struct Meter {
    std::mutex          mutex;
    std::vector<Bucket> buckets{kMaxSeconds};
};

Meter g_raw;  // dsp sink pad: echo as the mic hears it
Meter g_out;  // dsp src pad: what would be sent to the peer

GstElement *g_pipeline = nullptr;  // for LATENCY recalculation from the bus

double bucketDb(const Bucket &b) {
    if (b.samples == 0) return -120.0;
    const double rms = std::sqrt(b.sumSquares / b.samples);
    return rms > 0 ? 20.0 * std::log10(rms / 32768.0) : -120.0;
}

GstPadProbeReturn meterProbe(GstPad *, GstPadProbeInfo *info, gpointer user) {
    Meter *meter = static_cast<Meter *>(user);
    GstBuffer *buf = GST_PAD_PROBE_INFO_BUFFER(info);
    if (!buf || !GST_BUFFER_PTS_IS_VALID(buf)) return GST_PAD_PROBE_OK;
    const int sec = static_cast<int>(GST_BUFFER_PTS(buf) / GST_SECOND);
    if (sec < 0 || sec >= kMaxSeconds) return GST_PAD_PROBE_OK;

    GstMapInfo map;
    if (!gst_buffer_map(buf, &map, GST_MAP_READ)) return GST_PAD_PROBE_OK;
    const int16_t *s = reinterpret_cast<const int16_t *>(map.data);
    const size_t n = map.size / sizeof(int16_t);
    double sum = 0.0;
    for (size_t i = 0; i < n; ++i) sum += double(s[i]) * double(s[i]);
    gst_buffer_unmap(buf, &map);

    std::lock_guard<std::mutex> lock(meter->mutex);
    meter->buckets[sec].sumSquares += sum;
    meter->buckets[sec].samples    += long(n);
    return GST_PAD_PROBE_OK;
}

gboolean onBus(GstBus *, GstMessage *msg, gpointer loop) {
    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR: {
            GError *err = nullptr; gchar *dbg = nullptr;
            gst_message_parse_error(msg, &err, &dbg);
            fprintf(stderr, "ERROR from %s: %s\n%s\n",
                    GST_OBJECT_NAME(msg->src), err->message, dbg ? dbg : "");
            g_clear_error(&err); g_free(dbg);
            g_main_loop_quit(static_cast<GMainLoop *>(loop));
            break;
        }
        case GST_MESSAGE_LATENCY:
            // Same handling as the client: redistribute latency when an
            // element (the jitterbuffer, the sink) updates its requirements.
            if (g_pipeline) gst_bin_recalculate_latency(GST_BIN(g_pipeline));
            break;
        default: break;
    }
    return TRUE;
}

bool parseArgs(int argc, char **argv, Options *o) {
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        auto intArg = [&](const char *key, int *out) {
            const size_t len = strlen(key);
            if (strncmp(a, key, len) == 0 && a[len] == '=') {
                *out = atoi(a + len + 1); return true;
            }
            return false;
        };
        if (intArg("--duration", &o->durationSec)) continue;
        if (intArg("--jitter-ms", &o->jitterMs)) continue;
        if (intArg("--render-ms", &o->renderMs)) continue;
        if (intArg("--echo-delay-ms", &o->echoDelayMs)) continue;
        if (strncmp(a, "--echo-gain-db=", 15) == 0) { o->echoGainDb = atof(a + 15); continue; }
        if (strcmp(a, "--clip") == 0)   { o->clip = true; continue; }
        if (strcmp(a, "--no-agc") == 0) { o->agc = false; continue; }
        if (strcmp(a, "--no-aec") == 0) { o->aec = false; continue; }
        fprintf(stderr, "unknown argument: %s\n", a);
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char **argv) {
    Options opt;
    if (!parseArgs(argc, argv, &opt)) return 2;
    gst_init(nullptr, nullptr);

    // Degrade to identity where the plugin is unavailable so the harness
    // itself can be tested on any machine.
    bool haveDsp = false;
    if (GstElementFactory *f = gst_element_factory_find("webrtcdsp")) {
        gst_object_unref(f);
        haveDsp = true;
    }

    const char *caps = "audio/x-raw,format=S16LE,rate=48000,channels=1";
    std::string dspDesc, probeDesc;
    if (haveDsp) {
        char buf[256];
        snprintf(buf, sizeof buf,
                 "webrtcdsp name=dsp probe=echoprobe echo-cancel=%s "
                 "noise-suppression=true gain-control=%s",
                 opt.aec ? "true" : "false", opt.agc ? "true" : "false");
        dspDesc = buf;
        probeDesc = "webrtcechoprobe name=echoprobe";
    } else {
        // The probe ships in the same plugin — swap both for pass-throughs.
        dspDesc = "identity name=dsp";
        probeDesc = "identity name=echoprobe";
    }

    char desc[4096];
    snprintf(desc, sizeof desc,
        // Far end: voice → the same opus/jitterbuffer leg a real call has,
        // so the announced latency matches production.
        "audiotestsrc name=farend is-live=true wave=pink-noise volume=0.6 ! %s ! "
        "  opusenc bitrate=32000 inband-fec=true ! rtpopuspay pt=111 ! "
        "  application/x-rtp,media=audio,encoding-name=OPUS,clock-rate=48000,payload=111 ! "
        "  rtpjitterbuffer latency=%d ! rtpopusdepay ! opusdec ! "
        "  audioconvert ! audioresample ! %s ! "
        "  audiomixer name=amix ! %s ! "
        "  %s ! tee name=spk "
        // The client keeps the playback side prerolled with a silent live
        // source; mirror that.
        "audiotestsrc is-live=true wave=silence ! %s ! amix. "
        // Speakers: a sink that syncs to the clock and announces a device
        // latency, like a real audio sink would.
        "spk. ! queue ! fakesink name=speaker sync=true qos=false "
        // Room: what left the speakers re-enters the mic D ms later,
        // attenuated, optionally clipped (cheap speakers at full volume).
        "spk. ! queue name=roomq max-size-time=3000000000 max-size-buffers=0 max-size-bytes=0 ! "
        "  audioamplify name=clipamp amplification=1.0 clipping-method=none ! "
        "  volume name=echogain volume=1.0 ! "
        "  audiomixer name=mic ! %s ! %s ! "
        "  fakesink name=uplink sync=false "
        "audiotestsrc is-live=true wave=silence ! %s ! mic. ",
        caps, opt.jitterMs, caps, caps, probeDesc.c_str(), caps, caps,
        dspDesc.c_str(), caps);

    GError *err = nullptr;
    GstElement *pipeline = gst_parse_launch(desc, &err);
    if (!pipeline || err) {
        // parse_launch can hand back a half-linked pipeline together with
        // an error — running that yields confusing not-linked failures, so
        // treat any error as fatal.
        fprintf(stderr, "parse_launch: %s\n", err ? err->message : "?");
        return 1;
    }
    g_pipeline = pipeline;

    auto byName = [&](const char *n) {
        return gst_bin_get_by_name(GST_BIN(pipeline), n);
    };

    // Speaker latency announcement.
    if (GstElement *spk = byName("speaker")) {
        g_object_set(spk, "render-delay",
                     guint64(opt.renderMs) * GST_MSECOND, nullptr);
        gst_object_unref(spk);
    }
    // Coupling strength and the grandpa-mode clipping.
    if (GstElement *gain = byName("echogain")) {
        g_object_set(gain, "volume", std::pow(10.0, opt.echoGainDb / 20.0),
                     nullptr);
        gst_object_unref(gain);
    }
    if (opt.clip) {
        if (GstElement *clip = byName("clipamp")) {
            g_object_set(clip, "amplification", 6.0f, nullptr);
            gst_util_set_object_arg(G_OBJECT(clip), "clipping-method", "hard");
            gst_object_unref(clip);
        }
    }
    // The actual render-path delay D: shift the echo branch's running time.
    if (GstElement *gain = byName("echogain")) {
        GstPad *src = gst_element_get_static_pad(gain, "src");
        gst_pad_set_offset(src, gint64(opt.echoDelayMs) * GST_MSECOND);
        gst_object_unref(src);
        gst_object_unref(gain);
    }
    // RMS meters around the dsp.
    if (GstElement *dsp = byName("dsp")) {
        GstPad *sink = gst_element_get_static_pad(dsp, "sink");
        GstPad *src  = gst_element_get_static_pad(dsp, "src");
        gst_pad_add_probe(sink, GST_PAD_PROBE_TYPE_BUFFER, meterProbe, &g_raw, nullptr);
        gst_pad_add_probe(src,  GST_PAD_PROBE_TYPE_BUFFER, meterProbe, &g_out, nullptr);
        gst_object_unref(sink);
        gst_object_unref(src);
        gst_object_unref(dsp);
    }

    GMainLoop *loop = g_main_loop_new(nullptr, FALSE);
    GstBus *bus = gst_element_get_bus(pipeline);
    gst_bus_add_watch(bus, onBus, loop);

    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    g_timeout_add_seconds(opt.durationSec,
        +[](gpointer l) -> gboolean {
            g_main_loop_quit(static_cast<GMainLoop *>(l));
            return G_SOURCE_REMOVE;
        }, loop);
    g_main_loop_run(loop);

    // Announced pipeline latency — the probe's idea of when audio leaves
    // the speakers; print it so skew (D - L) is visible in the report.
    GstQuery *q = gst_query_new_latency();
    if (gst_element_query(pipeline, q)) {
        GstClockTime minLat = 0;
        gboolean live = FALSE;
        gst_query_parse_latency(q, &live, &minLat, nullptr);
        printf("LATENCY announced=%dms\n", int(minLat / GST_MSECOND));
    }
    gst_query_unref(q);

    gst_element_set_state(pipeline, GST_STATE_NULL);

    printf("CONFIG dsp=%s aec=%d agc=%d jitter=%dms render=%dms delay=%dms "
           "gain=%.1fdB clip=%d\n",
           haveDsp ? "present" : "absent", opt.aec ? 1 : 0, opt.agc ? 1 : 0,
           opt.jitterMs, opt.renderMs, opt.echoDelayMs, opt.echoGainDb,
           opt.clip ? 1 : 0);

    double steadyRaw = 0, steadyOut = 0;
    int steadyN = 0;
    for (int s = 0; s < opt.durationSec && s < kMaxSeconds; ++s) {
        Bucket raw, out;
        {
            std::lock_guard<std::mutex> lock(g_raw.mutex);
            raw = g_raw.buckets[s];
        }
        {
            std::lock_guard<std::mutex> lock(g_out.mutex);
            out = g_out.buckets[s];
        }
        if (raw.samples == 0 && out.samples == 0) continue;
        const double rawDb = bucketDb(raw), outDb = bucketDb(out);
        printf("SEC %d raw=%.1f out=%.1f erle=%.1f\n", s, rawDb, outDb,
               rawDb - outDb);
        if (s >= opt.durationSec - 5) {
            steadyRaw += rawDb; steadyOut += outDb; ++steadyN;
        }
    }
    if (steadyN > 0)
        printf("STEADY raw=%.1f out=%.1f erle=%.1f\n", steadyRaw / steadyN,
               steadyOut / steadyN, (steadyRaw - steadyOut) / steadyN);

    gst_object_unref(bus);
    g_main_loop_unref(loop);
    gst_object_unref(pipeline);
    return 0;
}
