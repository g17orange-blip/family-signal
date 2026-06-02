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

#include <QDebug>
#include <QPointer>

extern "C" {
#include <gst/gst.h>
#include <gst/sdp/sdp.h>
#include <gst/video/videooverlay.h>
#define GST_USE_UNSTABLE_API
#include <gst/webrtc/webrtc.h>
}

namespace {

// ---- one-shot GStreamer init -------------------------------------------
void ensureGstInit() {
    static bool inited = false;
    if (!inited) {
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

} // namespace

WebRtcSession::WebRtcSession(QObject *parent) : QObject(parent) {
    ensureGstInit();
    m_busTimer.setInterval(30);
    connect(&m_busTimer, &QTimer::timeout, this, &WebRtcSession::pollBus);
}

WebRtcSession::~WebRtcSession() {
    stop();
}

void WebRtcSession::setConfig(const Config &cfg) { m_config = cfg; }
void WebRtcSession::setVideoWindowHandle(quintptr handle) { m_videoHandle = handle; }

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
    const QByteArray launch =
        "webrtcbin name=webrtc bundle-policy=max-bundle "
        "  stun-server=" + m_config.stunUrl.toUtf8() + " "
        "autovideosrc ! videoconvert ! videoscale ! "
        "  video/x-raw,width=640,height=480,framerate=30/1 ! "
        "  queue max-size-buffers=10 leaky=downstream ! "
        "  x264enc tune=zerolatency speed-preset=ultrafast bitrate=600 key-int-max=30 ! "
        "  video/x-h264,profile=constrained-baseline ! "
        "  rtph264pay config-interval=1 pt=96 ! "
        "  application/x-rtp,media=video,encoding-name=H264,payload=96 ! webrtc. "
        "autoaudiosrc ! audioconvert ! audioresample ! "
        "  queue max-size-buffers=10 leaky=downstream ! "
        "  opusenc bitrate=32000 ! rtpopuspay pt=111 ! "
        "  application/x-rtp,media=audio,encoding-name=OPUS,payload=111 ! webrtc.";

    GError *err = nullptr;
    m_pipeline = gst_parse_launch(launch.constData(), &err);
    if (!m_pipeline) {
        emit error(QStringLiteral("gst_parse_launch: %1").arg(err ? err->message : "unknown"));
        if (err) g_error_free(err);
        return;
    }
    m_webrtc = gst_bin_get_by_name(GST_BIN(m_pipeline), "webrtc");

    // TURN server, if configured. webrtcbin accepts a turn:// URI of the
    // form turn://user:pass@host:port — note that any '@' or ':' in the
    // password must be percent-encoded, but for our two-user prototype we
    // generate the password ourselves and avoid such characters.
    if (!m_config.turn.url.isEmpty() && !m_config.turn.username.isEmpty()) {
        const QString turnUri = QStringLiteral("turn://%1:%2@%3")
            .arg(m_config.turn.username, m_config.turn.password,
                 QString(m_config.turn.url).remove(QStringLiteral("turn:")));
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
    // The actual create-offer happens automatically when webrtcbin fires
    // on-negotiation-needed, so we don't kick it off here.
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
    if (m_pipeline) gst_element_set_state(m_pipeline, GST_STATE_READY);
    m_inCall = false;
    m_dataChannel = nullptr;
    emit callEnded();
}

bool WebRtcSession::sendText(const QString &text) {
    if (!m_dataChannel) return false;
    g_signal_emit_by_name(m_dataChannel, "send-string", text.toUtf8().constData());
    emit textDelivered(text);
    return true;
}

// --- GStreamer callbacks ------------------------------------------------

void WebRtcSession::onNegotiationNeededCb(GstElement *, gpointer self) {
    static_cast<WebRtcSession *>(self)->onNegotiationNeeded();
}

void WebRtcSession::onNegotiationNeeded() {
    // Only the caller initiates the offer; the callee builds its answer
    // in response to set-remote-description.
    if (!m_isCaller) return;
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
                // glimagesink works on Linux/macOS/Windows and supports
                // GstVideoOverlay so we can embed into a Qt widget.
                sink = gst_element_factory_make("glimagesink", nullptr);
                if (sink && self->m_videoHandle) {
                    gst_video_overlay_set_window_handle(
                        GST_VIDEO_OVERLAY(sink),
                        static_cast<guintptr>(self->m_videoHandle));
                }
            } else if (g_str_has_prefix(name, "audio/")) {
                sink = gst_element_factory_make("autoaudiosink", nullptr);
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
            auto *self = static_cast<WebRtcSession *>(s);
            emit self->textReceived(self->m_peerId, QString::fromUtf8(text));
        }), this);
    g_signal_connect(channel, "on-open",
        G_CALLBACK(+[](void *, gpointer s) {
            emit static_cast<WebRtcSession *>(s)->callConnected();
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
                emit error(QString::fromUtf8(err ? err->message : "gst error"));
                if (err) g_error_free(err);
                g_free(dbg);
                break;
            }
            case GST_MESSAGE_EOS:
                emit callEnded();
                break;
            default: break;
        }
        gst_message_unref(msg);
    }
    gst_object_unref(bus);
}
