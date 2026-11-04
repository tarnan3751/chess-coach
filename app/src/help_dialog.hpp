// The help the app gives beyond its screens' own words (docs/reference/app.md, **Help**): Help >
// How to use chess-coach, a dialog of five short sections in rich text (Explore, Practice,
// Repertoire, Drill, and the database and the engine) with no link out of the app, and the
// toolbar's tooltips, one sentence each. GUI thread only.
#pragma once

#include <QDialog>
#include <QString>

class QWidget;

namespace chesscoach::app {

class HelpDialog : public QDialog {
    Q_OBJECT

  public:
    explicit HelpDialog(QWidget* parent = nullptr);

    // The five sections as rich text: a heading and a paragraph each.
    [[nodiscard]] static QString text();
};

// A one-sentence tooltip on New game, Back, Flip board and the Rating menu, which it finds in
// window by their object names; the toolbar's setup calls it once.
void add_toolbar_tooltips(const QWidget& window);

} // namespace chesscoach::app
