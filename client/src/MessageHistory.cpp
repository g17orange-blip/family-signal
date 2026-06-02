#include "MessageHistory.h"

#include <QDir>
#include <QStandardPaths>
#include <QtSql/QSqlError>
#include <QtSql/QSqlQuery>

MessageHistory::MessageHistory(QObject *parent) : QObject(parent) {}

MessageHistory::~MessageHistory() {
    if (m_db.isOpen()) m_db.close();
}

bool MessageHistory::open() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    const QString path = QDir(dir).filePath(QStringLiteral("history.db"));

    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                     QStringLiteral("signal-history"));
    m_db.setDatabaseName(path);
    if (!m_db.open()) {
        m_error = m_db.lastError().text();
        return false;
    }
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS messages ("
            "  id          INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  peer_id     TEXT NOT NULL,"
            "  sender_id   TEXT NOT NULL,"
            "  text        TEXT NOT NULL,"
            "  sent_at     INTEGER NOT NULL,"
            "  delivered   INTEGER NOT NULL DEFAULT 0"
            ")"))) {
        m_error = q.lastError().text();
        return false;
    }
    q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_messages_peer_time "
        "ON messages(peer_id, sent_at)"));
    return true;
}

bool MessageHistory::append(Message &msg) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO messages (peer_id, sender_id, text, sent_at, delivered) "
        "VALUES (?, ?, ?, ?, ?)"));
    q.addBindValue(msg.peerId);
    q.addBindValue(msg.senderId);
    q.addBindValue(msg.text);
    q.addBindValue(msg.sentAt.toMSecsSinceEpoch());
    q.addBindValue(msg.delivered ? 1 : 0);
    if (!q.exec()) {
        m_error = q.lastError().text();
        return false;
    }
    msg.rowId = q.lastInsertId().toLongLong();
    return true;
}

bool MessageHistory::markDelivered(qint64 rowId) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE messages SET delivered = 1 WHERE id = ?"));
    q.addBindValue(rowId);
    return q.exec();
}

QVector<Message> MessageHistory::loadConversation(const QString &peerId, int limit) const {
    QVector<Message> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, peer_id, sender_id, text, sent_at, delivered "
        "FROM messages WHERE peer_id = ? "
        "ORDER BY sent_at DESC LIMIT ?"));
    q.addBindValue(peerId);
    q.addBindValue(limit);
    if (!q.exec()) return out;
    while (q.next()) {
        Message m;
        m.rowId     = q.value(0).toLongLong();
        m.peerId    = q.value(1).toString();
        m.senderId  = q.value(2).toString();
        m.text      = q.value(3).toString();
        m.sentAt    = QDateTime::fromMSecsSinceEpoch(q.value(4).toLongLong());
        m.delivered = q.value(5).toInt() != 0;
        out.prepend(m); // reverse so callers get oldest-first
    }
    return out;
}
