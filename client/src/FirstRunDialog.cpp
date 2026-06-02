#include "FirstRunDialog.h"

#include "Config.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

FirstRunDialog::FirstRunDialog(Config *config, QWidget *parent)
    : QDialog(parent), m_config(config) {
    setWindowTitle(tr("Настройка Signal"));
    setMinimumWidth(460);

    auto *root = new QVBoxLayout(this);

    auto *intro = new QLabel(
        tr("Вставьте код приглашения, который вам прислали, и нажмите «Подключиться»."));
    intro->setWordWrap(true);
    root->addWidget(intro);

    m_invite = new QPlainTextEdit;
    m_invite->setPlaceholderText(tr("Код приглашения"));
    m_invite->setTabChangesFocus(true);
    m_invite->setFixedHeight(90);
    root->addWidget(m_invite);

    // --- collapsible manual entry ----------------------------------------
    auto *manualToggle = new QPushButton(tr("Ввести вручную"));
    manualToggle->setCheckable(true);
    manualToggle->setFlat(true);
    root->addWidget(manualToggle, 0, Qt::AlignLeft);

    m_manualBox = new QGroupBox;
    m_manualBox->setVisible(false);
    auto *form = new QFormLayout(m_manualBox);
    m_userId      = new QLineEdit;
    m_displayName = new QLineEdit;
    m_signaling   = new QLineEdit;
    m_signaling->setPlaceholderText(QStringLiteral("wss://signal.example.com/ws"));
    m_token       = new QLineEdit;
    m_stun        = new QLineEdit;
    m_stun->setPlaceholderText(QStringLiteral("stun:signal.example.com:3478"));
    m_turnUrl     = new QLineEdit;
    m_turnUrl->setPlaceholderText(QStringLiteral("turn:signal.example.com:3478"));
    m_turnUser    = new QLineEdit;
    m_turnPass    = new QLineEdit;
    m_peerId      = new QLineEdit;
    m_peerName    = new QLineEdit;
    form->addRow(tr("Ваш id"),          m_userId);
    form->addRow(tr("Ваше имя"),        m_displayName);
    form->addRow(tr("Адрес сигналинга"), m_signaling);
    form->addRow(tr("Токен"),           m_token);
    form->addRow(tr("STUN"),            m_stun);
    form->addRow(tr("TURN url"),        m_turnUrl);
    form->addRow(tr("TURN логин"),      m_turnUser);
    form->addRow(tr("TURN пароль"),     m_turnPass);
    form->addRow(tr("Контакт: id"),     m_peerId);
    form->addRow(tr("Контакт: имя"),    m_peerName);
    root->addWidget(m_manualBox);

    m_errorLabel = new QLabel;
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setStyleSheet(QStringLiteral("color:#e06c75;"));
    m_errorLabel->setVisible(false);
    root->addWidget(m_errorLabel);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    auto *connectBtn = buttons->addButton(tr("Подключиться"), QDialogButtonBox::AcceptRole);
    root->addWidget(buttons);

    connect(manualToggle, &QPushButton::toggled, this, &FirstRunDialog::toggleManual);
    connect(connectBtn, &QPushButton::clicked, this, &FirstRunDialog::onAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void FirstRunDialog::toggleManual(bool on) {
    m_manualBox->setVisible(on);
    adjustSize();
}

void FirstRunDialog::onAccept() {
    const QString invite = m_invite->toPlainText().trimmed();

    if (!invite.isEmpty()) {
        if (!m_config->applyInvite(invite)) {
            m_errorLabel->setText(m_config->errorString());
            m_errorLabel->setVisible(true);
            return;
        }
    } else {
        // Manual entry path.
        m_config->userId         = m_userId->text().trimmed();
        m_config->displayName    = m_displayName->text().trimmed();
        m_config->signalingUrl   = QUrl(m_signaling->text().trimmed());
        m_config->signalingToken = m_token->text().trimmed();
        m_config->stunUrl        = m_stun->text().trimmed();
        m_config->turn.url       = m_turnUrl->text().trimmed();
        m_config->turn.username  = m_turnUser->text().trimmed();
        m_config->turn.password  = m_turnPass->text();
        m_config->peers.clear();
        if (!m_peerId->text().trimmed().isEmpty()) {
            m_config->peers.append({m_peerId->text().trimmed(),
                                    m_peerName->text().trimmed()});
        }
        if (m_config->userId.isEmpty() || !m_config->signalingUrl.isValid()
            || m_config->signalingUrl.isEmpty()) {
            m_errorLabel->setText(tr("Заполните хотя бы «Ваш id» и «Адрес сигналинга», "
                                     "либо вставьте код приглашения."));
            m_errorLabel->setVisible(true);
            return;
        }
    }

    if (!m_config->save()) {
        m_errorLabel->setText(m_config->errorString());
        m_errorLabel->setVisible(true);
        return;
    }
    accept();
}
