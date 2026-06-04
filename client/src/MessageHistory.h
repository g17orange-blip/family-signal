#pragma once

#include "HistoryCipher.h"
#include "Message.h"

#include <QObject>
#include <QString>
#include <QVariant>
#include <QVector>
#include <QtSql/QSqlDatabase>

// Local-only message store backed by SQLite. The signaling server never
// sees these rows — messages travel over the WebRTC DataChannel directly
// between peers, and each peer keeps its own copy of the conversation.
//
// Stored at QStandardPaths::AppDataLocation/history.db. Message text is
// encrypted at rest (see HistoryCipher); pre-encryption databases are
// migrated transparently on open().
class MessageHistory : public QObject {
    Q_OBJECT
public:
    explicit MessageHistory(QObject *parent = nullptr);
    ~MessageHistory() override;

    bool open();
    QString errorString() const { return m_error; }

    // Persists the message and updates msg.rowId on success.
    bool append(Message &msg);

    // Marks an outgoing message as acknowledged by the remote peer.
    bool markDelivered(qint64 rowId);

    // Returns up to `limit` most recent messages for a conversation, oldest first.
    QVector<Message> loadConversation(const QString &peerId, int limit = 500) const;

    // Irreversibly deletes every conversation (used by the settings screen).
    bool clearAll();

private:
    // One-time plaintext→encrypted rewrite, tracked via PRAGMA user_version.
    bool migrateToEncrypted();
    QString decryptText(const QVariant &stored) const;

    QSqlDatabase  m_db;
    QString       m_error;
    HistoryCipher m_cipher;
};
