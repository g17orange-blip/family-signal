#pragma once

#include <QWidget>

class QTextEdit;
class QToolButton;

// Bottom input bar: a multi-line text edit + send button. Pressing Enter
// (without Shift) sends; Shift+Enter inserts a newline, matching the
// Telegram / Slack convention.
class MessageInputBar : public QWidget {
    Q_OBJECT
public:
    explicit MessageInputBar(QWidget *parent = nullptr);

    void clear();
    void setEnabled(bool enabled);

signals:
    void sendRequested(const QString &text);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onSendClicked();

private:
    QTextEdit   *m_edit;
    QToolButton *m_sendBtn;
};
