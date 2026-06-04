// WebRTC media session built on top of GStreamer's `webrtcbin`.
//
// Pipeline shape for outbound media:
//
//   autovideosrc → videoconvert → videoscale → capsfilter(640x480@30) →
//       x264enc(tune=zerolatency,bitrate=600) → rtph264pay → webrtcbin
//
//   autoaudiosrc → audioconvert → audioresample →
//       opusenc → rtpopuspay → webrtcbin
//
// On the inbound side webrtcbin emits `pad-added` for every incoming RTP
// stream; we route each into decodebin and then to the appropriate sink
// (autoaudiosink for audio, glimagesink for video — the latter accepts a
// platform-specific window handle so the picture appears inside a Qt
// widget rather than in its own top-level window).
//
// Bus integration: we don't have a GMainLoop here (Qt owns the event
// loop), so a 30 ms QTimer drains gst_bus_pop()/gst_bus_pop_filtered()
// on the Qt thread. Reliable, lower-overhead than gst_bus_add_watch
// when there's no GLib loop, and avoids cross-thread Qt calls.

#include "WebRtcSession.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>

extern "C" {
#include <gst/gst.h>
#include <gst/sdp/sdp.h>
#include <gst/video/videooverlay.h>
#define GST_USE_UNSTABLE_API
#include <gst/webrtc/webrtc.h>
}

namespace {

// When the app ships as a relocatable bundle (macOS .app, Linux AppImage),
// its GStreamer plugins live next to the executable rather than in a system
// path. Point GStreamer at them before gst_init so webrtcbin & friends load.
// Harmless on a normal build (the bundled dir simply won't exist).
void pointGstAtBundledPlugins() {
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QStringList candidates = {
        appDir.filePath(QStringLiteral("../Resources/gstreamer-1.0")), // macOS .app
        appDir.filePath(QStringLiteral("../PlugIns/gstreamer-1.0")),   // legacy .app layout
        appDir.filePath(QStringLiteral("../lib/gstreamer-1.0")),       // *nix layout
        appDir.filePath(QStringLiteral("gstreamer-1.0")),              // alongside exe
    };
    for (const QString &c : candidates) {
        const QString path = QFileInfo(c).canonicalFilePath();
        if (!path.isEmpty() && QFileInfo(path).isDir()) {
            qputenv("GST_PLUGIN_SYSTEM_PATH_1_0", path.toUtf8());
            // Without the helper binary GStreamer scans plugins in-process
            // ("Couldn't create helper process" + a slower first start, and
            // a crashing plugin takes the app down with it). Both env names
            // are checked: the POSIX loader reads the _1_0 variant first,
            // the win32 loader the bare one.
            for (const char *name : {"gst-plugin-scanner", "gst-plugin-scanner.exe"}) {
                const QString scanner = appDir.filePath(QLatin1String(name));
                if (QFileInfo::exists(scanner)) {
                    qputenv("GST_PLUGIN_SCANNER_1_0", scanner.toUtf8());
                    qputenv("GST_PLUGIN_SCANNER", scanner.toUtf8());
                    break;
                }
            }
            break;
        }
    }
}

// ---- one-shot GStreamer init -------------------------------------------
void ensureGstInit() {
    static bool inited = false;
    if (!inited) {
        pointGstAtBundledPlugins();
        gst_init(nullptr, nullptr);
        inited = true;
    }
}

// Convenience: take a SDP string + type ("offer"|"answer") and build the
// GstWebRTCSessionDescription that webrtcbin's set-remote-description
// signal expects. Returns nullptr on parse failure; caller owns the result.
GstWebRTCSessionDescription *makeSessionDescription(const char *type, const QByteArray &sdp) {
    GstSDPMessage *msg = nullptr;
    if (gst_sdp_message_new(&msg) != GST_SDP_OK) return nullptr;
    if (gst_sdp_message_parse_buffer(reinterpret_cast<const guint8 *>(sdp.constData()),
                                     sdp.size(), msg) != GST_SDP_OK) {
        gst_sdp_message_free(msg);
        return nullptr;
    }
    const GstWebRTCSDPType kind =
        (qstrcmp(type, "offer") == 0) ? GST_WEBRTC_SDP_TYPE_OFFER
                                      : GST_WEBRTC_SDP_TYPE_ANSWER;
    return gst_webrtc_session_description_new(kind, msg);
}

// webrtcbin's nice agent insists on the stun://host:port form; the config
// carries the conventional stun:host:port. Without this fix STUN was
// silently skipped ("has no host" in the logs) and connectivity relied on
// host candidates + TURN only.
QByteArray stunUri(const QString &configured) {
    QString s = configured;
    if (s.startsWith(QStringLiteral("stun:")) &&
        !s.startsWith(QStringLiteral("stun://")))
        s.replace(0, 5, QStringLiteral("stun://"));
    return s.toUtf8();
}

// Copies an RGBA appsink sample into a QImage (deep copy — the buffer is
// unmapped immediately). Returns a null image on any failure.
QImage sampleToImage(GstSample *sample) {
    if (!sample) return {};
    int w = 0;
    int h = 0;
    if (GstCaps *caps = gst_sample_get_caps(sample)) {
        if (const GstStructure *st = gst_caps_get_structure(caps, 0)) {
            gst_structure_get_int(st, "width", &w);
            gst_structure_get_int(st, "height", &h);
        }
    }
    GstBuffer *buf = gst_sample_get_buffer(sample);
    if (!buf || w <= 0 || h <= 0) return {};
    GstMapInfo map;
    if (!gst_buffer_map(buf, &map, GST_MAP_READ)) return {};
    const QImage frame(map.data, w, h, w * 4, QImage::Format_RGBA8888);
    QImage out = frame.copy();
    gst_buffer_unmap(buf, &map);
    return out;
}

} // namespace

