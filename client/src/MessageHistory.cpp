#include "MessageHistory.h"

#include <QDir>
#include <QStandardPaths>
#include <QUuid>
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
    return migrateToEncrypted() && migrateAddMsgId() && migrateAddRead();
}

// v3: read receipts. `read` — outgoing: peer displayed it / incoming: we
// displayed it. `read_sent` (incoming only) — the receipt reached the peer;
// kept separately so receipts survive the peer being offline at read time.
bool MessageHistory::migrateAddRead() {
    QSqlQuery v(m_db);
    if (!v.exec(QStringLiteral("PRAGMA user_version")) || !v.next()) {
        m_error = v.lastError().text();
        return false;
    }
    if (v.value(0).toInt() >= 3) return true;

    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral(
            "ALTER TABLE messages ADD COLUMN read INTEGER NOT NULL DEFAULT 0")) ||
        !q.exec(QStringLiteral(
            "ALTER TABLE messages ADD COLUMN read_sent INTEGER NOT NULL DEFAULT 0"))) {
        m_error = q.lastError().text();
        return false;
    }
    q.exec(QStringLiteral("PRAGMA user_version = 3"));
    return true;
}

// v2: per-message UUID used to pair delivery acks and dedup re-sends.
bool MessageHistory::migrateAddMsgId() {
    QSqlQuery v(m_db);
    if (!v.exec(QStringLiteral("PRAGMA user_version")) || !v.next()) {
        m_error = v.lastError().text();
        return false;
    }
    if (v.value(0).toInt() >= 2) return true;

    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("ALTER TABLE messages ADD COLUMN msg_id TEXT"))) {
        m_error = q.lastError().text();
        return false;
    }
    // Backfill old rows so msg_id is never NULL going forward.
    QSqlQuery sel(m_db), upd(m_db);
    upd.prepare(QStringLiteral("UPDATE messages SET msg_id = ? WHERE id = ?"));
    if (sel.exec(QStringLiteral("SELECT id FROM messages WHERE msg_id IS NULL"))) {
        while (sel.next()) {
            upd.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces));
            upd.addBindValue(sel.value(0).toLongLong());
            upd.exec();
        }
    }
    q.exec(QStringLiteral(
        "CREATE UNIQUE INDEX IF NOT EXISTS idx_messages_msg_id ON messages(msg_id)"));
    q.exec(QStringLiteral("PRAGMA user_version = 2"));
    return true;
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
    if (msg.msgId.isEmpty())
        msg.msgId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO messages (msg_id, peer_id, sender_id, text, sent_at, delivered) "
        "VALUES (?, ?, ?, ?, ?, ?)"));
    q.addBindValue(msg.msgId);
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

bool MessageHistory::markDeliveredByMsgId(const QString &msgId) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE messages SET delivered = 1 WHERE msg_id = ?"));
    q.addBindValue(msgId);
    return q.exec() && q.numRowsAffected() > 0;
}

namespace {
// "?, ?, ?, ..." for binding a string list into an IN (...) clause.
QString placeholders(int n) {
    QStringList p;
    p.reserve(n);
    for (int i = 0; i < n; ++i) p.append(QStringLiteral("?"));
    return p.join(QStringLiteral(", "));
}
} // namespace

QStringList MessageHistory::markConversationRead(const QString &peerId,
                                                 const QString &selfId) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE messages SET read = 1 "
        "WHERE peer_id = ? AND sender_id != ? AND read = 0"));
    q.addBindValue(peerId);
    q.addBindValue(selfId);
    q.exec();
    return pendingReceipts(peerId, selfId);
}

QStringList MessageHistory::pendingReceipts(const QString &peerId,
                                            const QString &selfId) const {
    QStringList out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT msg_id FROM messages "
        "WHERE peer_id = ? AND sender_id != ? AND read = 1 AND read_sent = 0"));
    q.addBindValue(peerId);
    q.addBindValue(selfId);
    if (q.exec())
        while (q.next()) out.append(q.value(0).toString());
    return out;
}

bool MessageHistory::markReceiptsSent(const QStringList &msgIds) {
    if (msgIds.isEmpty()) return true;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE messages SET read_sent = 1 WHERE msg_id IN (%1)")
                  .arg(placeholders(msgIds.size())));
    for (const QString &id : msgIds) q.addBindValue(id);
    return q.exec();
}

bool MessageHistory::markPeerRead(const QStringList &msgIds) {
    if (msgIds.isEmpty()) return true;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE messages SET read = 1 WHERE msg_id IN (%1)")
                  .arg(placeholders(msgIds.size())));
    for (const QString &id : msgIds) q.addBindValue(id);
    return q.exec() && q.numRowsAffected() > 0;
}

bool MessageHistory::containsMsgId(const QString &msgId) const {
    if (msgId.isEmpty()) return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT 1 FROM messages WHERE msg_id = ? LIMIT 1"));
    q.addBindValue(msgId);
    return q.exec() && q.next();
}

QVector<Message> MessageHistory::loadUndelivered(const QString &peerId,
                                                 const QString &selfId) const {
    QVector<Message> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, msg_id, peer_id, sender_id, text, sent_at, delivered "
        "FROM messages "
        "WHERE peer_id = ? AND sender_id = ? AND delivered = 0 "
        "ORDER BY sent_at ASC"));
    q.addBindValue(peerId);
    q.addBindValue(selfId);
    if (!q.exec()) return out;
    while (q.next()) {
        Message m;
        m.rowId     = q.value(0).toLongLong();
        m.msgId     = q.value(1).toString();
        m.peerId    = q.value(2).toString();
        m.senderId  = q.value(3).toString();
        m.text      = decryptText(q.value(4));
        m.sentAt    = QDateTime::fromMSecsSinceEpoch(q.value(5).toLongLong());
        m.delivered = false;
        out.append(m);
    }
    return out;
}

bool MessageHistory::clearAll() {
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("DELETE FROM messages"))) {
        m_error = q.lastError().text();
        return false;
    }
    q.exec(QStringLiteral("VACUUM"));   // shrink the file so the data is really gone
    return true;
}

QVector<Message> MessageHistory::loadConversation(const QString &peerId, int limit) const {
    QVector<Message> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, msg_id, peer_id, sender_id, text, sent_at, delivered, read "
        "FROM messages WHERE peer_id = ? "
        "ORDER BY sent_at DESC LIMIT ?"));
    q.addBindValue(peerId);
    q.addBindValue(limit);
    if (!q.exec()) return out;
    while (q.next()) {
        Message m;
        m.rowId     = q.value(0).toLongLong();
        m.msgId     = q.value(1).toString();
        m.peerId    = q.value(2).toString();
        m.senderId  = q.value(3).toString();
        m.text      = decryptText(q.value(4));
        m.sentAt    = QDateTime::fromMSecsSinceEpoch(q.value(5).toLongLong());
        m.delivered = q.value(6).toInt() != 0;
        m.read      = q.value(7).toInt() != 0;
        out.prepend(m); // reverse so callers get oldest-first
    }
    return out;
}
