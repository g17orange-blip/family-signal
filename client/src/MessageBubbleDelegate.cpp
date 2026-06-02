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
    const QString meta = sentAt.toString(QStringLiteral("HH:mm"))
                       + (outgoing && index.data(ChatModel::DeliveredRole).toBool()
                          ? QStringLiteral("  ✓") : QString());

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
    QString meta = sentAt.toString(QStringLiteral("HH:mm"));
    if (outgoing) meta += delivered ? QStringLiteral("  ✓✓") : QStringLiteral("  ✓");
    painter->drawText(L.meta,
                      outgoing ? Qt::AlignRight : Qt::AlignLeft,
                      meta);
    painter->restore();
}

QSize MessageBubbleDelegate::sizeHint(const QStyleOptionViewItem &option,
                                      const QModelIndex &index) const {
    const Layout L = computeLayout(option, index);
    return QSize(option.rect.width(), L.bubble.height() + kMargin);
}