QString WebRtcSession::mediaDiagnostics() {
    ensureGstInit();
    QString out;

    // Which capture elements does this GStreamer build offer?
    const char *interesting[] = {
        "autovideosrc", "autoaudiosrc",
#ifdef Q_OS_WIN
        "mfvideosrc", "ksvideosrc", "wasapisrc", "wasapi2src", "directsoundsrc",
#elif defined(Q_OS_MACOS)
        "avfvideosrc", "osxaudiosrc",
#else
        "v4l2src", "pipewiresrc", "pulsesrc", "alsasrc",
#endif
        "webrtcdsp", "webrtcbin",
        "x264enc", "opusenc", "rtph264pay", "rtpopuspay",
        "dtlssrtpenc", "nicesink",
    };
    out += QStringLiteral("Элементы GStreamer:\n");
    for (const char *name : interesting) {
        GstElementFactory *f = gst_element_factory_find(name);
        out += QStringLiteral("  %1 — %2\n")
                   .arg(QLatin1String(name),
                        f ? QStringLiteral("есть") : QStringLiteral("НЕТ"));
        if (f) gst_object_unref(f);
    }

    // Live test: run each capture source into fakesink for a moment.
    const struct { const char *label; const char *launch; } tests[] = {
        {"Камера",   "autovideosrc ! fakesink"},
        {"Микрофон", "autoaudiosrc ! fakesink"},
        // The exact capture→encode chain a video call uses; if this fails
        // while the plain camera test passes, it's caps negotiation or the
        // encoder, not the device.
        {"Видеотракт звонка",
         "autovideosrc ! videoconvert ! videoscale ! "
         "video/x-raw,width=640,height=480 ! "
         "x264enc tune=zerolatency speed-preset=ultrafast bitrate=600 ! fakesink"},
    };
    for (const auto &t : tests) {
        GError *err = nullptr;
        GstElement *pipe = gst_parse_launch(t.launch, &err);
        if (!pipe) {
            out += QStringLiteral("%1: не удалось собрать конвейер (%2)\n")
                       .arg(QString::fromUtf8(t.label),
                            err ? QString::fromUtf8(err->message)
                                : QStringLiteral("?"));
            if (err) g_error_free(err);
            continue;
        }
        const GstStateChangeReturn r =
            gst_element_set_state(pipe, GST_STATE_PLAYING);
        GstState state = GST_STATE_NULL;
        gst_element_get_state(pipe, &state, nullptr, 3 * GST_SECOND);
        QString verdict;
        if (r == GST_STATE_CHANGE_FAILURE || state != GST_STATE_PLAYING) {
            GstBus *bus = gst_element_get_bus(pipe);
            QString detail;
            while (GstMessage *msg = gst_bus_pop_filtered(bus, GST_MESSAGE_ERROR)) {
                GError *e = nullptr; gchar *dbg = nullptr;
                gst_message_parse_error(msg, &e, &dbg);
                if (e) detail = QString::fromUtf8(e->message);
                if (e) g_error_free(e);
                g_free(dbg);
                gst_message_unref(msg);
            }
            gst_object_unref(bus);
            verdict = QStringLiteral("НЕ РАБОТАЕТ%1")
                          .arg(detail.isEmpty() ? QString()
                                                : QStringLiteral(" — %1").arg(detail));
        } else {
            verdict = QStringLiteral("работает");
        }
        out += QStringLiteral("%1: %2\n").arg(QString::fromUtf8(t.label), verdict);
        gst_element_set_state(pipe, GST_STATE_NULL);
        gst_object_unref(pipe);
    }
    return out;
}

WebRtcSession::WebRtcSession(QObject *parent, Mode mode)
    : QObject(parent), m_mode(mode) {
    ensureGstInit();
    m_busTimer.setInterval(30);
    connect(&m_busTimer, &QTimer::timeout, this, &WebRtcSession::pollBus);
}

