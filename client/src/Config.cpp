#include "Config.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

QString Config::configPath() const {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return QDir(dir).filePath(QStringLiteral("config.json"));
}

bool Config::exists() const {
    return QFile::exists(configPath());
}

bool Config::load() {
    const QString path = configPath();

    QFile f(path);
    if (!f.exists()) {
        // First launch is handled by the setup wizard (see FirstRunDialog),
        // which fills this Config and calls save(). Reaching load() with no
        // file is therefore an error, not a seeding opportunity.
        m_error = QStringLiteral("config not found at %1").arg(path);
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
    if (!fromJsonObject(doc.object())) {
        m_error = QStringLiteral("%1 is missing user_id or signaling_url").arg(path);
        return false;
    }
    return true;
}

bool Config::save() const {
    const QString path = configPath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_error = QStringLiteral("cannot write %1: %2").arg(path, f.errorString());
        return false;
    }
    f.write(QJsonDocument(toJsonObject()).toJson(QJsonDocument::Indented));
    return true;
}

bool Config::applyInvite(const QString &code) {
    const QByteArray json = QByteArray::fromBase64(code.trimmed().toUtf8(),
                                                   QByteArray::AbortOnBase64DecodingErrors);
    if (json.isEmpty()) {
        m_error = QStringLiteral("код приглашения повреждён (не base64)");
        return false;
    }
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        m_error = QStringLiteral("код приглашения повреждён: %1").arg(err.errorString());
        return false;
    }
    if (!fromJsonObject(doc.object())) {
        m_error = QStringLiteral("в коде приглашения нет user_id или signaling_url");
        return false;
    }
    return true;
}

QString Config::encodeInvite() const {
    return QString::fromLatin1(
        QJsonDocument(toJsonObject()).toJson(QJsonDocument::Compact).toBase64());
}

bool Config::fromJsonObject(const QJsonObject &root) {
    userId         = root.value(QStringLiteral("user_id")).toString();
    displayName    = root.value(QStringLiteral("display_name")).toString();
    signalingUrl   = QUrl(root.value(QStringLiteral("signaling_url")).toString());
    signalingToken = root.value(QStringLiteral("signaling_token")).toString();
    stunUrl        = root.value(QStringLiteral("stun_url")).toString(
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

    return !userId.isEmpty() && signalingUrl.isValid() && !signalingUrl.isEmpty();
}

QJsonObject Config::toJsonObject() const {
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
    return root;
}
