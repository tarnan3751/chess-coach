// The move list's rows (move_list_delegate.hpp).
#include "move_list_delegate.hpp"

#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPalette>
#include <QRect>
#include <QString>
#include <QStringList>
#include <Qt>

namespace chesscoach::app {

void MoveListDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                             const QModelIndex& index) const {
    const QStringList parts = index.data().toString().split(QLatin1Char(' '));
    const QFont& font = option.font;
    const QFontMetrics metrics(font);
    const int number_width = metrics.horizontalAdvance(QStringLiteral("000."));
    const int move_width = metrics.horizontalAdvance(QStringLiteral("Qxe8=Q+"));
    const bool last_row = index.row() == index.model()->rowCount() - 1;
    painter->save();
    painter->setFont(font);
    QRect cell = option.rect.adjusted(move_padding, 0, 0, 0);
    cell.setWidth(number_width);
    painter->setPen(option.palette.color(QPalette::PlaceholderText));
    painter->drawText(cell, Qt::AlignRight | Qt::AlignVCenter, parts.value(0));
    for (qsizetype move = 1; move < parts.size(); ++move) {
        const int left = option.rect.left() + move_padding + number_width + move_gap +
                         ((static_cast<int>(move) - 1) * (move_width + move_gap));
        const QRect text(left, option.rect.top(), move_width, option.rect.height());
        if (last_row && move == parts.size() - 1) {
            const int width = metrics.horizontalAdvance(parts.at(move));
            const QRect mark(left - mark_padding, option.rect.top() + 1, width + (2 * mark_padding),
                             option.rect.height() - 2);
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(Qt::NoPen);
            painter->setBrush(option.palette.color(QPalette::Highlight));
            painter->drawRoundedRect(mark, 2, 2);
        }
        painter->setPen(option.palette.color(QPalette::Text));
        painter->drawText(text, Qt::AlignLeft | Qt::AlignVCenter, parts.at(move));
    }
    painter->restore();
}

QSize MoveListDelegate::sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const {
    QSize size = QStyledItemDelegate::sizeHint(option, index);
    size.setHeight(QFontMetrics(option.font).height() + 6);
    return size;
}

} // namespace chesscoach::app