WebRtcSession::~WebRtcSession() {
    stop();
}

void WebRtcSession::setConfig(const Config &cfg) { m_config = cfg; }

void WebRtcSession::prepare(bool withVideo) {
    if (m_pipeline && m_withVideo != withVideo) stop();   // rebuild on mode change
    m_withVideo = withVideo;
}

bool WebRtcSession::start() {
    buildPipelineIfNeeded();
    if (!m_pipeline) return false;
    gst_element_set_state(m_pipeline, GST_STATE_READY);
    m_busTimer.start();
    return true;
}

void WebRtcSession::stop() {
    m_busTimer.stop();
    if (m_pipeline) {
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        gst_object_unref(m_pipeline);
        m_pipeline = nullptr;
        m_webrtc = nullptr;
        m_dataChannel = nullptr;
    }
    m_inCall = false;
    m_channelOpen = false;
    m_offerCreated = false;
    m_answerApplied = false;
}

void WebRtcSession::buildPipelineIfNeeded() {
    if (m_pipeline) return;

    // Build everything except the inbound branches via gst_parse_launch —
    // the textual syntax is markedly easier to audit than the equivalent
    // chain of gst_element_factory_make + gst_element_link_many calls.
    //
    // Notes on the chosen encoders:
    //   x264enc — software H.264, available everywhere via gst-plugins-ugly.
    //             tune=zerolatency disables B-frames and the look-ahead
    //             queue (otherwise latency balloons to 1-2 s).
    //             bitrate is in kbps; 600 is plenty for 480p talking-head.
    //   opusenc — works on every platform; perceptual-quality variable
    //             bitrate around 32 kbps is already telephone-clear.
    // Chat mode carries only the DataChannel: no camera, no mic, no media
    // m-lines — the connection is silent and cheap enough to keep open in
    // the background whenever the peer is online. Built by hand because
    // gst_parse_launch with a single element returns the element itself,
    // not a pipeline, and the GST_BIN lookup below would silently fail.
    if (m_mode == Mode::Chat) {
        m_pipeline = gst_pipeline_new("chat-session");
        m_webrtc = gst_element_factory_make("webrtcbin", "webrtc");
        if (!m_webrtc) {
            emit error(QStringLiteral("webrtcbin element unavailable"));
            gst_object_unref(m_pipeline);
            m_pipeline = nullptr;
            return;
        }
        g_object_set(m_webrtc,
                     "bundle-policy", GST_WEBRTC_BUNDLE_POLICY_MAX_BUNDLE,
                     "stun-server", stunUri(m_config.stunUrl).constData(),
                     nullptr);
        gst_bin_add(GST_BIN(m_pipeline), m_webrtc);
        gst_object_ref(m_webrtc);   // match the parse_launch path's get_by_name ref
        attachWebrtcSignals();
        return;
    }

    // Echo cancellation: webrtcdsp (paired with webrtcechoprobe on the
    // playback side, see onIncomingStream) kills the speaker→mic feedback
    // loop that otherwise builds up on laptops without headphones. The
    // element ships with the official Windows/Ubuntu GStreamer builds but
    // not with Homebrew's — degrade gracefully where it's missing.
    QByteArray aec;
    if (GstElementFactory *f = gst_element_factory_find("webrtcdsp")) {
        gst_object_unref(f);
        // webrtcdsp wants S16 mono at a fixed rate. The probe is referenced
        // BY EXPLICIT NAME: auto-generated names ("webrtcechoprobe0", 1, …)
        // increment per process, so after a pipeline rebuild the dsp would
        // look for probe0 while the new probe is probe1 — and kill the call.
        aec = "audio/x-raw,format=S16LE,rate=48000,channels=1 ! "
              "webrtcdsp probe=echoprobe echo-cancel=true "
              "  noise-suppression=true gain-control=true ! ";
        m_haveAec = true;
    }

    const QByteArray core =
        "webrtcbin name=webrtc bundle-policy=max-bundle "
        "  stun-server=" + stunUri(m_config.stunUrl) + " ";
    // Audio-only calls skip the whole camera branch — the camera LED never
    // lights up and the SDP carries no video m-line.
    // No hard framerate in the caps: plenty of webcams (especially on
    // Windows via Media Foundation) can't do exactly 30/1 at 640x480 and
    // the whole pipeline then fails to negotiate. Let the camera pick.
    // Self-view: frames are pulled into Qt via appsink and painted by a
    // plain widget (rounded corners, always above the native video surface).
    // Both previous approaches died: a second GL overlay window got "Quit
    // requested" when restacked, and compositing into the remote frame put
    // the preview under the window's control strip.
    // Width-only caps: the height follows the camera's real aspect ratio
    // (squeezing a 16:9 sensor into 4:3 looked awful). 320 wide so the
    // preview stays sharp on hi-dpi screens.
    const QByteArray selfPip = m_withVideo ?
        "selftee. ! queue max-size-buffers=2 leaky=downstream ! "
        "  videoconvert ! videoscale ! "
        "  video/x-raw,format=RGBA,width=320 ! "
        "  appsink name=selfsink max-buffers=1 drop=true sync=false " : "";
    const QByteArray videoBranch = m_withVideo ?
        "autovideosrc ! videoconvert ! videoscale ! "
        "  video/x-raw,width=640,height=480 ! "
        "  tee name=selftee ! "
        "  queue max-size-buffers=10 leaky=downstream ! "
        "  x264enc tune=zerolatency speed-preset=ultrafast bitrate=600 key-int-max=30 ! "
        "  video/x-h264,profile=constrained-baseline ! "
        "  rtph264pay config-interval=1 pt=96 ! "
        "  application/x-rtp,media=video,encoding-name=H264,payload=96 ! webrtc. "
        + selfPip : QByteArray();
    // With AEC the playback side must exist BEFORE webrtcdsp processes its
    // first mic buffer ("No echo probe ... found" kills the pipeline
    // otherwise). Build it statically: a silent live source keeps the mixer
    // and sink prerolled; remote audio is mixed in when it arrives
    // (see onIncomingStream).
    // Caps pinned to the DSP's native format (S16/48k/mono) along the whole
    // chain: leaving the mixer and probe to negotiate freely ends in
    // "not-negotiated" on Windows. Convert/resample AFTER the probe adapts
    // to whatever the actual audio sink wants.
    const QByteArray aecPlayback = m_haveAec ?
        "audiotestsrc wave=silence is-live=true ! "
        "  audio/x-raw,format=S16LE,rate=48000,channels=1 ! "
        "  audiomixer name=amix ! "
        "  audio/x-raw,format=S16LE,rate=48000,channels=1 ! "
        "  webrtcechoprobe name=echoprobe ! "
        "  audioconvert ! audioresample ! autoaudiosink " : "";
    const QByteArray launch = core + videoBranch +
        "autoaudiosrc ! audioconvert ! audioresample ! " + aec +
        "  queue max-size-buffers=10 leaky=downstream ! "
        "  opusenc bitrate=32000 ! rtpopuspay pt=111 ! "
        "  application/x-rtp,media=audio,encoding-name=OPUS,payload=111 ! webrtc. " +
        aecPlayback;

    GError *err = nullptr;
    m_pipeline = gst_parse_launch(launch.constData(), &err);
    if (!m_pipeline) {
        emit error(QStringLiteral("gst_parse_launch: %1").arg(err ? err->message : "unknown"));
        if (err) g_error_free(err);
        return;
    }
    m_webrtc = gst_bin_get_by_name(GST_BIN(m_pipeline), "webrtc");

    // Pump self-view frames to the UI. The appsink callback fires on a
    // streaming thread; QImage is implicitly shared, and the queued signal
    // hands it to the GUI thread safely (deep copy taken below because the
    // GstBuffer memory is unmapped right after).
    if (GstElement *selfsink = gst_bin_get_by_name(GST_BIN(m_pipeline), "selfsink")) {
        g_object_set(selfsink, "emit-signals", TRUE, nullptr);
        g_signal_connect(selfsink, "new-sample",
            G_CALLBACK(+[](GstElement *sink, gpointer s) -> GstFlowReturn {
                auto *self = static_cast<WebRtcSession *>(s);
                GstSample *sample = nullptr;
                g_signal_emit_by_name(sink, "pull-sample", &sample);
                const QImage img = sampleToImage(sample);
                if (sample) gst_sample_unref(sample);
                if (!img.isNull()) emit self->selfFrame(img);
                return GST_FLOW_OK;
            }), this);
        gst_object_unref(selfsink);
    }

    attachWebrtcSignals();
}

