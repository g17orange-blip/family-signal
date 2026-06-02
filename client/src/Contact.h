#pragma once

#include <QString>

struct Contact {
    QString id;          // peer user_id matching what's on the signaling server
    QString displayName; // human-readable
    bool    online = false;
};
