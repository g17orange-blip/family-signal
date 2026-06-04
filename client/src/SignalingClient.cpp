#include "SignalingClient.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QWebSocket>

SignalingClient::SignalingClient(QObject *parent) : QObject(parent) {
    m_socket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    connect(m_socket, &QWebSocket::connected,       this, &SignalingClient::onConnected);
    connect(m_socket, &QWebSocket::disconnected,    this, &SignalingClient::onDisconnected);
    connect(m_socket, &QWebSocket::textMessageReceived, this, &SignalingClient::onTextMessage);
    connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::errorOccurred),
            this, &SignalingClient::onError);

    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &SignalingClient::tryReconnect);
}

SignalingClient::~SignalingClient() = default;

void SignalingClient::connectTo(const QUrl &url, const QString &userId, const QString &token) {
    m_url = url;
    m_userId = userId;
    m_token = token;
    m_socket->open(url);
}

void SignalingClient::disconnect() {
    m_reconnectTimer.stop();
    m_socket->close();
}

bool SignalingClient::isConnected() const {
    return m_socket->state() == QAbstractSocket::ConnectedState && m_helloSent;
}

void SignalingClient::onConnected() {
    QJsonObject payload;
    payload.insert(QStringLiteral("user_id"), m_userId);
    payload.insert(QStringLiteral("token"),   m_token);
    sendEnvelope(QStringLiteral("hello"), QString(),
                 QJsonDocument(payload).toJson(QJsonDocument::Compact));
    m_helloSent = true;
    emit connected();
}

void SignalingClient::onDisconnected() {
    m_helloSent = false;
    emit disconnected();
    // Reconnect with backoff; the messenger should keep trying as long as
    // the user is logged in. Five seconds is conservative for human-scale
    // outages and won't hammer the server during a real outage.
    if (m_url.isValid()) m_reconnectTimer.start(5000);
}

void SignalingClient::onError() {
    emit errorOccurred(m_socket->errorString());
}

void SignalingClient::tryReconnect() {
    if (m_socket->state() == QAbstractSocket::UnconnectedState) {
        m_socket->open(m_url);
    }
}

void SignalingClient::onTextMessage(const QString &message) {
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(message.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return;
    const auto env = doc.object();
    const auto type = env.value(QStringLiteral("type")).toString();
    const auto from = env.value(QStringLiteral("from")).toString();
    const auto payload = env.value(QStringLiteral("payload")).toObject();

    if (type == QStringLiteral("presence")) {
        QStringList online;
        for (const auto v : payload.value(QStringLiteral("online")).toArray()) {
            online.append(v.toString());
        }
        emit presenceChanged(online);
    } else if (type == QStringLiteral("offer")) {
        emit offerReceived(from, payload.value(QStringLiteral("sdp")).toString(),
                           payload.value(QStringLiteral("kind"))
                               .toString(QStringLiteral("call")));
    } else if (type == QStringLiteral("answer")) {
        emit answerReceived(from, payload.value(QStringLiteral("sdp")).toString(),
                            payload.value(QStringLiteral("kind"))
                                .toString(QStringLiteral("call")));
    } else if (type == QStringLiteral("ice")) {
        emit iceReceived(from,
                         payload.value(QStringLiteral("candidate")).toString(),
                         payload.value(QStringLiteral("sdpMid")).toString(),
                         payload.value(QStringLiteral("sdpMLineIndex")).toInt(),
                         payload.value(QStringLiteral("kind"))
                             .toString(QStringLiteral("call")));
    } else if (type == QStringLiteral("bye")) {
        emit byeReceived(from);
    } else if (type == QStringLiteral("error")) {
        emit errorOccurred(payload.value(QStringLiteral("reason")).toString());
    }
}

void SignalingClient::sendEnvelope(const QString &type, const QString &to,
                                    const QByteArray &payloadJson) {
    QJsonObject env;
    env.insert(QStringLiteral("type"), type);
    if (!to.isEmpty()) env.insert(QStringLiteral("to"), to);
    if (!payloadJson.isEmpty()) {
        env.insert(QStringLiteral("payload"),
                   QJsonDocument::fromJson(payloadJson).object());
    }
    m_socket->sendTextMessage(QString::fromUtf8(QJsonDocument(env).toJson(QJsonDocument::Compact)));
}

void SignalingClient::sendOffer(const QString &toPeerId, const QString &sdp,
                                 const QString &kind) {
    QJsonObject p;
    p.insert(QStringLiteral("sdp"), sdp);
    p.insert(QStringLiteral("kind"), kind);
    sendEnvelope(QStringLiteral("offer"), toPeerId,
                 QJsonDocument(p).toJson(QJsonDocument::Compact));
}

void SignalingClient::sendAnswer(const QString &toPeerId, const QString &sdp,
                                  const QString &kind) {
    QJsonObject p;
    p.insert(QStringLiteral("sdp"), sdp);
    p.insert(QStringLiteral("kind"), kind);
    sendEnvelope(QStringLiteral("answer"), toPeerId,
                 QJsonDocument(p).toJson(QJsonDocument::Compact));
}

void SignalingClient::sendIce(const QString &toPeerId, const QString &candidate,
                               const QString &sdpMid, int sdpMLineIndex,
                               const QString &kind) {
    QJsonObject p;
    p.insert(QStringLiteral("candidate"), candidate);
    p.insert(QStringLiteral("sdpMid"), sdpMid);
    p.insert(QStringLiteral("sdpMLineIndex"), sdpMLineIndex);
    p.insert(QStringLiteral("kind"), kind);
    sendEnvelope(QStringLiteral("ice"), toPeerId,
                 QJsonDocument(p).toJson(QJsonDocument::Compact));
}

void SignalingClient::sendBye(const QString &toPeerId) {
    sendEnvelope(QStringLiteral("bye"), toPeerId, QByteArray());
}
