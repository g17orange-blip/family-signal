#pragma once

#include <QDialog>

class Config;
class MessageHistory;

// Settings live behind a menu item on purpose: the main window stays free of
// anything a non-technical user could press by accident. The "clear history"
// button in particular is double-confirmed.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    // `history` may be null (demo mode) — the data section is then disabled.
    SettingsDialog(Config *config, MessageHistory *history, QWidget *parent = nullptr);

signals:
    // Emitted after the local conversation store has been wiped, so the
    // main window can drop the visible chat as well.
    void historyCleared();
    // Emitted after a new invite code was applied and saved.
    void inviteApplied();

private slots:
    void onReapplyInvite();
    void onClearHistory();

private:
    Config         *m_config;
    MessageHistory *m_history;
};