// Shared by both pipeline shapes: TURN config + the webrtcbin signal hookups.
void WebRtcSession::attachWebrtcSignals() {
    // TURN server, if configured. webrtcbin accepts a turn:// URI of the
    // form turn://user:pass@host:port — note that any '@' or ':' in the
    // password must be percent-encoded, but for our two-user prototype we
    // generate the password ourselves and avoid such characters.
    if (!m_config.turn.url.isEmpty() && !m_config.turn.username.isEmpty()) {
        // Preserve the scheme: a turns: URL (TLS relay on 5349) must stay
        // turns://, not be mangled into turn://. Strip the scheme prefix
        // (and optional //) and re-emit it explicitly.
        QString hostPort = m_config.turn.url;
        const bool tls = hostPort.startsWith(QStringLiteral("turns:"));
        hostPort.remove(0, hostPort.indexOf(QLatin1Char(':')) + 1);
        if (hostPort.startsWith(QStringLiteral("//"))) hostPort.remove(0, 2);
        const QString turnUri = QStringLiteral("%1://%2:%3@%4")
            .arg(tls ? QStringLiteral("turns") : QStringLiteral("turn"),
                 m_config.turn.username, m_config.turn.password, hostPort);
        gboolean ok = FALSE;
        g_signal_emit_by_name(m_webrtc, "add-turn-server", turnUri.toUtf8().constData(), &ok);
        if (!ok) qWarning() << "webrtcbin rejected TURN URI" << turnUri;
    }

    g_signal_connect(m_webrtc, "on-negotiation-needed",
                     G_CALLBACK(&WebRtcSession::onNegotiationNeededCb), this);
    g_signal_connect(m_webrtc, "on-ice-candidate",
                     G_CALLBACK(&WebRtcSession::onIceCandidateCb), this);
    g_signal_connect(m_webrtc, "pad-added",
                     G_CALLBACK(&WebRtcSession::onIncomingStreamCb), this);
    g_signal_connect(m_webrtc, "on-data-channel",
                     G_CALLBACK(&WebRtcSession::onDataChannelCb), this);
}

