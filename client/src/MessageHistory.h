#pragma once

#include "Message.h"

#include <QObject>
#include <QString>
#include <QVector>
#include <QtSql/QSqlDatabase>

// Local-only message store backed by SQLite. The signaling server never
// sees these rows — messages travel over the WebRTC DataChannel directly
// between peers, and each peer keeps its own copy of the conversation.
//
// Stored at QStandardPaths::AppDataLocation/history.db.
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

private:
    QSqlDatabase m_db;
    QString      m_error;
};
