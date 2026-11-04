// The Explore screen (docs/reference/app.md, **Explore**), in the Study look: a line saying what
// the screen is for, the position's header (the ECO code in a chip before the opening's name, and
// a line with the side to move, the games at the tier and the stored evaluation), the table of
// moves at the selected tier or, when the position is off book there, a label in place of the
// table, and the engine's panel under them (its name, the depth and the lines, or what it is
// doing). Activating a row (double-click
// or Enter) asks for its move to be played on the board. It shows what the database worker sent
// and queries nothing itself. It paints in the StudyPalette it is given (set_palette), the light
// one until then. GUI thread only.
#pragma once

#include "engine_worker.hpp"
#include "explore_data.hpp"
#include "study_palette.hpp"

#include <QString>
#include <QWidget>

class QFrame;
class QLabel;
class QListWidget;
class QStackedWidget;

namespace chesscoach::app {

class ExploreModel;
class MoveTable;

// A score from white's side as text: "+0.3" (centipawns as pawns), "mate in 3" (white mates) or
// "mated in 3" (white is mated).
[[nodiscard]] QString score_text(std::optional<qint64> cp, std::optional<qint64> mate);
// The line under the position's name, the tier named by its rating band (wording.hpp's
// rating_band): "White to move · 1,000 games by players rated 1600–1799 · the database's
// evaluation +0.2 for White", or off book "Black to move · too few games by players rated
// 1600–1799 to show". The side to move is left out when the position has no stored FEN, and the
// evaluation when it has none.
[[nodiscard]] QString position_summary(const ExploreData& explore, const QString& band);
// An engine line's score as score_text writes it; a checkmated side to move reads "mate" when
// black is mated and "mated" when white is.
[[nodiscard]] QString engine_score_text(const EngineLine& line);
// An engine line as the panel lists it, its score then its moves: "-0.3 Nc6 Bb5 Nf6".
[[nodiscard]] QString engine_line_text(const EngineLine& line);

class ExploreScreen : public QWidget {
    Q_OBJECT

  public:
    explicit ExploreScreen(QWidget* parent = nullptr);

    // The theme's colours, in which the header, the table and the engine's panel repaint.
    void set_palette(const StudyPalette& palette);
    [[nodiscard]] const StudyPalette& study_palette() const { return palette_; }

    // Shows a position at a tier, the tier named by its rating band (wording.hpp's rating_band).
    void show_data(const ExploreData& explore, const QString& band);
    // The empty state, without a database; message says why, else a plain one.
    void show_no_database(const QString& message = QString());

    // The engine's panel, headed by the engine's name ("Engine" until it has given one). Its
    // lines, the head giving their depth and count, "from an earlier search" when they came from
    // the cache. The engine's state (EngineWorker::engine_state; name is the engine's own when it
    // is ready or searching), said in the panel's body in wording.hpp's engine_words with the way
    // forward, which clears the lines. A failure the worker reports, said in the body, and kept
    // above the lines that follow until the next state.
    void show_engine_lines(const QVector<EngineLine>& lines, bool cached);
    void show_engine_state(EngineState state, const QString& name);
    void show_engine_failure(const QString& message);

    [[nodiscard]] const ExploreModel& model() const { return *model_; }

  signals:
    void move_requested(const QString& uci);

  private:
    [[nodiscard]] QString engine_title() const;
    // Sizes the engine's list to the lines it holds, and hides it when there are none.
    void fit_engine_lines();
    // Says a state or a failure in the engine's panel, in place of its lines.
    void say_in_engine_panel(const QString& text);

    StudyPalette palette_;
    ExploreModel* model_;
    QLabel* eco_;
    QLabel* name_;
    QLabel* summary_;
    QStackedWidget* content_;
    MoveTable* table_;
    QLabel* message_;
    QFrame* engine_panel_;
    QLabel* engine_label_;
    QLabel* engine_detail_;
    QLabel* engine_message_;
    QListWidget* engine_lines_;
    QString engine_name_;
    bool failure_said_ = false;
};

} // namespace chesscoach::app
