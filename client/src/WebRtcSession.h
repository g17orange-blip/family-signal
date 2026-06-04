#pragma once

#include "Config.h"

#include <QObject>
#include <QString>
#include <QTimer>

// Forward-declare the few GStreamer types we expose so callers don't need
// to include the entire GStreamer headers. GLib types are pulled in
// directly because the static callbacks below take `gpointer`/`gchar*`
// and we need the typedefs to match GStreamer's signal signatures exactly.
#include <glib.h>

struct _GstElement;
typedef struct _GstElement GstElement;

// One peer-to-peer voice/video/data session. Lifetime:
//   1. Construct, attach to a video widget via setVideoWindowHandle().
//   2. Call start() to spin up the GStreamer pipeline (camera, mic, encoders,
//      webrtcbin). Pipeline stays in READY state until a call is initiated
//      or accepted.
//   3. To start an outbound call: startCall(peerId)
//      To accept an inbound call: acceptOffer(remoteSdp, peerId)
//   4. On end: hangup()
//
// Text messages travel over a DataChannel that is created as part of the
// initial offer (so it exists for both sides as soon as ICE completes).
//
// Chat mode (Mode::Chat): the pipeline is just a webrtcbin — no camera, no
// microphone, no media m-lines in the SDP. Used for the silent background
// connections that deliver queued text whenever both peers are online.
class WebRtcSession : public QObject {
    Q_OBJECT
public:
    enum class Mode { Call, Chat };

    explicit WebRtcSession(QObject *parent = nullptr, Mode mode = Mode::Call);
    ~WebRtcSession() override;

    void setConfig(const Config &cfg);

    // Pass the platform window handle of the QWidget that should display
    // the remote video. Must be called before remote video arrives.
    void setVideoWindowHandle(quintptr handle);
    // Window handle for the local camera preview (0 = no self-view).
    // Must be set before start() — the preview branch is built with the
    // pipeline.
    void setSelfViewHandle(quintptr handle);

    // Choose whether the next call carries camera video. Call before
    // start(); switching modes tears the previous pipeline down so the
    // camera is never opened for an audio-only call.
    void prepare(bool withVideo);

    // Human-readable capture diagnostics: which video/audio source elements
    // exist and whether a short live test of each succeeds. Shown from the
    // settings dialog so problems can be reported without log spelunking.
    static QString mediaDiagnostics();

    bool start();   // builds the pipeline, returns false on construction failure
    void stop();

    // Caller flow.
    void startCall(const QString &peerId);
    void acceptOffer(const QString &peerId, const QString &remoteSdp);
    void provideAnswer(const QString &remoteSdp);
    void addRemoteIce(const QString &candidate, int sdpMLineIndex);
    void hangup();

    // Send a chat message over the DataChannel. `msgId` identifies the
    // message for the delivery ack (textDelivered fires when the peer
    // confirms receipt). Returns false if the channel isn't open yet —
    // the message then stays queued in the history DB.
    bool sendText(const QString &msgId, const QString &text);

    // Tell the peer these of their messages were displayed (read receipts).
    bool sendReadReceipts(const QStringList &msgIds);

    bool isInCall() const { return m_inCall; }
    bool isChannelOpen() const { return m_channelOpen; }
    QString remotePeerId() const { return m_peerId; }

signals:
    // Local SDP ready to be sent to the peer via signaling.
    void localOfferReady(const QString &peerId, const QString &sdp);
    void localAnswerReady(const QString &peerId, const QString &sdp);
    // Local ICE candidate gathered.
    void localIceReady(const QString &peerId, const QString &candidate,
                       const QString &sdpMid, int sdpMLineIndex);

    void callConnected();
    void callEnded();
    void error(const QString &message);

    // Inbound chat message from the remote peer over the DataChannel.
    // msgId is empty for legacy plain-string messages.
    void textReceived(const QString &fromPeerId, const QString &msgId,
                      const QString &text);
    // The remote peer acknowledged receiving this message.
    void textDelivered(const QString &msgId);
    // The remote peer displayed these (our outgoing) messages.
    void peerReadMessages(const QStringList &msgIds);
    // DataChannel is open — queued messages can be flushed now.
    void channelOpen();

private:
    // Bus poll on Qt thread — see comment in WebRtcSession.cpp.
    void pollBus();

    // Static C-style callbacks GStreamer calls; each forwards to the
    // member of the same name on the right instance.
    static void onNegotiationNeededCb(GstElement *webrtc, gpointer self);
    static void onIceCandidateCb(GstElement *webrtc, guint mlineIndex,
                                  const gchar *candidate, gpointer self);
    static void onIncomingStreamCb(GstElement *webrtc, void *pad, gpointer self);
    static void onDataChannelCb(GstElement *webrtc, void *channel, gpointer self);
    static void onOfferCreatedCb(void *promise, gpointer self);
    static void onAnswerCreatedCb(void *promise, gpointer self);

    void onNegotiationNeeded();
    void onIceCandidate(unsigned mlineIndex, const QString &candidate);
    void onOfferCreated(void *promise);
    void onAnswerCreated(void *promise);
    void onIncomingStream(void *pad);
    void attachDataChannel(void *channel);

    void buildPipelineIfNeeded();
    void attachWebrtcSignals();
    void setRemoteDescription(const QString &type, const QString &sdp);

    void onChannelMessage(const QString &raw);

    Config m_config;
    Mode   m_mode = Mode::Call;
    bool   m_withVideo = true;  // Call mode: include the camera branch
    bool   m_channelOpen = false;
    bool   m_haveAec = false;   // webrtcdsp echo cancellation in the pipeline
    quintptr m_videoHandle = 0;
    quintptr m_selfViewHandle = 0;

    GstElement *m_pipeline = nullptr;
    GstElement *m_webrtc   = nullptr;
    void       *m_dataChannel = nullptr; // GstWebRTCDataChannel*

    QString m_peerId;
    bool    m_inCall = false;
    bool    m_isCaller = false;

    QTimer m_busTimer;
};
