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
    m_status->setStyleSheet(QStringLiteral("color: #C0C8CF; padding: 6px;"));

    m_hangupBtn = new QPushButton(tr("Завершить"), this);
    m_hangupBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: #E54545; color: white; border: none;"
        "              padding: 10px 24px; border-radius: 22px; font-weight: 600; }"
        "QPushButton:hover { background: #FF5050; }"));
    connect(m_hangupBtn, &QPushButton::clicked, this, &CallWindow::hangupRequested);

    auto *controls = new QHBoxLayout;
    controls->addStretch();
    controls->addWidget(m_hangupBtn);
    controls->addStretch();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 12);
    layout->setSpacing(8);
    layout->addWidget(m_videoArea, 1);
    layout->addWidget(m_status);
    layout->addLayout(controls);
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

void CallWindow::closeEvent(QCloseEvent *event) {
    emit hangupRequested();
    event->accept();
}
