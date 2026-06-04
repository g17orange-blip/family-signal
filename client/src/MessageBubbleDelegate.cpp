#include "MessageBubbleDelegate.h"
#include "ChatModel.h"

#include <QApplication>
#include <QDateTime>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QTextOption>

namespace {
constexpr int kBubblePaddingX = 12;
constexpr int kBubblePaddingY = 8;
constexpr int kRadius         = 12;
constexpr int kMargin         = 8;
constexpr int kMetaSpacing    = 4;

// Bubble fills at most this fraction of the row width.
constexpr qreal kMaxBubbleFrac = 0.65;
}

MessageBubbleDelegate::MessageBubbleDelegate(QObject *parent)
    : QStyledItemDelegate(parent) {}

MessageBubbleDelegate::Layout
MessageBubbleDelegate::computeLayout(const QStyleOptionViewItem &option,
                                      const QModelIndex &index) const {
    const QString text = index.data(ChatModel::TextRole).toString();
    const bool outgoing = index.data(ChatModel::OutgoingRole).toBool();
    const QDateTime sentAt = index.data(ChatModel::SentAtRole).toDateTime();
    QString meta = sentAt.toString(QStringLiteral("HH:mm"));
    if (outgoing) meta += QStringLiteral("      ");   // room for painted ticks

    const int rowW = option.rect.width();
    const int maxBubbleW = int(rowW * kMaxBubbleFrac);

    QFont metaFont = option.font;
    metaFont.setPointSizeF(metaFont.pointSizeF() * 0.85);
    QFontMetrics fm(option.font);
    QFontMetrics metaFm(metaFont);

    const int maxTextW = maxBubbleW - 2 * kBubblePaddingX;
    QRect textBound = fm.boundingRect(
        QRect(0, 0, maxTextW, INT_MAX),
        Qt::TextWordWrap, text);

    const int metaW = metaFm.horizontalAdvance(meta);
    const int innerW = qMax(textBound.width(), metaW);
    const int bubbleW = innerW + 2 * kBubblePaddingX;
    const int bubbleH = textBound.height() + metaFm.height() + 2 * kBubblePaddingY + kMetaSpacing;

    QRect bubble;
    bubble.setSize(QSize(bubbleW, bubbleH));
    if (outgoing) {
        bubble.moveRight(option.rect.right() - kMargin);
    } else {
        bubble.moveLeft(option.rect.left() + kMargin);
    }
    bubble.moveTop(option.rect.top() + kMargin / 2);

    QRect textRect = bubble.adjusted(kBubblePaddingX, kBubblePaddingY,
                                      -kBubblePaddingX, -(kBubblePaddingY + metaFm.height() + kMetaSpacing));
    QRect metaRect(textRect.left(), textRect.bottom() + kMetaSpacing,
                   textRect.width(), metaFm.height());

    return {bubble, textRect, metaRect};
}

void MessageBubbleDelegate::paint(QPainter *painter,
                                   const QStyleOptionViewItem &option,
                                   const QModelIndex &index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    const Layout L = computeLayout(option, index);
    const bool outgoing = index.data(ChatModel::OutgoingRole).toBool();
    const QString text = index.data(ChatModel::TextRole).toString();
    const QDateTime sentAt = index.data(ChatModel::SentAtRole).toDateTime();
    const bool delivered = index.data(ChatModel::DeliveredRole).toBool();

    // Colour palette — kept distinct from Telegram's by using a teal accent
    // instead of their blue, and a slightly cooler grey for incoming.
    const QColor outgoingBg(0x18, 0x95, 0x88);
    const QColor incomingBg(0x2E, 0x33, 0x38);
    const QColor textColor(Qt::white);
    const QColor metaColor(0xC0, 0xC8, 0xCF);

    QPainterPath bubble;
    bubble.addRoundedRect(L.bubble, kRadius, kRadius);
    painter->fillPath(bubble, outgoing ? outgoingBg : incomingBg);

    painter->setPen(textColor);
    QTextOption opt;
    opt.setWrapMode(QTextOption::WordWrap);
    painter->drawText(L.text, text, opt);

    QFont metaFont = option.font;
    metaFont.setPointSizeF(metaFont.pointSizeF() * 0.85);
    painter->setFont(metaFont);
    painter->setPen(metaColor);
    const QString meta = sentAt.toString(QStringLiteral("HH:mm"));
    if (!outgoing) {
        painter->drawText(L.meta, Qt::AlignLeft, meta);
        painter->restore();
        return;
    }

    // Outgoing: time, then Telegram-style overlapping ticks.
    //   one grey tick   — sent, not yet delivered
    //   two grey ticks  — delivered to the peer's device
    //   two green ticks — the peer opened the conversation (read receipt)
    const bool read = index.data(ChatModel::ReadRole).toBool();
    const int tickAreaW = 22;
    QRect timeRect = L.meta.adjusted(0, 0, -tickAreaW, 0);
    painter->drawText(timeRect, Qt::AlignRight, meta);

    const QColor tickColor = read ? QColor(0x8A, 0xFF, 0xB0)   // bright mint
                                  : metaColor;                  // grey
    QPen tickPen(tickColor, 1.6);
    tickPen.setCapStyle(Qt::RoundCap);
    tickPen.setJoinStyle(Qt::RoundJoin);
    painter->setPen(tickPen);

    auto drawTick = [painter](const QPointF &origin) {
        // A small check mark: down-stroke then the long up-stroke.
        QPainterPath p(origin + QPointF(0.0, 4.5));
        p.lineTo(origin + QPointF(3.0, 7.5));
        p.lineTo(origin + QPointF(9.0, 0.5));
        painter->drawPath(p);
    };
    const qreal ticksRight = L.meta.right() - 2;
    const qreal tickY = L.meta.center().y() - 4.5;
    if (delivered || read) {
        // Two overlapping ticks, second shifted right like Telegram's.
        drawTick(QPointF(ticksRight - 15, tickY));
        drawTick(QPointF(ticksRight - 9,  tickY));
    } else {
        drawTick(QPointF(ticksRight - 9, tickY));
    }
    painter->restore();
}

QSize MessageBubbleDelegate::sizeHint(const QStyleOptionViewItem &option,
                                      const QModelIndex &index) const {
    const Layout L = computeLayout(option, index);
    return QSize(option.rect.width(), L.bubble.height() + kMargin);
}
