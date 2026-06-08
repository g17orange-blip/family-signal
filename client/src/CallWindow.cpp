#include "CallWindow.h"

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

// Rounded own-camera preview. Plain QPainter rendering of QImage frames —
// since the remote picture is painted the same way (no native video
// surfaces anywhere), ordinary widget stacking is enough to stay on top.
// Width is a fraction of the video area; height follows the frame aspect.
class CallWindow::SelfView : public QWidget {
public:
    explicit SelfView(QWidget *parent) : QWidget(parent) { hide(); }

    void setFrame(const QImage &frame) {
        m_frame = frame;
        resizeToParent();
        if (!isVisible()) { show(); raise(); }
        update();
    }
    void resizeToParent() {
        if (!parentWidget() || m_frame.isNull()) return;
        const int w = qBound(120, int(parentWidget()->width() * 0.24), 320);
        const int h = qMax(1, m_frame.height() * w / qMax(1, m_frame.width()));
        if (size() != QSize(w, h)) setFixedSize(w, h);
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        QPainterPath clip;
        clip.addRoundedRect(rect(), 14, 14);
        p.setClipPath(clip);
        p.fillRect(rect(), QColor(0x10, 0x14, 0x18));
        if (!m_frame.isNull())
            p.drawImage(rect(), m_frame);
        p.setClipping(false);
        p.setPen(QPen(QColor(255, 255, 255, 60), 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 14, 14);
    }
private:
    QImage m_frame;
};

// Full-area remote picture, aspect-fit on black, painted from QImages.
class CallWindow::RemoteView : public QWidget {
public:
    explicit RemoteView(QWidget *parent) : QWidget(parent) {
        setAutoFillBackground(false);
    }
    void setFrame(const QImage &frame) {
        m_frame = frame;
        update();
    }
    void clear() { m_frame = QImage(); update(); }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.fillRect(rect(), Qt::black);
        if (m_frame.isNull()) return;
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        QSize s = m_frame.size().scaled(size(), Qt::KeepAspectRatio);
        QRect target(QPoint(0, 0), s);
        target.moveCenter(rect().center());
        p.drawImage(target, m_frame);
    }
private:
    QImage m_frame;
};

CallWindow::CallWindow(QWidget *parent) : QWidget(parent) {
    // A top-level window despite having a parent: the parent is the main
    // window so macOS keeps this window with the app (an orphaned secondary
    // window can disappear behind other apps with no way to get it back),
    // but Qt::Window makes it its own resizable/maximizable frame, not an
    // embedded child widget.
    setWindowFlag(Qt::Window);
    setWindowTitle(tr("Звонок"));
    resize(720, 540);

    m_videoArea = new RemoteView(this);
    m_videoArea->setObjectName(QStringLiteral("videoArea"));
    m_videoArea->setMinimumSize(320, 240);

    // Own camera preview pinned to the bottom-right corner of the video
    // area; frames arrive via setSelfFrame().
    m_selfView = new SelfView(m_videoArea);

    m_status = new QLabel(tr("Соединение..."), this);
    m_status->setAlignment(Qt::AlignCenter);
    m_status->setStyleSheet(QStringLiteral("color: #C0C8CF; padding: 6px; font-size: 16px;"));

    // Big, unambiguous buttons — the target user is a grandfather.
    m_acceptBtn = new QPushButton(tr("✆ Принять"), this);
    m_acceptBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: #2EBD59; color: white; border: none;"
        "              padding: 14px 32px; border-radius: 26px;"
        "              font-weight: 700; font-size: 16px; }"
        "QPushButton:hover { background: #36D866; }"));
    connect(m_acceptBtn, &QPushButton::clicked, this, &CallWindow::acceptRequested);

    m_hangupBtn = new QPushButton(tr("Завершить"), this);
    m_hangupBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: #E54545; color: white; border: none;"
        "              padding: 14px 32px; border-radius: 26px;"
        "              font-weight: 700; font-size: 16px; }"
        "QPushButton:hover { background: #FF5050; }"));
    connect(m_hangupBtn, &QPushButton::clicked, this, &CallWindow::hangupRequested);

    auto *controls = new QHBoxLayout;
    controls->addStretch();
    controls->addWidget(m_acceptBtn);
    controls->addSpacing(24);
    controls->addWidget(m_hangupBtn);
    controls->addStretch();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 14);
    layout->setSpacing(8);
    layout->addWidget(m_videoArea, 1);
    layout->addWidget(m_status);
    layout->addLayout(controls);

    showActive();   // default look until told otherwise
}

void CallWindow::setRemoteFrame(const QImage &frame) {
    m_videoArea->setFrame(frame);
}

void CallWindow::setSelfFrame(const QImage &frame) {
    m_selfView->setFrame(frame);
    repositionSelfView();   // after: the frame may have changed the size
}

void CallWindow::setSelfViewVisible(bool visible) {
    m_selfView->setVisible(visible);
    if (visible) {
        repositionSelfView();
        m_selfView->raise();
    }
}

void CallWindow::repositionSelfView() {
    // Top-right: the bottom edge sits too close to the status/controls strip.
    m_selfView->resizeToParent();
    const int margin = 12;
    m_selfView->move(m_videoArea->width() - m_selfView->width() - margin,
                     margin);
}

void CallWindow::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    repositionSelfView();
}

void CallWindow::setPeerName(const QString &name) {
    setWindowTitle(tr("Звонок — %1").arg(name));
}

void CallWindow::setStatus(const QString &status) {
    m_status->setText(status);
}

void CallWindow::showIncoming(const QString &peerName, bool video) {
    setPeerName(peerName);
    setStatus(video ? tr("Входящий видеозвонок от %1").arg(peerName)
                    : tr("Входящий звонок от %1").arg(peerName));
    m_acceptBtn->show();
    m_hangupBtn->setText(tr("Отклонить"));
    show();
    raise();
    activateWindow();
}

void CallWindow::showActive() {
    m_acceptBtn->hide();
    m_hangupBtn->setText(tr("Завершить"));
    // Fresh call, fresh pictures: the previews pop up with their first
    // frames; audio-only calls produce none, so they stay dark/hidden.
    m_selfView->hide();
    m_videoArea->clear();
}

void CallWindow::closeEvent(QCloseEvent *event) {
    emit hangupRequested();
    event->accept();
}
