#pragma once

#include <QListView>

class ChatModel;
class MessageBubbleDelegate;

// QListView preconfigured with the bubble delegate. Auto-scrolls to the
// bottom whenever a new row arrives (the typical messenger behaviour).
class ChatView : public QListView {
    Q_OBJECT
public:
    explicit ChatView(QWidget *parent = nullptr);

    void setChatModel(ChatModel *model);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    MessageBubbleDelegate *m_delegate;
};
