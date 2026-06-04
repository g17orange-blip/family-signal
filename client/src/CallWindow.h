#pragma once

#include <QWidget>

class QLabel;
class QPushButton;

// Separate top-level window shown during a call. Two modes:
//   - incoming: green "Принять" + red "Отклонить", no media running yet —
//     the camera/microphone start only after the user accepts;
//   - active:   video area + red "Завершить".
class CallWindow : public QWidget {
    Q_OBJECT
public:
    explicit CallWindow(QWidget *parent = nullptr);

    // Native window handle of the video area. Pass to WebRtcSession.
    quintptr videoHandle() const;

    void setPeerName(const QString &name);
    void setStatus(const QString &status);

    // Incoming-call mode: show accept/decline, hide hangup.
    void showIncoming(const QString &peerName, bool video);
    // Active-call mode (caller side or after accepting).
    void showActive();

signals:
    void acceptRequested();
    void hangupRequested();   // also used as "decline" in incoming mode

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    QWidget     *m_videoArea;
    QLabel      *m_status;
    QPushButton *m_acceptBtn;
    QPushButton *m_hangupBtn;
};
