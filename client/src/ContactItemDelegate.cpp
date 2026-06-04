#include "ContactItemDelegate.h"

#include "ContactsModel.h"

#include <QPainter>

void ContactItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                                const QModelIndex &index) const {
    // Base item (text, selection, hover) keeps the stylesheet look.
    QStyledItemDelegate::paint(painter, option, index);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    const QRect r = option.rect;

    // Online indicator: small green dot on the right edge.
    if (index.data(ContactsModel::OnlineRole).toBool()) {
        const int d = 10;
        const QRect dot(r.right() - d - 10, r.center().y() - d / 2, d, d);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0x4c, 0xaf, 0x50));
        painter->drawEllipse(dot);
    }

    // Unread badge: red circle with the count, left of the online dot.
    const int unread = index.data(ContactsModel::UnreadRole).toInt();
    if (unread > 0) {
        const QString text = unread > 99 ? QStringLiteral("99+")
                                         : QString::number(unread);
        QFont f = option.font;
        f.setBold(true);
        // The stylesheet sizes fonts in pixels, so pointSizeF() may be -1.
        if (f.pointSizeF() > 0)
            f.setPointSizeF(f.pointSizeF() * 0.85);
        else if (f.pixelSize() > 0)
            f.setPixelSize(int(f.pixelSize() * 0.85));
        painter->setFont(f);

        const int h = 20;
        const int w = qMax(h, QFontMetrics(f).horizontalAdvance(text) + 12);
        const QRect badge(r.right() - w - 28, r.center().y() - h / 2, w, h);

        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0xe5, 0x39, 0x35));   // alarm red
        painter->drawRoundedRect(badge, h / 2.0, h / 2.0);
        painter->setPen(Qt::white);
        painter->drawText(badge, Qt::AlignCenter, text);
    }

    painter->restore();
}
