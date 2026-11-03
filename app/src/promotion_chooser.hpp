// The promotion popup: four buttons, queen, rook, bishop and knight, over the promotion square's
// file, shown by BoardView before a promoting move is played. A child of the board's viewport,
// so it draws with the board and needs no window of its own. Each button carries an action named
// promote_q, promote_r, promote_b or promote_n; triggering one emits chosen with the UCI
// promotion letter. GUI thread only.
#pragma once

#include <QFrame>
#include <array>

class QToolButton;
class QVBoxLayout;

namespace chesscoach::app {

class PieceSet;

class PromotionChooser : public QFrame {
    Q_OBJECT

  public:
    explicit PromotionChooser(QWidget* parent = nullptr);

    // Shows white's or black's pieces in a column of four squares of the given side, the queen
    // at top_left, extending downwards or upwards.
    void show_for(bool white, const PieceSet& pieces, QPoint top_left, int square, bool downwards);

  signals:
    void chosen(char promotion);

  private:
    QVBoxLayout* column_;
    std::array<QToolButton*, 4> buttons_{}; // queen, rook, bishop, knight
};

} // namespace chesscoach::app
