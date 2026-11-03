// The board: a QGraphicsView over a BoardScene that shows a position and takes moves by two
// clicks or by drag and drop (docs/reference/app.md, **Board**). Hit testing is ours: a press,
// a drag and a release are mapped from the viewport to scene coordinates and from there to a
// square by BoardScene::square_at, honouring the flip. A legal move is played on the view's own
// copy of the position through core's Position, then announced by move_played; the view never
// decides legality itself. Picking a piece shows its targets; a played move animates over
// 150 ms, except a dropped piece, which is already there; set_position replaces the position
// without animation. GUI thread only.
#pragma once

#include <chesscoach/position.hpp>

#include "board_theme.hpp"
#include "squares.hpp"

#include <QGraphicsView>
#include <QPointF>
#include <QPointer>
#include <QString>
#include <optional>
#include <string>
#include <vector>

class QVariantAnimation;

namespace chesscoach::app {

class BoardScene;
class PromotionChooser;

class BoardView : public QGraphicsView {
    Q_OBJECT

  public:
    static constexpr int animation_ms = 150;

    explicit BoardView(QWidget* parent = nullptr, const BoardTheme& theme = BoardTheme::light());

    // Shows the position, without animation, with no piece picked and no last move.
    void set_position(const Position& position);
    [[nodiscard]] const Position& position() const { return position_; }

    // Plays a legal move given in UCI as if it had been clicked: animated, then announced by
    // move_played. Returns false, and plays nothing, for any other text.
    bool play_move(const QString& uci);

    void set_theme(const BoardTheme& theme);

    // Turns the board round: black at the bottom, or white again.
    void flip();
    [[nodiscard]] bool flipped() const;

    [[nodiscard]] const BoardScene& board_scene() const { return *scene_; }
    // Whether a move is still animating.
    [[nodiscard]] bool animating() const;

  signals:
    // A legal move was played on the board: its UCI and its SAN in the position before it.
    void move_played(const QString& uci, const QString& san);

  protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

  private:
    struct Drag {
        int from = 0;
        QPointF press;      // where the press was, in scene coordinates
        bool moved = false; // the pointer travelled far enough to be a drag
    };
    struct PendingPromotion {
        int from = 0;
        int to = 0;
        bool animate = true;
    };

    [[nodiscard]] std::vector<std::string> legal_moves_from(int square) const;
    [[nodiscard]] bool is_target(int from, int to) const;
    [[nodiscard]] bool own_piece_on(int square) const;
    void select(std::optional<int> square);
    // Plays from-to, asking for the promotion piece first when the move promotes.
    void try_move(int from, int to, bool animate);
    void play(const std::string& uci, bool animate);
    void animate_piece(int from, int to);
    void stop_animations();
    void refresh();
    void cancel_promotion();

    BoardScene* scene_;
    PromotionChooser* chooser_;
    Position position_;
    std::optional<int> selected_;
    std::optional<Drag> drag_;
    std::optional<PendingPromotion> promotion_;
    std::optional<std::pair<int, int>> last_move_;
    std::vector<QPointer<QVariantAnimation>> animations_;
};

} // namespace chesscoach::app
