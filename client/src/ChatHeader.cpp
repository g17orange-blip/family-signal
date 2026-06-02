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

    m_audioBtn = new QToolButton(this);
    m_videoBtn = new QToolButton(this);
    m_audioBtn->setText(QStringLiteral("📞"));
    m_videoBtn->setText(QStringLiteral("🎥"));
    m_audioBtn->setToolTip(tr("Голосовой звонок"));
    m_videoBtn->setToolTip(tr("Видеозвонок"));
    m_audioBtn->setAutoRaise(true);
    m_videoBtn->setAutoRaise(true);
    m_audioBtn->setEnabled(false);
    m_videoBtn->setEnabled(false);

    connect(m_audioBtn, &QToolButton::clicked, this, &ChatHeader::audioCallRequested);
    connect(m_videoBtn, &QToolButton::clicked, this, &ChatHeader::videoCallRequested);

    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(16, 8, 12, 8);
    row->addLayout(names, 1);
    row->addWidget(m_audioBtn);
    row->addWidget(m_videoBtn);
}

void ChatHeader::setContact(const QString &displayName, bool online) {
    m_name->setText(displayName);
    m_status->setText(online ? tr("в сети") : tr("не в сети"));
    m_status->setStyleSheet(online
        ? QStringLiteral("color: #18B98C;")
        : QStringLiteral("color: #777E86;"));
}

void ChatHeader::setCallEnabled(bool enabled) {
    m_audioBtn->setEnabled(enabled);
    m_videoBtn->setEnabled(enabled);
}
