#pragma once

#include <QJsonObject>
#include <QString>
#include <QUrl>
#include <QVector>

// Per-user client configuration, loaded from a JSON file at startup.
// On Linux:   ~/.config/signal/config.json
// On macOS:   ~/Library/Application Support/signal/config.json
// On Windows: %APPDATA%/signal/config.json
//
// The file is created with sane defaults on first launch if missing.
struct PeerConfig {
    QString id;          // remote user_id this client will talk to
    QString displayName; // shown in contacts list
};

struct TurnConfig {
    QString url;      // turn:host:3478?transport=udp
    QString username;
    QString password;
};

class Config {
public:
    // Loads from the platform-specific path. Returns true on success; on
    // failure errorString() explains why. Use exists() to distinguish a
    // missing file (→ run the first-run wizard) from a malformed one.
    bool load();
    bool save() const;
    bool exists() const;

    // Invite code: base64(config.json). Produced by server/install.sh, pasted
    // into the first-run wizard. applyInvite fills this Config from the code
    // (returns false + errorString on malformed input); encodeInvite is the
    // inverse, handy for sharing.
    bool applyInvite(const QString &code);
    QString encodeInvite() const;

    QString configPath() const;
    QString errorString() const { return m_error; }

    // Local identity.
    QString userId;
    QString displayName;

    // Signaling.
    QUrl    signalingUrl;   // wss://your-vps:8080/ws
    QString signalingToken; // shared secret with the server

    // Peers we want to chat with. For two users (you + grandfather) this
    // list has exactly one entry; the model is kept open for future growth.
    QVector<PeerConfig> peers;

    // ICE servers — STUN is always usable, TURN only kicks in when needed.
    QString stunUrl;       // stun:stun.l.google.com:19302 by default
    TurnConfig turn;       // empty fields → TURN disabled

private:
    // Shared JSON (de)serialization used by load/save and the invite codec.
    QJsonObject toJsonObject() const;
    bool fromJsonObject(const QJsonObject &root);

    mutable QString m_error;
};
