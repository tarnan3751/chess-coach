// The evaluation bar beside the board: white's share of a vertical bar from the stored
// evaluation of the position (docs/database.md §6.2), hidden when there is none. A centipawn
// score maps to white's winning chances, 1 / (1 + e^(-0.00368208 * cp)), the curve lichess
// draws its bar with; a mate fills the bar for the mating side. White is at the bottom, or at
// the top when the board is flipped. GUI thread only.
#pragma once

#include <QWidget>
#include <optional>

namespace chesscoach::app {

class EvalBar : public QWidget {
    Q_OBJECT

  public:
    explicit EvalBar(QWidget* parent = nullptr);

    // Shows the evaluation (centipawns or mate in n, from white's side); hides the bar when
    // both are absent.
    void set_evaluation(std::optional<qint64> cp, std::optional<qint64> mate);
    void set_flipped(bool flipped);

    // White's share of the bar, 0 to 1, or nullopt when hidden.
    [[nodiscard]] std::optional<double> white_share() const { return white_share_; }
    [[nodiscard]] static double white_share_of_cp(qint64 cp);

    [[nodiscard]] QSize sizeHint() const override;

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    std::optional<double> white_share_;
    bool flipped_ = false;
};

} // namespace chesscoach::app
