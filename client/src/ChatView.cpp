#include "ChatView.h"
#include "ChatModel.h"
#include "MessageBubbleDelegate.h"

#include <QScrollBar>

ChatView::ChatView(QWidget *parent) : QListView(parent) {
    m_delegate = new MessageBubbleDelegate(this);
    setItemDelegate(m_delegate);
    setSelectionMode(QAbstractItemView::NoSelection);
    setFocusPolicy(Qt::NoFocus);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setUniformItemSizes(false);
    setSpacing(0);
    viewport()->setAutoFillBackground(false);
    setStyleSheet(QStringLiteral("QListView { background: #1E2226; border: none; }"));
}

void ChatView::setChatModel(ChatModel *model) {
    setModel(model);
    connect(model, &QAbstractItemModel::rowsInserted, this,
            [this](const QModelIndex &, int, int last) {
        // Snap to bottom only for rows appended at the END (new messages /
        // a freshly opened conversation). Pages of older history are
        // prepended at the top and must not yank the viewport around.
        if (!this->model() || last != this->model()->rowCount() - 1) return;
        QMetaObject::invokeMethod(this, [this]() {
            verticalScrollBar()->setValue(verticalScrollBar()->maximum());
        }, Qt::QueuedConnection);
    });
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int v) {
        if (v <= 40 && verticalScrollBar()->maximum() > 0)
            emit needOlderMessages();
    });
}

void ChatView::resizeEvent(QResizeEvent *event) {
    QListView::resizeEvent(event);
    // sizeHint depends on viewport width — force the delegate to recompute.
    if (model()) {
        const int last = model()->rowCount() - 1;
        if (last >= 0) {
            emit model()->dataChanged(model()->index(0, 0), model()->index(last, 0));
        }
    }
}
