#pragma once

#include "HistoryCipher.h"
#include "Message.h"

#include <QObject>
#include <QString>
#include <QStringList>
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
    bool markDeliveredByMsgId(const QString &msgId);

    // --- Read receipts ------------------------------------------------------
    // Receiver side: the conversation with `peerId` is on screen — flag its
    // inbound messages as read and return every msg_id whose read receipt
    // still has to reach the sender (including ones from earlier sessions).
    QStringList markConversationRead(const QString &peerId, const QString &selfId);
    // Receiver side: receipts for these ids were sent successfully.
    bool markReceiptsSent(const QStringList &msgIds);
    // Receiver side: receipts read earlier but never sent (peer was offline).
    QStringList pendingReceipts(const QString &peerId, const QString &selfId) const;
    // Sender side: the peer displayed these outgoing messages.
    bool markPeerRead(const QStringList &msgIds);

    // Returns up to `limit` most recent messages for a conversation, oldest first.
    QVector<Message> loadConversation(const QString &peerId, int limit = 500) const;

    // Outbound messages that never got a delivery ack, oldest first —
    // the offline queue flushed when a DataChannel to the peer opens.
    QVector<Message> loadUndelivered(const QString &peerId, const QString &selfId) const;

    // True if a message with this id is already stored (receiver-side dedup:
    // the sender re-sends anything unacked, so duplicates are expected).
    bool containsMsgId(const QString &msgId) const;

    // Irreversibly deletes every conversation (used by the settings screen).
    bool clearAll();

private:
    // One-time plaintext→encrypted rewrite, tracked via PRAGMA user_version.
    bool migrateToEncrypted();
    // v2: add the msg_id column (delivery acks / dedup).
    bool migrateAddMsgId();
    // v3: add read / read_sent columns (read receipts).
    bool migrateAddRead();
    // v4: add the kind column (call-event system notes).
    bool migrateAddKind();
    QString decryptText(const QVariant &stored) const;

    QSqlDatabase  m_db;
    QString       m_error;
    HistoryCipher m_cipher;
};
