#pragma once

#include <QStyledItemDelegate>

// Paints chat bubbles. Outgoing messages are right-aligned with an accent
// background; incoming messages are left-aligned in neutral grey. Text is
// word-wrapped at ~65% of the viewport width; below the text sits a small
// row with the timestamp and (for outgoing) a check-mark when delivered.
class MessageBubbleDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit MessageBubbleDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter,
               const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;

    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override;

private:
    // Computes the bubble rect (inside the row rect) and the text rect
    // (inside the bubble) for a given option/index. Used by both paint
    // and sizeHint so they agree.
    struct Layout {
        QRect bubble;
        QRect text;
        QRect meta;
    };
    Layout computeLayout(const QStyleOptionViewItem &option,
                         const QModelIndex &index) const;
};
