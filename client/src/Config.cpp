#include "Config.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUuid>

QString Config::configPath() const {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return QDir(dir).filePath(QStringLiteral("config.json"));
}

bool Config::load() {
    const QString path = configPath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QFile f(path);
    if (!f.exists()) {
        // First launch: seed with placeholders so the user knows what to fill in.
        userId = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
        displayName = QStringLiteral("Me");
        signalingUrl = QUrl(QStringLiteral("ws://localhost:8080/ws"));
        signalingToken = QStringLiteral("CHANGE_ME");
        stunUrl = QStringLiteral("stun:stun.l.google.com:19302");
        peers.append({QStringLiteral("peer-id-here"), QStringLiteral("Дедушка")});
        if (!save()) return false;
        m_error = QStringLiteral("config seeded at %1 — edit it and relaunch").arg(path);
        return false;
    }

    if (!f.open(QIODevice::ReadOnly)) {
        m_error = QStringLiteral("cannot open %1: %2").arg(path, f.errorString());
        return false;
    }
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError) {
        m_error = QStringLiteral("%1: %2").arg(path, err.errorString());
        return false;
    }
    const auto root = doc.object();
    userId = root.value(QStringLiteral("user_id")).toString();
    displayName = root.value(QStringLiteral("display_name")).toString();
    signalingUrl = QUrl(root.value(QStringLiteral("signaling_url")).toString());
    signalingToken = root.value(QStringLiteral("signaling_token")).toString();
    stunUrl = root.value(QStringLiteral("stun_url")).toString(
        QStringLiteral("stun:stun.l.google.com:19302"));

    peers.clear();
    for (const auto v : root.value(QStringLiteral("peers")).toArray()) {
        const auto o = v.toObject();
        peers.append({o.value(QStringLiteral("id")).toString(),
                      o.value(QStringLiteral("name")).toString()});
    }

    const auto t = root.value(QStringLiteral("turn")).toObject();
    turn.url      = t.value(QStringLiteral("url")).toString();
    turn.username = t.value(QStringLiteral("username")).toString();
    turn.password = t.value(QStringLiteral("password")).toString();

    if (userId.isEmpty() || !signalingUrl.isValid()) {
        m_error = QStringLiteral("%1 is missing user_id or signaling_url").arg(path);
        return false;
    }
    return true;
}

bool Config::save() const {
    QJsonObject root;
    root.insert(QStringLiteral("user_id"), userId);
    root.insert(QStringLiteral("display_name"), displayName);
    root.insert(QStringLiteral("signaling_url"), signalingUrl.toString());
    root.insert(QStringLiteral("signaling_token"), signalingToken);
    root.insert(QStringLiteral("stun_url"), stunUrl);

    QJsonArray peerArr;
    for (const auto &p : peers) {
        QJsonObject o;
        o.insert(QStringLiteral("id"), p.id);
        o.insert(QStringLiteral("name"), p.displayName);
        peerArr.append(o);
    }
    root.insert(QStringLiteral("peers"), peerArr);

    QJsonObject t;
    t.insert(QStringLiteral("url"), turn.url);
    t.insert(QStringLiteral("username"), turn.username);
    t.insert(QStringLiteral("password"), turn.password);
    root.insert(QStringLiteral("turn"), t);

    QFile f(configPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}