// --- Public call-control surface ----------------------------------------

void WebRtcSession::startCall(const QString &peerId) {
    if (!m_webrtc) return;
    m_peerId = peerId;
    m_inCall = true;
    m_isCaller = true;

    // Caller side creates the DataChannel — only the offerer can create it
    // up-front in the SDP. The answerer receives it via on-data-channel.
    GstStructure *opts = gst_structure_new("data-channel-options",
                                            "ordered", G_TYPE_BOOLEAN, TRUE,
                                            nullptr);
    void *channel = nullptr;
    g_signal_emit_by_name(m_webrtc, "create-data-channel", "chat", opts, &channel);
    gst_structure_free(opts);
    if (channel) attachDataChannel(channel);

    gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    // Call mode: create-offer happens when webrtcbin fires
    // on-negotiation-needed as the media pads link up during the PLAYING
    // transition. Chat mode has no media — webrtcbin raised the flag once
    // at start() (before m_isCaller was set, so we ignored it) and won't
    // re-emit for the data channel, so kick the offer explicitly.
    if (m_mode == Mode::Chat) onNegotiationNeeded();
}

void WebRtcSession::acceptOffer(const QString &peerId, const QString &remoteSdp) {
    if (!m_webrtc) return;
    m_peerId = peerId;
    m_inCall = true;
    m_isCaller = false;
    gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    setRemoteDescription("offer", remoteSdp);
    // Now produce the answer.
    GstPromise *promise = gst_promise_new_with_change_func(
        +[](GstPromise *p, gpointer s) {
            static_cast<WebRtcSession *>(s)->onAnswerCreated(p);
        }, this, nullptr);
    g_signal_emit_by_name(m_webrtc, "create-answer", nullptr, promise);
}

void WebRtcSession::provideAnswer(const QString &remoteSdp) {
    // Belt-and-braces against duplicate delivery: applying an answer in the
    // stable state is an error inside webrtcbin, so drop repeats here.
    if (m_answerApplied) return;
    m_answerApplied = true;
    setRemoteDescription("answer", remoteSdp);
}

void WebRtcSession::setRemoteDescription(const QString &type, const QString &sdp) {
    GstWebRTCSessionDescription *desc =
        makeSessionDescription(type.toUtf8().constData(), sdp.toUtf8());
    if (!desc) {
        emit error(QStringLiteral("failed to parse remote SDP"));
        return;
    }
    GstPromise *promise = gst_promise_new();
    g_signal_emit_by_name(m_webrtc, "set-remote-description", desc, promise);
    gst_promise_interrupt(promise);
    gst_promise_unref(promise);
    gst_webrtc_session_description_free(desc);
}

void WebRtcSession::addRemoteIce(const QString &candidate, int sdpMLineIndex) {
    if (!m_webrtc) return;
    g_signal_emit_by_name(m_webrtc, "add-ice-candidate",
                          static_cast<guint>(sdpMLineIndex),
                          candidate.toUtf8().constData());
}

void WebRtcSession::hangup() {
    // Tear the whole pipeline down, not just to READY: webrtcbin is not
    // reusable across sessions — transceivers and ICE state accumulate and
    // the next call fails to negotiate until the app is restarted. The
    // next start() builds a fresh pipeline.
    stop();
    emit callEnded();
}

bool WebRtcSession::sendText(const QString &msgId, const QString &text) {
    if (!m_dataChannel || !m_channelOpen) return false;
    // Structured envelope so the receiver can ack: delivery is confirmed by
    // the peer, not assumed at send time (SCTP buffering ≠ delivered).
    QJsonObject o;
    o.insert(QStringLiteral("v"), 1);
    o.insert(QStringLiteral("t"), QStringLiteral("msg"));
    o.insert(QStringLiteral("id"), msgId);
    o.insert(QStringLiteral("text"), text);
    const QByteArray raw = QJsonDocument(o).toJson(QJsonDocument::Compact);
    g_signal_emit_by_name(m_dataChannel, "send-string", raw.constData());
    return true;
}

