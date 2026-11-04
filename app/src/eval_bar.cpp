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
}

double EvalBar::white_share_of_cp(qint64 cp) {
    return 1.0 / (1.0 + std::exp(-0.00368208 * static_cast<double>(cp)));
}

void EvalBar::set_evaluation(std::optional<qint64> cp, std::optional<qint64> mate,
                             std::optional<Color> mated) {
    if (mated) {
        white_share_ = *mated == Color::white ? 0.0 : 1.0;
    } else if (mate) {
        white_share_ = *mate > 0 ? 1.0 : 0.0;
    } else if (cp) {
        white_share_ = white_share_of_cp(*cp);
    } else {
        white_share_.reset();
    }
    update();
}

void EvalBar::set_flipped(bool flipped) {
    flipped_ = flipped;
    update();
}

void EvalBar::set_palette(const StudyPalette& palette) {
    frame_ = palette.border;
    update();
}

QSize EvalBar::sizeHint() const { return {14, 240}; }

void EvalBar::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    // The shares inside the frame, which is a 1 px hairline drawn last.
    const QRectF all = rect();
    const QRectF inside = all.adjusted(1, 1, -1, -1);
    if (white_share_) {
        const qreal white_height = inside.height() * *white_share_;
        painter.fillRect(inside, QColor(0x3a, 0x3a, 0x3a));
        const QRectF white = flipped_
                                 ? QRectF(inside.left(), inside.top(), inside.width(), white_height)
                                 : QRectF(inside.left(), inside.bottom() - white_height,
                                          inside.width(), white_height);
        painter.fillRect(white, QColor(0xf4, 0xf4, 0xf4));
    }
    painter.setPen(frame_);
    painter.drawRect(all.adjusted(0, 0, -1, -1));
}

} // namespace chesscoach::app
