#include "SettingsDialog.h"

#include "Config.h"
#include "FirstRunDialog.h"
#include "Logging.h"
#include "MessageHistory.h"
#include "WebRtcSession.h"

#include <QDesktopServices>
#include <QUrl>

#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(Config *config, MessageHistory *history, QWidget *parent)
    : QDialog(parent), m_config(config), m_history(history) {
    setWindowTitle(tr("Настройки"));
    setMinimumWidth(420);

    auto *layout = new QVBoxLayout(this);

    // --- Connection info (read-only) --------------------------------------
    auto *infoBox = new QGroupBox(tr("Подключение"));
    auto *infoLay = new QVBoxLayout(infoBox);
    auto *info = new QLabel(
        tr("Вы: %1 (%2)\nСервер: %3")
            .arg(m_config->displayName, m_config->userId,
                 m_config->signalingUrl.toString()));
    info->setTextInteractionFlags(Qt::TextSelectableByMouse);
    infoLay->addWidget(info);

    auto *reapply = new QPushButton(tr("Ввести новый код приглашения…"));
    connect(reapply, &QPushButton::clicked, this, &SettingsDialog::onReapplyInvite);
    infoLay->addWidget(reapply, 0, Qt::AlignLeft);
    layout->addWidget(infoBox);

    // --- Data --------------------------------------------------------------
    auto *dataBox = new QGroupBox(tr("Данные на этом устройстве"));
    auto *dataLay = new QVBoxLayout(dataBox);
    auto *note = new QLabel(tr("История хранится только на этом компьютере "
                               "в зашифрованном виде. Сервер переписку не видит."));
    note->setWordWrap(true);
    dataLay->addWidget(note);

    auto *clear = new QPushButton(tr("Очистить историю переписки…"));
    clear->setEnabled(m_history != nullptr);
    connect(clear, &QPushButton::clicked, this, &SettingsDialog::onClearHistory);
    dataLay->addWidget(clear, 0, Qt::AlignLeft);
    layout->addWidget(dataBox);

    // --- Diagnostics ---------------------------------------------------------
    auto *diagBox = new QGroupBox(tr("Если что-то не работает"));
    auto *diagLay = new QVBoxLayout(diagBox);
    auto *diag = new QPushButton(tr("Проверить камеру и микрофон…"));
    connect(diag, &QPushButton::clicked, this, [this] {
        QMessageBox box(QMessageBox::Information, tr("Диагностика"),
                        WebRtcSession::mediaDiagnostics(),
                        QMessageBox::Close, this);
        box.setTextInteractionFlags(Qt::TextSelectableByMouse);
        box.exec();
    });
    diagLay->addWidget(diag, 0, Qt::AlignLeft);

    auto *logs = new QPushButton(tr("Открыть папку с логами…"));
    connect(logs, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(Logging::directory()));
    });
    diagLay->addWidget(logs, 0, Qt::AlignLeft);
    layout->addWidget(diagBox);

#ifndef SIGNAL_VERSION
#define SIGNAL_VERSION "dev"
#endif
    auto *version = new QLabel(tr("Signal, версия %1").arg(QStringLiteral(SIGNAL_VERSION)));
    version->setObjectName(QStringLiteral("versionLabel"));
    version->setStyleSheet(QStringLiteral("color: gray;"));
    layout->addWidget(version);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void SettingsDialog::onReapplyInvite() {
    // Reuses the first-run wizard; on accept it fills m_config and saves it.
    FirstRunDialog dlg(m_config, this);
    if (dlg.exec() != QDialog::Accepted) return;
    QMessageBox::information(
        this, tr("Готово"),
        tr("Новый код применён. Список контактов обновится при следующем "
           "запуске приложения."));
    emit inviteApplied();
}

void SettingsDialog::onClearHistory() {
    if (!m_history) return;

    // Deliberately two confirmations with "No" as the default: this lives in
    // settings so it can't be hit by accident, but a misclick inside settings
    // shouldn't wipe the conversation either.
    if (QMessageBox::question(
            this, tr("Очистить историю"),
            tr("Удалить всю историю переписки на этом устройстве?\n"
               "У собеседников их копии сохранятся."),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes)
        return;
    if (QMessageBox::warning(
            this, tr("Очистить историю"),
            tr("Точно удалить? Это действие необратимо."),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes)
        return;

    if (!m_history->clearAll()) {
        QMessageBox::critical(this, tr("Ошибка"), m_history->errorString());
        return;
    }
    QMessageBox::information(this, tr("Готово"), tr("История удалена."));
    emit historyCleared();
}
