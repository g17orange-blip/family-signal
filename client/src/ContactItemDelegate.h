#pragma once

#include <QStyledItemDelegate>

// Renders a contact row: the default (qss-styled) item plus an overlay —
// a green "online" dot and, when there are unread messages, a red badge
// with the count on the right edge. Big and obvious on purpose: the target
// user is a grandfather on a 1366x768 screen.
class ContactItemDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
};
