#pragma once

#include "Message.h"

#include <QAbstractListModel>
#include <QString>
#include <QVector>

// Per-conversation message list backing the chat view. Loaded from
// MessageHistory whenever the user switches contact; mutated on every
// inbound/outbound text. Knows the local userId so the view can paint
// outgoing vs. incoming bubbles without re-checking.
class ChatModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        TextRole = Qt::UserRole + 1,
        SenderIdRole,
        SentAtRole,
        OutgoingRole,
        DeliveredRole,
        ReadRole,
    };

    explicit ChatModel(QObject *parent = nullptr);

    void setSelfId(const QString &id) { m_selfId = id; }
    void setMessages(QVector<Message> messages);
    void append(const Message &msg);
    void markDelivered(qint64 rowId);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Message messageAt(int row) const;

private:
    QString          m_selfId;
    QVector<Message> m_messages;
};
