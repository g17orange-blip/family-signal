#pragma once

#include <QWidget>

class QLabel;
class QToolButton;

// Top bar of the chat pane: contact name + online dot + audio/video call
// buttons on the right.
class ChatHeader : public QWidget {
    Q_OBJECT
public:
    explicit ChatHeader(QWidget *parent = nullptr);

    void setContact(const QString &displayName, bool online);
    void setCallEnabled(bool enabled);
    // While a call is up, the two call buttons give way to a single red
    // "Завершить" — a hang-up that's always reachable even if the separate
    // call window got buried behind another app (the macOS lost-window bug).
    void setInCall(bool inCall);

signals:
    void audioCallRequested();
    void videoCallRequested();
    void hangupRequested();

private:
    void refreshButtons();

    QLabel      *m_name;
    QLabel      *m_status;
    QToolButton *m_audioBtn;
    QToolButton *m_videoBtn;
    QToolButton *m_hangupBtn;
    bool         m_inCall = false;
    bool         m_callEnabled = false;
};
