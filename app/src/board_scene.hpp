// The board's scene: 64 square items, the file and rank coordinates, a QGraphicsSvgItem per
// piece sharing its type's renderer, and the highlight and hint items, laid out in scene units
// of square_size per square with a8 (h1 when flipped) at the origin. It draws what BoardView
// tells it and maps scene points to squares; it holds no position and plays no move
// (docs/reference/app.md, **Board**). GUI thread only.
#pragma once

#include "board_theme.hpp"
#include "squares.hpp"

#include <QGraphicsScene>
#include <QPointF>
#include <QRectF>
#include <array>
#include <optional>
#include <utility>
#include <vector>

class QGraphicsEllipseItem;
class QGraphicsRectItem;
class QGraphicsSimpleTextItem;
class QGraphicsSvgItem;

namespace chesscoach::app {

class PieceSet;

class BoardScene : public QGraphicsScene {
    Q_OBJECT

  public:
    static constexpr qreal square_size = 100;
    // The stacking order of the items: squares, highlights, coordinates, pieces, hints, and a
    // piece being dragged above them all.
    static constexpr qreal square_z = 0;
    static constexpr qreal highlight_z = 1;
    static constexpr qreal coordinate_z = 2;
    static constexpr qreal piece_z = 3;
    static constexpr qreal hint_z = 4;
    static constexpr qreal dragged_z = 10;

    explicit BoardScene(const BoardTheme& theme, QObject* parent = nullptr);

    [[nodiscard]] const BoardTheme& theme() const { return theme_; }
    // Recolours the squares, highlights, hints and coordinates.
    void set_theme(const BoardTheme& theme);

    [[nodiscard]] bool flipped() const { return flipped_; }
    // Lays every item out again with white at the top (flipped) or at the bottom.
    void set_flipped(bool flipped);

    // The square's rectangle in scene coordinates, honouring the flip.
    [[nodiscard]] QRectF square_rect(int square) const;
    // The square under a scene point, honouring the flip, or nullopt off the board.
    [[nodiscard]] std::optional<int> square_at(QPointF point) const;

    // Replaces every piece item by one per occupied square of the placement.
    void set_placement(const Placement& placement);
    // The item of the piece drawn on a square, or nullptr when it is empty.
    [[nodiscard]] QGraphicsSvgItem* piece_item(int square) const;
    // Every piece item, in square order.
    [[nodiscard]] std::vector<QGraphicsSvgItem*> piece_items() const;
    // The FEN letter an item draws.
    [[nodiscard]] static char piece_of(const QGraphicsSvgItem* item);

    void set_last_move(std::optional<std::pair<int, int>> move);
    void set_check(std::optional<int> king);
    // The picked piece's square, and its targets: dots on empty squares, rings on captures.
    void set_selection(std::optional<int> square, const std::vector<int>& quiet_targets,
                       const std::vector<int>& capture_targets);

    [[nodiscard]] std::optional<std::pair<int, int>> last_move() const { return last_move_; }
    [[nodiscard]] std::optional<int> check_square() const { return check_; }
    [[nodiscard]] std::optional<int> selected_square() const { return selected_; }
    // The squares carrying a hint, quiet targets then captures, each in the order given.
    [[nodiscard]] std::vector<int> hint_squares() const;

    // The coordinate labels: the file letter shown along the bottom edge, the rank digit along
    // the left edge, wherever the flip puts them.
    [[nodiscard]] const QGraphicsSimpleTextItem* file_label(int file) const;
    [[nodiscard]] const QGraphicsSimpleTextItem* rank_label(int rank) const;

    [[nodiscard]] const PieceSet& pieces() const { return *pieces_; }

  private:
    void layout();
    void place_highlight(QGraphicsRectItem* item, std::optional<int> square) const;

    BoardTheme theme_;
    PieceSet* pieces_;
    bool flipped_ = false;
    std::array<QGraphicsRectItem*, square_count> squares_{};
    std::array<QGraphicsSimpleTextItem*, 8> file_labels_{};
    std::array<QGraphicsSimpleTextItem*, 8> rank_labels_{};
    std::array<QGraphicsSvgItem*, square_count> piece_items_{};
    QGraphicsRectItem* last_from_;
    QGraphicsRectItem* last_to_;
    QGraphicsRectItem* selected_item_;
    QGraphicsRectItem* check_item_;
    std::vector<QGraphicsEllipseItem*> hint_items_;
    std::optional<std::pair<int, int>> last_move_;
    std::optional<int> check_;
    std::optional<int> selected_;
    std::vector<int> hints_;
    std::vector<bool> hint_is_capture_;
};

} // namespace chesscoach::app
