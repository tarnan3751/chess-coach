// The evaluation bar beside the board: white's share of a vertical bar from the stored
// evaluation of the position (docs/database.md §6.2), empty when there is none. A centipawn
// score maps to white's winning chances, 1 / (1 + e^(-0.00368208 * cp)), the curve lichess
// draws its bar with; a mate fills the bar for the mating side. White is at the bottom, or at
// the top when the board is flipped. In the Study look (docs/reference/app.md, **Themes**) it is
// 14 px wide, black's share #3a3a3a and white's #f4f4f4 in both themes, in a hairline frame of
// the theme's, with no text; empty, only the frame shows. GUI thread only.
#pragma once

#include <chesscoach/position.hpp>

#include "study_palette.hpp"

#include <QColor>
#include <QWidget>
#include <optional>

namespace chesscoach::app {

class EvalBar : public QWidget {
    Q_OBJECT

  public:
    explicit EvalBar(QWidget* parent = nullptr);

    // Shows the evaluation (centipawns or mate in n, from white's side, n never 0), or a
    // checkmated side, whose opponent fills the bar; with none of them the bar is empty, its
    // width kept so that the board beside it does not move.
    void set_evaluation(std::optional<qint64> cp, std::optional<qint64> mate,
                        std::optional<Color> mated = std::nullopt);
    void set_flipped(bool flipped);
    // The theme's hairline, which frames the bar.
    void set_palette(const StudyPalette& palette);

    // White's share of the bar, 0 to 1, or nullopt when it is empty.
    [[nodiscard]] std::optional<double> white_share() const { return white_share_; }
    [[nodiscard]] static double white_share_of_cp(qint64 cp);

    [[nodiscard]] QSize sizeHint() const override;

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    std::optional<double> white_share_;
    bool flipped_ = false;
    QColor frame_ = StudyPalette::light().border;
};

} // namespace chesscoach::app
