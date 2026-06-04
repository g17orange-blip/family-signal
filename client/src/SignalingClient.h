#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>

class QWebSocket;

// Thin wrapper around QWebSocket that speaks the JSON protocol of the
// signaling server (see server/signaling/main.go).
//
// Emits Qt signals for every inbound message kind so the rest of the
// client never touches WebSockets or JSON directly.
class SignalingClient : public QObject {
    Q_OBJECT
public:
    explicit SignalingClient(QObject *parent = nullptr);
    ~SignalingClient() override;

    void connectTo(const QUrl &url, const QString &userId, const QString &token);
    void disconnect();
    bool isConnected() const;

    // Send WebRTC handshake messages destined for a specific peer.
    // `kind` distinguishes parallel sessions to the same peer: "call" is a
    // ringing audio/video call, "chat" is the silent text-delivery session.
    // The server relays payloads verbatim, so no server change is needed;
    // older clients simply ignore the extra field.
    void sendOffer(const QString &toPeerId, const QString &sdp,
                   const QString &kind = QStringLiteral("call"));
    void sendAnswer(const QString &toPeerId, const QString &sdp,
                    const QString &kind = QStringLiteral("call"));
    void sendIce(const QString &toPeerId, const QString &candidate,
                 const QString &sdpMid, int sdpMLineIndex,
                 const QString &kind = QStringLiteral("call"));
    void sendBye(const QString &toPeerId);

signals:
    void connected();
    void disconnected();
    void errorOccurred(const QString &reason);

    void presenceChanged(const QStringList &onlinePeerIds);

    // `kind` is "call" (default when absent) or "chat" — see sendOffer.
    void offerReceived(const QString &fromPeerId, const QString &sdp,
                       const QString &kind);
    void answerReceived(const QString &fromPeerId, const QString &sdp,
                        const QString &kind);
    void iceReceived(const QString &fromPeerId, const QString &candidate,
                     const QString &sdpMid, int sdpMLineIndex,
                     const QString &kind);
    void byeReceived(const QString &fromPeerId);

private slots:
    void onConnected();
    void onDisconnected();
    void onTextMessage(const QString &message);
    void onError();
    void tryReconnect();

private:
    void sendEnvelope(const QString &type, const QString &to,
                      const QByteArray &payloadJson);

    QWebSocket *m_socket = nullptr;
    QUrl        m_url;
    QString     m_userId;
    QString     m_token;
    QTimer      m_reconnectTimer;
    bool        m_helloSent = false;
};
