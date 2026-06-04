#pragma once

#include <QByteArray>
#include <QString>

// At-rest encryption for the local message history (XChaCha20-Poly1305 via
// the vendored Monocypher). A random 256-bit key is generated on first use
// and stored next to history.db:
//   - file permissions 0600 everywhere;
//   - on Windows the key is additionally wrapped with DPAPI
//     (CryptProtectData), so it is tied to the OS user account.
//
// This protects the conversation from offline file theft (a copied
// history.db is unreadable without the key file / user account). It does
// NOT protect against malware running as the same user — nothing local can.
class HistoryCipher {
public:
    // Loads the key from disk, creating it on first run. Must be called
    // before encrypt/decrypt. Returns false (with errorString set) if the
    // key can neither be read nor created.
    bool init(const QString &dir);

    QString errorString() const { return m_error; }

    // nonce(24) || mac(16) || ciphertext. Empty on failure.
    QByteArray encrypt(const QString &plainText) const;

    // Empty string if the blob is corrupt or keyed differently.
    QString decrypt(const QByteArray &blob) const;

    // True if `blob` looks like one of our encrypted records (used by the
    // plaintext→encrypted migration to stay idempotent).
    static bool looksEncrypted(const QByteArray &blob);

private:
    QByteArray m_key;     // 32 bytes
    QString    m_error;
};