bool WebRtcSession::sendReadReceipts(const QStringList &msgIds) {
    if (!m_dataChannel || !m_channelOpen || msgIds.isEmpty()) return false;
    QJsonObject o;
    o.insert(QStringLiteral("v"), 1);
    o.insert(QStringLiteral("t"), QStringLiteral("read"));
    o.insert(QStringLiteral("ids"), QJsonArray::fromStringList(msgIds));
    const QByteArray raw = QJsonDocument(o).toJson(QJsonDocument::Compact);
    g_signal_emit_by_name(m_dataChannel, "send-string", raw.constData());
    return true;
}

void WebRtcSession::onChannelMessage(const QString &raw) {
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(raw.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        // Legacy peer (pre-ack protocol): the string is the message itself.
        emit textReceived(m_peerId, QString(), raw);
        return;
    }
    const auto o = doc.object();
    const QString type = o.value(QStringLiteral("t")).toString();
    const QString id   = o.value(QStringLiteral("id")).toString();
    if (type == QLatin1String("msg")) {
        emit textReceived(m_peerId, id, o.value(QStringLiteral("text")).toString());
        // Confirm receipt — the sender keeps the message queued until then.
        if (m_dataChannel && m_channelOpen && !id.isEmpty()) {
            QJsonObject ack;
            ack.insert(QStringLiteral("v"), 1);
            ack.insert(QStringLiteral("t"), QStringLiteral("ack"));
            ack.insert(QStringLiteral("id"), id);
            const QByteArray rawAck = QJsonDocument(ack).toJson(QJsonDocument::Compact);
            g_signal_emit_by_name(m_dataChannel, "send-string", rawAck.constData());
        }
    } else if (type == QLatin1String("ack")) {
        emit textDelivered(id);
    } else if (type == QLatin1String("read")) {
        QStringList ids;
        for (const auto v : o.value(QStringLiteral("ids")).toArray())
            ids.append(v.toString());
        if (!ids.isEmpty()) emit peerReadMessages(ids);
    }
}

// --- GStreamer callbacks ------------------------------------------------

void WebRtcSession::onNegotiationNeededCb(GstElement *, gpointer self) {
    static_cast<WebRtcSession *>(self)->onNegotiationNeeded();
}

void WebRtcSession::onNegotiationNeeded() {
    // Only the caller initiates the offer; the callee builds its answer
    // in response to set-remote-description.
    if (!m_isCaller) return;
    // One offer per session. webrtcbin re-raises on-negotiation-needed for
    // every transceiver/data-channel it picks up while going to PLAYING;
    // without this guard we sent two offers, the peer answered both, and
    // the second answer bounced off webrtcbin with "Not in the correct
    // state (stable)". Renegotiation doesn't exist here — hangup() tears
    // the pipeline down and the next call starts fresh.
    if (m_offerCreated) return;
    m_offerCreated = true;
    GstPromise *promise = gst_promise_new_with_change_func(
        +[](GstPromise *p, gpointer s) {
            static_cast<WebRtcSession *>(s)->onOfferCreated(p);
        }, this, nullptr);
    g_signal_emit_by_name(m_webrtc, "create-offer", nullptr, promise);
}

void WebRtcSession::onOfferCreatedCb(void *p, gpointer s) {
    static_cast<WebRtcSession *>(s)->onOfferCreated(p);
}
void WebRtcSession::onAnswerCreatedCb(void *p, gpointer s) {
    static_cast<WebRtcSession *>(s)->onAnswerCreated(p);
}

