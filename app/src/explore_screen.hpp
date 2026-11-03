// The Explore screen (docs/reference/app.md, **Explore**): the opening name, the stored
// evaluation and the node's games above the table of moves at the selected tier, or, when the
// position is off book there, a label in place of the table. Activating a row (double-click or
// Enter) asks for its move to be played on the board. It shows what the database worker sent and
// queries nothing itself. GUI thread only.
#pragma once

#include "engine_worker.hpp"
#include "explore_data.hpp"

#include <QString>
#include <QWidget>

class QLabel;
class QListWidget;
class QStackedWidget;
class QTableView;

namespace chesscoach::app {

class ExploreModel;

// The stored evaluation as text: "+0.3" (centipawns as pawns, from white's side), "M3" or "-M3"
// (mate for white or for black), or "no evaluation".
[[nodiscard]] QString evaluation_text(const std::optional<PositionRow>& position);
// A score from white's side as text, as evaluation_text writes it.
[[nodiscard]] QString score_text(std::optional<qint64> cp, std::optional<qint64> mate);
// An engine line as Explore lists it: "1: depth 18 -0.3 Nc6 Bb5 Nf6".
[[nodiscard]] QString engine_line_text(const EngineLine& line);

class ExploreScreen : public QWidget {
    Q_OBJECT

  public:
    explicit ExploreScreen(QWidget* parent = nullptr);

    // Shows a position at a tier, the tier named by its label.
    void show_data(const ExploreData& explore, const QString& tier_label);
    // The empty state, without a database; message says why, else a plain one.
    void show_no_database(const QString& message = QString());

    // The engine's part, under the table: its lines, marked when they came from the cache; that
    // it is searching, which clears them; or another state ("not found") or failure.
    void show_engine_lines(const QVector<EngineLine>& lines, bool cached);
    void show_engine_searching();
    void show_engine_state(const QString& text);

    [[nodiscard]] const ExploreModel& model() const { return *model_; }

  signals:
    void move_requested(const QString& uci);

  private:
    ExploreModel* model_;
    QLabel* name_;
    QLabel* evaluation_;
    QLabel* games_;
    QStackedWidget* content_;
    QTableView* table_;
    QLabel* message_;
    QLabel* engine_label_;
    QListWidget* engine_lines_;
};

} // namespace chesscoach::app
