// The result bar (result_delegate.hpp).
#include "result_delegate.hpp"

#include <QColor>
#include <QList>
#include <QPainter>
#include <QRectF>
#include <QString>
#include <array>
#include <cstddef>

namespace chesscoach::app {

void ResultDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                           const QModelIndex& index) const {
    const auto shares = index.data(shares_role).value<QList<double>>();
    if (shares.size() != 3) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }
    constexpr std::array fills{
        QColor(0xf4, 0xf4, 0xf4),
        QColor(0xa0, 0xa0, 0xa0),
        QColor(0x30, 0x30, 0x30),
    };
    constexpr std::array texts{
        QColor(0x20, 0x20, 0x20),
        QColor(0x10, 0x10, 0x10),
        QColor(0xf0, 0xf0, 0xf0),
    };
    const QRectF bar = QRectF(option.rect).adjusted(2, 3, -2, -3);
    painter->save();
    qreal left = bar.left();
    for (std::size_t i = 0; i < fills.size(); ++i) {
        const double share = shares.at(static_cast<qsizetype>(i));
        const QRectF part(left, bar.top(), bar.width() * share, bar.height());
        painter->fillRect(part, fills.at(i));
        const QString label = QString::number(qRound(100 * share)) + QLatin1Char('%');
        if (painter->fontMetrics().horizontalAdvance(label) + 4 < part.width()) {
            painter->setPen(texts.at(i));
            painter->drawText(part, Qt::AlignCenter, label);
        }
        left += part.width();
    }
    painter->setPen(QColor(0x80, 0x80, 0x80));
    painter->drawRect(bar);
    painter->restore();
}

} // namespace chesscoach::app