void WebRtcSession::onOfferCreated(void *p) {
    GstPromise *promise = static_cast<GstPromise *>(p);
    const GstStructure *reply = gst_promise_get_reply(promise);
    GstWebRTCSessionDescription *offer = nullptr;
    gst_structure_get(reply, "offer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &offer, nullptr);
    gst_promise_unref(promise);
    if (!offer) return;

    GstPromise *setLocal = gst_promise_new();
    g_signal_emit_by_name(m_webrtc, "set-local-description", offer, setLocal);
    gst_promise_interrupt(setLocal);
    gst_promise_unref(setLocal);

    gchar *sdpText = gst_sdp_message_as_text(offer->sdp);
    emit localOfferReady(m_peerId, QString::fromUtf8(sdpText));
    g_free(sdpText);
    gst_webrtc_session_description_free(offer);
}

void WebRtcSession::onAnswerCreated(void *p) {
    GstPromise *promise = static_cast<GstPromise *>(p);
    const GstStructure *reply = gst_promise_get_reply(promise);
    GstWebRTCSessionDescription *answer = nullptr;
    gst_structure_get(reply, "answer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &answer, nullptr);
    gst_promise_unref(promise);
    if (!answer) return;

    GstPromise *setLocal = gst_promise_new();
    g_signal_emit_by_name(m_webrtc, "set-local-description", answer, setLocal);
    gst_promise_interrupt(setLocal);
    gst_promise_unref(setLocal);

    gchar *sdpText = gst_sdp_message_as_text(answer->sdp);
    emit localAnswerReady(m_peerId, QString::fromUtf8(sdpText));
    g_free(sdpText);
    gst_webrtc_session_description_free(answer);
}

void WebRtcSession::onIceCandidateCb(GstElement *, guint mlineIndex,
                                      const gchar *candidate, gpointer self) {
    static_cast<WebRtcSession *>(self)->onIceCandidate(mlineIndex,
                                                        QString::fromUtf8(candidate));
}

void WebRtcSession::onIceCandidate(unsigned mlineIndex, const QString &candidate) {
    // We don't have the mid here; emit empty and let the peer ignore it.
    emit localIceReady(m_peerId, candidate, QString(), static_cast<int>(mlineIndex));
}

void WebRtcSession::onIncomingStreamCb(GstElement *, void *pad, gpointer self) {
    static_cast<WebRtcSession *>(self)->onIncomingStream(pad);
}

void WebRtcSession::onIncomingStream(void *padPtr) {
    GstPad *pad = static_cast<GstPad *>(padPtr);
    if (GST_PAD_DIRECTION(pad) != GST_PAD_SRC) return;

    // Drop the pad into decodebin and let it sort encoding-specific bits;
    // decodebin then fires its own pad-added when raw audio/video appears,
    // which we route to the appropriate sink.
    GstElement *decodebin = gst_element_factory_make("decodebin", nullptr);
    g_signal_connect(decodebin, "pad-added",
        G_CALLBACK(+[](GstElement *, GstPad *newpad, gpointer s) {
            WebRtcSession *self = static_cast<WebRtcSession *>(s);
            GstCaps *caps = gst_pad_get_current_caps(newpad);
            const GstStructure *str = gst_caps_get_structure(caps, 0);
            const gchar *name = gst_structure_get_name(str);
            GstElement *sink = nullptr;
            if (g_str_has_prefix(name, "video/")) {
                // Remote video also goes through appsink → QImage → QPainter.
                // No native video surface at all: nothing to fight Qt over
                // stacking, no GL/D3D display requirements (the grandfather's
                // GPU already failed glimagesink once) — at 480p the copy is
                // cheap. One rendering path on every platform.
                GstElement *conv  = gst_element_factory_make("videoconvert", nullptr);
                GstElement *asink = gst_element_factory_make("appsink", nullptr);
                if (!conv || !asink) { gst_caps_unref(caps); return; }
                GstCaps *vcaps = gst_caps_from_string("video/x-raw,format=RGBA");
                g_object_set(asink, "caps", vcaps, "max-buffers", 2, "drop", TRUE,
                             "emit-signals", TRUE, nullptr);
                gst_caps_unref(vcaps);
                g_signal_connect(asink, "new-sample",
                    G_CALLBACK(+[](GstElement *sk, gpointer s) -> GstFlowReturn {
                        auto *ss = static_cast<WebRtcSession *>(s);
                        GstSample *sample = nullptr;
                        g_signal_emit_by_name(sk, "pull-sample", &sample);
                        const QImage img = sampleToImage(sample);
                        if (sample) gst_sample_unref(sample);
                        if (!img.isNull()) emit ss->remoteFrame(img);
                        return GST_FLOW_OK;
                    }), self);
                gst_bin_add_many(GST_BIN(self->m_pipeline), conv, asink, nullptr);
                gst_element_link(conv, asink);
                gst_element_sync_state_with_parent(conv);
                gst_element_sync_state_with_parent(asink);
                gst_caps_unref(caps);
                GstPad *sinkpad = gst_element_get_static_pad(conv, "sink");
                gst_pad_link(newpad, sinkpad);
                gst_object_unref(sinkpad);
                return;
            } else if (g_str_has_prefix(name, "audio/")) {
                GstElement *conv = gst_element_factory_make("audioconvert", nullptr);
                GstElement *res  = gst_element_factory_make("audioresample", nullptr);
                if (!conv || !res) return;
                gst_bin_add_many(GST_BIN(self->m_pipeline), conv, res, nullptr);
                gst_element_link(conv, res);

                // With AEC the playback chain (mixer → echo probe → sink)
                // already runs; feed the remote audio into the mixer so the
                // probe sees exactly what reaches the speakers.
                GstElement *amix = self->m_haveAec
                    ? gst_bin_get_by_name(GST_BIN(self->m_pipeline), "amix")
                    : nullptr;
                if (amix) {
                    // The mixer runs locked to S16/48k/mono — bring the
                    // remote audio to exactly that before its pad.
                    GstElement *cf = gst_element_factory_make("capsfilter", nullptr);
                    GstCaps *mixCaps = gst_caps_from_string(
                        "audio/x-raw,format=S16LE,rate=48000,channels=1");
                    g_object_set(cf, "caps", mixCaps, nullptr);
                    gst_caps_unref(mixCaps);
                    gst_bin_add(GST_BIN(self->m_pipeline), cf);
                    gst_element_link(res, cf);
                    gst_element_sync_state_with_parent(cf);
                    GstPad *mixPad = gst_element_request_pad_simple(amix, "sink_%u");
                    GstPad *cfSrc  = gst_element_get_static_pad(cf, "src");
                    gst_pad_link(cfSrc, mixPad);
                    gst_object_unref(cfSrc);
                    gst_object_unref(mixPad);
                    gst_object_unref(amix);
                } else {
                    GstElement *out = gst_element_factory_make("autoaudiosink", nullptr);
                    if (!out) return;
                    gst_bin_add(GST_BIN(self->m_pipeline), out);
                    gst_element_link(res, out);
                    gst_element_sync_state_with_parent(out);
                }
                gst_element_sync_state_with_parent(conv);
                gst_element_sync_state_with_parent(res);
                gst_caps_unref(caps);
                GstPad *sinkpad = gst_element_get_static_pad(conv, "sink");
                gst_pad_link(newpad, sinkpad);
                gst_object_unref(sinkpad);
                return;
            }
            gst_caps_unref(caps);
            if (!sink) return;
            gst_bin_add(GST_BIN(self->m_pipeline), sink);
            gst_element_sync_state_with_parent(sink);
            GstPad *sinkpad = gst_element_get_static_pad(sink, "sink");
            gst_pad_link(newpad, sinkpad);
            gst_object_unref(sinkpad);
        }), this);

    gst_bin_add(GST_BIN(m_pipeline), decodebin);
    gst_element_sync_state_with_parent(decodebin);
    GstPad *sinkpad = gst_element_get_static_pad(decodebin, "sink");
    gst_pad_link(pad, sinkpad);
    gst_object_unref(sinkpad);
}

void WebRtcSession::onDataChannelCb(GstElement *, void *channel, gpointer self) {
    static_cast<WebRtcSession *>(self)->attachDataChannel(channel);
}

void WebRtcSession::attachDataChannel(void *channel) {
    m_dataChannel = channel;
    g_signal_connect(channel, "on-message-string",
        G_CALLBACK(+[](void *, const gchar *text, gpointer s) {
            static_cast<WebRtcSession *>(s)->onChannelMessage(QString::fromUtf8(text));
        }), this);
    g_signal_connect(channel, "on-open",
        G_CALLBACK(+[](void *, gpointer s) {
            auto *self = static_cast<WebRtcSession *>(s);
            self->m_channelOpen = true;
            emit self->channelOpen();
            emit self->callConnected();
        }), this);
    g_signal_connect(channel, "on-close",
        G_CALLBACK(+[](void *, gpointer s) {
            static_cast<WebRtcSession *>(s)->m_channelOpen = false;
        }), this);
}

// --- Bus pump -----------------------------------------------------------

void WebRtcSession::pollBus() {
    if (!m_pipeline) return;
    GstBus *bus = gst_element_get_bus(m_pipeline);
    while (true) {
        GstMessage *msg = gst_bus_pop(bus);
        if (!msg) break;
        switch (GST_MESSAGE_TYPE(msg)) {
            case GST_MESSAGE_ERROR: {
                GError *err = nullptr;
                gchar *dbg = nullptr;
                gst_message_parse_error(msg, &err, &dbg);
                // The debug detail names the failing element — keep it in
                // the log even though the UI only shows the short message.
                qWarning() << "[gst] ERROR from"
                           << (GST_MESSAGE_SRC(msg) ? GST_OBJECT_NAME(GST_MESSAGE_SRC(msg)) : "?")
                           << ":" << (err ? err->message : "?")
                           << "|" << (dbg ? dbg : "");
                emit error(QString::fromUtf8(err ? err->message : "gst error"));
                if (err) g_error_free(err);
                g_free(dbg);
                break;
            }
            case GST_MESSAGE_EOS:
                emit callEnded();
                break;
            case GST_MESSAGE_LATENCY:
                // webrtcbin/rtpbin post this when ICE completes or an
                // incoming branch appears. Without redistributing latency
                // the RTP session never learns its running time and stops
                // generating RTCP sender reports ("generated empty RTCP
                // messages" in the logs) — audio/video still flow but
                // lip-sync between them is never established.
                gst_bin_recalculate_latency(GST_BIN(m_pipeline));
                break;
            default: break;
        }
        gst_message_unref(msg);
    }
    gst_object_unref(bus);
}
