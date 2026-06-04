#include "ChatModel.h"

ChatModel::ChatModel(QObject *parent) : QAbstractListModel(parent) {}

void ChatModel::setMessages(QVector<Message> messages) {
    beginResetModel();
    m_messages = std::move(messages);
    endResetModel();
}

void ChatModel::append(const Message &msg) {
    beginInsertRows(QModelIndex(), m_messages.size(), m_messages.size());
    m_messages.append(msg);
    endInsertRows();
}

void ChatModel::markDelivered(qint64 rowId) {
    for (int i = 0; i < m_messages.size(); ++i) {
        if (m_messages[i].rowId == rowId) {
            m_messages[i].delivered = true;
            const auto idx = index(i);
            emit dataChanged(idx, idx, {DeliveredRole});
            return;
        }
    }
}

int ChatModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_messages.size();
}

QVariant ChatModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_messages.size()) return {};
    const auto &m = m_messages[index.row()];
    switch (role) {
        case Qt::DisplayRole:
        case TextRole:      return m.text;
        case SenderIdRole:  return m.senderId;
        case SentAtRole:    return m.sentAt;
        case OutgoingRole:  return m.isOutgoing(m_selfId);
        case DeliveredRole: return m.delivered;
        case ReadRole:      return m.read;
        default:            return {};
    }
}

QHash<int, QByteArray> ChatModel::roleNames() const {
    auto names = QAbstractListModel::roleNames();
    names.insert(TextRole,      "text");
    names.insert(SenderIdRole,  "senderId");
    names.insert(SentAtRole,    "sentAt");
    names.insert(OutgoingRole,  "outgoing");
    names.insert(DeliveredRole, "delivered");
    names.insert(ReadRole,      "read");
    return names;
}

Message ChatModel::messageAt(int row) const {
    if (row < 0 || row >= m_messages.size()) return {};
    return m_messages[row];
}
