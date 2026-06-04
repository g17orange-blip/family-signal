#include "CallWindow.h"

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

CallWindow::CallWindow(QWidget *parent) : QWidget(parent) {
    setWindowTitle(tr("Звонок"));
    resize(720, 540);

    m_videoArea = new QWidget(this);
    m_videoArea->setObjectName(QStringLiteral("videoArea"));
    // Tell Qt to give the video area its own native window so winId()
    // returns a real platform handle that GstVideoOverlay can paint into.
    m_videoArea->setAttribute(Qt::WA_NativeWindow, true);
    m_videoArea->setAttribute(Qt::WA_DontCreateNativeAncestors, false);
    m_videoArea->setAutoFillBackground(true);
    QPalette p = m_videoArea->palette();
    p.setColor(QPalette::Window, Qt::black);
    m_videoArea->setPalette(p);
    m_videoArea->setMinimumSize(320, 240);

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

quintptr CallWindow::videoHandle() const {
    return static_cast<quintptr>(m_videoArea->winId());
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
}

void CallWindow::closeEvent(QCloseEvent *event) {
    emit hangupRequested();
    event->accept();
}
