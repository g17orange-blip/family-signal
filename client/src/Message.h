#pragma once

#include <QDateTime>
#include <QString>

struct Message {
    // What the row represents; CallEvent* are local-only system notes
    // rendered as a centered pill in the conversation.
    enum Kind { Text = 0, CallEventGood = 1, CallEventBad = 2 };

    qint64    rowId    = -1;     // SQLite primary key, -1 for not-yet-stored
    int       kind     = Text;
    QString   msgId;              // stable UUID; pairs sends with delivery acks
    QString   peerId;             // remote party of the conversation
    QString   senderId;           // userId of whoever wrote the text
    QString   text;
    QDateTime sentAt;
    bool      delivered = false;  // true once DataChannel ack arrives
    bool      read      = false;  // outgoing: peer displayed it; incoming: we displayed it

    bool isOutgoing(const QString &selfId) const { return senderId == selfId; }
};
