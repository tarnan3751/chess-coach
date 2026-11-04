// The Rating chooser (rating_chooser.hpp).
#include "rating_chooser.hpp"

#include "icons.hpp"

#include <QColor>
#include <QEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QRect>
#include <QStyle>
#include <QStyleOptionComboBox>

namespace chesscoach::app {

void RatingChooser::changeEvent(QEvent* event) {
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange) {
        const QColor muted = palette().color(QPalette::PlaceholderText);
        chevron_ = tinted_icon(QStringLiteral("chevron-down"), muted, muted);
    }
    QComboBox::changeEvent(event);
}

void RatingChooser::paintEvent(QPaintEvent* event) {
    QComboBox::paintEvent(event);
    QStyleOptionComboBox option;
    initStyleOption(&option);
    QRect chevron(0, 0, 16, 16);
    chevron.moveCenter(
        style()
            ->subControlRect(QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxArrow, this)
            .center());
    QPainter painter(this);
    chevron_.paint(&painter, chevron);
}

} // namespace chesscoach::app
