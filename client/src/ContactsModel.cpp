#include "ContactsModel.h"

ContactsModel::ContactsModel(QObject *parent) : QAbstractListModel(parent) {}

void ContactsModel::setContacts(QVector<Contact> contacts) {
    beginResetModel();
    m_contacts = std::move(contacts);
    endResetModel();
}

void ContactsModel::setOnlinePeers(const QStringList &onlineIds) {
    for (int row = 0; row < m_contacts.size(); ++row) {
        const bool nowOnline = onlineIds.contains(m_contacts[row].id);
        if (nowOnline != m_contacts[row].online) {
            m_contacts[row].online = nowOnline;
            const auto idx = index(row);
            emit dataChanged(idx, idx, {OnlineRole, Qt::DecorationRole});
        }
    }
}

int ContactsModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_contacts.size();
}

QVariant ContactsModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_contacts.size()) return {};
    const auto &c = m_contacts[index.row()];
    switch (role) {
        case Qt::DisplayRole:
        case DisplayNameRole:
            return c.displayName;
        case IdRole:
            return c.id;
        case OnlineRole:
            return c.online;
        default:
            return {};
    }
}

QHash<int, QByteArray> ContactsModel::roleNames() const {
    auto names = QAbstractListModel::roleNames();
    names.insert(IdRole,          "peerId");
    names.insert(DisplayNameRole, "displayName");
    names.insert(OnlineRole,      "online");
    return names;
}

Contact ContactsModel::contactAt(int row) const {
    if (row < 0 || row >= m_contacts.size()) return {};
    return m_contacts[row];
}

int ContactsModel::indexOf(const QString &peerId) const {
    for (int i = 0; i < m_contacts.size(); ++i) {
        if (m_contacts[i].id == peerId) return i;
    }
    return -1;
}
