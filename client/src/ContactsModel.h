#pragma once

#include "Contact.h"

#include <QAbstractListModel>
#include <QStringList>
#include <QVector>

// Backing model for the left-hand contacts list. Loaded once from Config
// and then updated in place when presence changes arrive from the signaling
// server. Two-entry-list for now, but the model handles arbitrary size.
class ContactsModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        DisplayNameRole,
        OnlineRole,
        UnreadRole,
    };

    explicit ContactsModel(QObject *parent = nullptr);

    void setContacts(QVector<Contact> contacts);
    void setOnlinePeers(const QStringList &onlineIds);

    // Unread message badge: bumped for messages arriving into a conversation
    // that is not currently on screen; cleared when the user opens it.
    void incrementUnread(const QString &peerId);
    void clearUnread(const QString &peerId);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Contact contactAt(int row) const;
    int     indexOf(const QString &peerId) const;

private:
    QVector<Contact>    m_contacts;
    QHash<QString, int> m_unread;   // peerId → count
};
