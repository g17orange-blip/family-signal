#pragma once

#include <QWidget>

class QLabel;
class QPushButton;

// Separate top-level window shown during an active call. Contains a video
// area that GStreamer paints into directly (we hand its winId() to
// WebRtcSession::setVideoWindowHandle) plus a hang-up button.
class CallWindow : public QWidget {
    Q_OBJECT
public:
    explicit CallWindow(QWidget *parent = nullptr);

    // Native window handle of the video area. Pass to WebRtcSession.
    quintptr videoHandle() const;

    void setPeerName(const QString &name);
    void setStatus(const QString &status);

signals:
    void hangupRequested();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    QWidget     *m_videoArea;
    QLabel      *m_status;
    QPushButton *m_hangupBtn;
};
