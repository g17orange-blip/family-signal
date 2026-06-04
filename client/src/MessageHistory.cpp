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

    if (!m_cipher.init(dir)) {
        m_error = m_cipher.errorString();
        return false;
    }

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
            "  text        TEXT NOT NULL,"   // encrypted blob since user_version 1
            "  sent_at     INTEGER NOT NULL,"
            "  delivered   INTEGER NOT NULL DEFAULT 0"
            ")"))) {
        m_error = q.lastError().text();
        return false;
    }
    q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_messages_peer_time "
        "ON messages(peer_id, sent_at)"));
    return migrateToEncrypted();
}

bool MessageHistory::migrateToEncrypted() {
    QSqlQuery v(m_db);
    if (!v.exec(QStringLiteral("PRAGMA user_version")) || !v.next()) {
        m_error = v.lastError().text();
        return false;
    }
    if (v.value(0).toInt() >= 1) return true;   // already encrypted

    if (!m_db.transaction()) {
        m_error = m_db.lastError().text();
        return false;
    }
    QSqlQuery sel(m_db);
    QSqlQuery upd(m_db);
    upd.prepare(QStringLiteral("UPDATE messages SET text = ? WHERE id = ?"));
    if (!sel.exec(QStringLiteral("SELECT id, text FROM messages"))) {
        m_error = sel.lastError().text();
        m_db.rollback();
        return false;
    }
    while (sel.next()) {
        const QByteArray stored = sel.value(1).toByteArray();
        if (HistoryCipher::looksEncrypted(stored)) continue;   // idempotent
        upd.addBindValue(m_cipher.encrypt(sel.value(1).toString()));
        upd.addBindValue(sel.value(0).toLongLong());
        if (!upd.exec()) {
            m_error = upd.lastError().text();
            m_db.rollback();
            return false;
        }
    }
    QSqlQuery setv(m_db);
    if (!setv.exec(QStringLiteral("PRAGMA user_version = 1")) || !m_db.commit()) {
        m_error = m_db.lastError().text();
        m_db.rollback();
        return false;
    }
    return true;
}

QString MessageHistory::decryptText(const QVariant &stored) const {
    const QByteArray raw = stored.toByteArray();
    if (HistoryCipher::looksEncrypted(raw)) return m_cipher.decrypt(raw);
    return stored.toString();   // defensive: pre-migration plaintext row
}

bool MessageHistory::append(Message &msg) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO messages (peer_id, sender_id, text, sent_at, delivered) "
        "VALUES (?, ?, ?, ?, ?)"));
    q.addBindValue(msg.peerId);
    q.addBindValue(msg.senderId);
    q.addBindValue(m_cipher.encrypt(msg.text));
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
        m.text      = decryptText(q.value(3));
        m.sentAt    = QDateTime::fromMSecsSinceEpoch(q.value(4).toLongLong());
        m.delivered = q.value(5).toInt() != 0;
        out.prepend(m); // reverse so callers get oldest-first
    }
    return out;
}
