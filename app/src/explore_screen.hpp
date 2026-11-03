// The Explore screen (docs/reference/app.md, **Explore**): the opening name, the stored
// evaluation and the node's games above the table of moves at the selected tier, or, when the
// position is off book there, a label in place of the table. Activating a row (double-click or
// Enter) asks for its move to be played on the board. It shows what the database worker sent and
// queries nothing itself. GUI thread only.
#pragma once

#include "explore_data.hpp"

#include <QString>
#include <QWidget>

class QLabel;
class QStackedWidget;
class QTableView;

namespace chesscoach::app {

class ExploreModel;

// The stored evaluation as text: "+0.3" (centipawns as pawns, from white's side), "M3" or "-M3"
// (mate for white or for black), or "no evaluation".
[[nodiscard]] QString evaluation_text(const std::optional<PositionRow>& position);

class ExploreScreen : public QWidget {
    Q_OBJECT

  public:
    explicit ExploreScreen(QWidget* parent = nullptr);

    // Shows a position at a tier, the tier named by its label.
    void show_data(const ExploreData& explore, const QString& tier_label);
    // The empty state, without a database.
    void show_no_database();

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
};

} // namespace chesscoach::app
