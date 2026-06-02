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

signals:
    void audioCallRequested();
    void videoCallRequested();

private:
    QLabel      *m_name;
    QLabel      *m_status;
    QToolButton *m_audioBtn;
    QToolButton *m_videoBtn;
};
