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
    connect(model, &QAbstractItemModel::rowsInserted, this, [this]() {
        // Snap to bottom after the new row is laid out.
        QMetaObject::invokeMethod(this, [this]() {
            verticalScrollBar()->setValue(verticalScrollBar()->maximum());
        }, Qt::QueuedConnection);
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
