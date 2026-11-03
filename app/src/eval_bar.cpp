// The evaluation bar (eval_bar.hpp).
#include "eval_bar.hpp"

#include <QColor>
#include <QPainter>
#include <QRectF>
#include <QSize>
#include <QSizePolicy>
#include <cmath>

namespace chesscoach::app {

EvalBar::EvalBar(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("eval_bar"));
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    hide();
}

double EvalBar::white_share_of_cp(qint64 cp) {
    return 1.0 / (1.0 + std::exp(-0.00368208 * static_cast<double>(cp)));
}

void EvalBar::set_evaluation(std::optional<qint64> cp, std::optional<qint64> mate) {
    if (mate) {
        white_share_ = *mate > 0 ? 1.0 : 0.0;
    } else if (cp) {
        white_share_ = white_share_of_cp(*cp);
    } else {
        white_share_.reset();
    }
    setVisible(white_share_.has_value());
    update();
}

void EvalBar::set_flipped(bool flipped) {
    flipped_ = flipped;
    update();
}

QSize EvalBar::sizeHint() const { return {18, 240}; }

void EvalBar::paintEvent(QPaintEvent* /*event*/) {
    if (!white_share_) {
        return;
    }
    QPainter painter(this);
    const QRectF all = rect();
    const qreal white_height = all.height() * *white_share_;
    painter.fillRect(all, QColor(0x40, 0x40, 0x40));
    const QRectF white =
        flipped_ ? QRectF(all.left(), all.top(), all.width(), white_height)
                 : QRectF(all.left(), all.bottom() - white_height, all.width(), white_height);
    painter.fillRect(white, QColor(0xf4, 0xf4, 0xf4));
    painter.setPen(QColor(0x80, 0x80, 0x80));
    painter.drawRect(all.adjusted(0, 0, -1, -1));
}

} // namespace chesscoach::app
