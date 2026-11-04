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

// A score from white's side as text: "+0.3" (centipawns as pawns), "M3" or "-M3" (a mate for
// white or for black).
[[nodiscard]] QString score_text(std::optional<qint64> cp, std::optional<qint64> mate);
// The line under the position's name, the tier named by its rating band (wording.hpp's
// rating_band): "White to move · 1,000 games by players rated 1600–1799 · the database's
// evaluation +0.2 at depth 40". The side to move is left out when the position has no stored
// FEN, and the evaluation when it has none.
[[nodiscard]] QString position_summary(const ExploreData& explore, const QString& band);
// An engine line's score as score_text writes it; a checkmated side to move reads as a mate in 0
// for the other side, "M0" when black is mated and "-M0" when white is.
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

    // The engine's panel: its lines, the head giving their depth and count, "(cached)" when they
    // came from the cache; that it is thinking, which clears them; or another state ("not
    // found", "failed to start") or a failure, in the head. The engine's name heads the panel:
    // the worker reports it as the state that ends a search (EngineWorker::engine_state), the
    // one that follows show_engine_searching; "Engine" until it has.
    void show_engine_lines(const QVector<EngineLine>& lines, bool cached);
    void show_engine_searching();
    void show_engine_state(const QString& text);

    [[nodiscard]] const ExploreModel& model() const { return *model_; }

  signals:
    void move_requested(const QString& uci);

  private:
    [[nodiscard]] QString engine_title() const;
    // Keeps room in the engine's panel for the lines shown, three at least, never shrinking, so
    // that the table above does not move while the engine thinks.
    void fit_engine_lines();

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
    QListWidget* engine_lines_;
    QString engine_name_;
    bool searching_ = false;
    int engine_rows_ = 0;
};

} // namespace chesscoach::app
