#pragma once

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
    // Loads (or creates with defaults) from the platform-specific path.
    // Returns true on success; on failure errorString() explains why.
    bool load();
    bool save() const;

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
    QString m_error;
};
