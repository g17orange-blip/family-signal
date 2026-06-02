#include "MessageInputBar.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QTextEdit>
#include <QToolButton>

MessageInputBar::MessageInputBar(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("messageInputBar"));
    m_edit = new QTextEdit(this);
    m_edit->setPlaceholderText(tr("Сообщение"));
    m_edit->setAcceptRichText(false);
    m_edit->setTabChangesFocus(true);
    m_edit->setFixedHeight(44);
    m_edit->installEventFilter(this);
    m_edit->setStyleSheet(QStringLiteral(
        "QTextEdit { background: #2B3036; color: #E6E9EC;"
        "            border: 1px solid #3A4047; border-radius: 18px;"
        "            padding: 10px 14px; }"));

    m_sendBtn = new QToolButton(this);
    m_sendBtn->setText(QStringLiteral("➤"));
    m_sendBtn->setToolTip(tr("Отправить"));
    m_sendBtn->setAutoRaise(true);
    m_sendBtn->setFixedSize(36, 36);
    connect(m_sendBtn, &QToolButton::clicked, this, &MessageInputBar::onSendClicked);

    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(10, 8, 10, 8);
    row->addWidget(m_edit, 1);
    row->addWidget(m_sendBtn);
}

bool MessageInputBar::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_edit && event->type() == QEvent::KeyPress) {
        auto *k = static_cast<QKeyEvent *>(event);
        if ((k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter)
                && !(k->modifiers() & Qt::ShiftModifier)) {
            onSendClicked();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void MessageInputBar::onSendClicked() {
    const QString text = m_edit->toPlainText().trimmed();
    if (text.isEmpty()) return;
    emit sendRequested(text);
    m_edit->clear();
}

void MessageInputBar::clear() { m_edit->clear(); }

void MessageInputBar::setEnabled(bool enabled) {
    m_edit->setEnabled(enabled);
    m_sendBtn->setEnabled(enabled);
}
