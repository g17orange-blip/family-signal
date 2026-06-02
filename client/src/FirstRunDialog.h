#pragma once

#include <QDialog>

class Config;
class QPlainTextEdit;
class QLineEdit;
class QGroupBox;
class QLabel;

// Shown on first launch when no config.json exists. The primary path is to
// paste the one-line invite code produced by server/install.sh; an expandable
// "manual" section lets the user type the fields by hand instead.
//
// On accept() the supplied Config is filled and written to disk; the caller
// can then proceed straight into MainWindow::start().
class FirstRunDialog : public QDialog {
    Q_OBJECT
public:
    explicit FirstRunDialog(Config *config, QWidget *parent = nullptr);

private slots:
    void onAccept();
    void toggleManual(bool on);

private:
    Config *m_config;

    QPlainTextEdit *m_invite      = nullptr;
    QGroupBox      *m_manualBox   = nullptr;
    QLineEdit      *m_userId      = nullptr;
    QLineEdit      *m_displayName = nullptr;
    QLineEdit      *m_signaling   = nullptr;
    QLineEdit      *m_token       = nullptr;
    QLineEdit      *m_stun        = nullptr;
    QLineEdit      *m_turnUrl     = nullptr;
    QLineEdit      *m_turnUser    = nullptr;
    QLineEdit      *m_turnPass    = nullptr;
    QLineEdit      *m_peerId      = nullptr;
    QLineEdit      *m_peerName    = nullptr;
    QLabel         *m_errorLabel  = nullptr;
};
