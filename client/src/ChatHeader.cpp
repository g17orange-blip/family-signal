#include "ChatHeader.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

ChatHeader::ChatHeader(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("chatHeader"));
    setAutoFillBackground(true);

    m_name   = new QLabel(this);
    m_status = new QLabel(this);
    m_name->setObjectName(QStringLiteral("chatHeaderName"));
    m_status->setObjectName(QStringLiteral("chatHeaderStatus"));

    auto *names = new QVBoxLayout;
    names->setContentsMargins(0, 0, 0, 0);
    names->setSpacing(2);
    names->addWidget(m_name);
    names->addWidget(m_status);

    // Text buttons, not bare icons: the grandfather shouldn't have to
    // guess what a pictogram does.
    m_audioBtn = new QToolButton(this);
    m_videoBtn = new QToolButton(this);
    m_audioBtn->setText(tr("📞 Позвонить"));
    m_videoBtn->setText(tr("🎥 Видеозвонок"));
    m_audioBtn->setToolTip(tr("Голосовой звонок — без камеры"));
    m_videoBtn->setToolTip(tr("Звонок с видео"));
    const QString btnStyle = QStringLiteral(
        "QToolButton { background: rgba(24,149,136,0.25); color: #E6ECF0;"
        "              border: 1px solid #189588; border-radius: 16px;"
        "              padding: 7px 14px; font-weight: 600; }"
        "QToolButton:hover:enabled { background: #189588; color: white; }"
        "QToolButton:disabled { color: #586068; border-color: #3A444E;"
        "                       background: transparent; }");
    m_audioBtn->setStyleSheet(btnStyle);
    m_videoBtn->setStyleSheet(btnStyle);
    m_audioBtn->setEnabled(false);
    m_videoBtn->setEnabled(false);

    // Hang-up that lives in the header too, so the call is always endable
    // even when its own window is off-screen or buried.
    m_hangupBtn = new QToolButton(this);
    m_hangupBtn->setText(tr("✕ Завершить"));
    m_hangupBtn->setToolTip(tr("Завершить текущий звонок"));
    m_hangupBtn->setStyleSheet(QStringLiteral(
        "QToolButton { background: #E54545; color: white; border: none;"
        "              border-radius: 16px; padding: 7px 16px; font-weight: 700; }"
        "QToolButton:hover { background: #FF5050; }"));
    m_hangupBtn->hide();

    connect(m_audioBtn, &QToolButton::clicked, this, &ChatHeader::audioCallRequested);
    connect(m_videoBtn, &QToolButton::clicked, this, &ChatHeader::videoCallRequested);
    connect(m_hangupBtn, &QToolButton::clicked, this, &ChatHeader::hangupRequested);

    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(16, 8, 12, 8);
    row->addLayout(names, 1);
    row->addWidget(m_audioBtn);
    row->addWidget(m_videoBtn);
    row->addWidget(m_hangupBtn);
}

void ChatHeader::setContact(const QString &displayName, bool online) {
    m_name->setText(displayName);
    m_status->setText(online ? tr("в сети") : tr("не в сети"));
    m_status->setStyleSheet(online
        ? QStringLiteral("color: #18B98C;")
        : QStringLiteral("color: #777E86;"));
}

void ChatHeader::setCallEnabled(bool enabled) {
    m_callEnabled = enabled;
    refreshButtons();
}

void ChatHeader::setInCall(bool inCall) {
    m_inCall = inCall;
    refreshButtons();
}

void ChatHeader::refreshButtons() {
    // In a call only the hang-up shows; otherwise the two call buttons,
    // enabled only when the open contact is reachable.
    m_hangupBtn->setVisible(m_inCall);
    m_audioBtn->setVisible(!m_inCall);
    m_videoBtn->setVisible(!m_inCall);
    m_audioBtn->setEnabled(m_callEnabled);
    m_videoBtn->setEnabled(m_callEnabled);
}
