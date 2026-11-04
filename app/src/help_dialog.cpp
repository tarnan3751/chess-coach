// The How to use dialog and the toolbar's tooltips (help_dialog.hpp).
#include "help_dialog.hpp"

#include <QAction>
#include <QDialogButtonBox>
#include <QStringList>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QWidget>

namespace chesscoach::app {

HelpDialog::HelpDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("help_dialog"));
    setWindowTitle(tr("How to use chess-coach"));
    auto* browser = new QTextBrowser(this);
    browser->setObjectName(QStringLiteral("help_text"));
    // Nothing in the text links anywhere, and nothing would open if it did.
    browser->setOpenLinks(false);
    browser->setOpenExternalLinks(false);
    browser->setHtml(text());
    browser->setMinimumSize(480, 360);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* column = new QVBoxLayout(this);
    column->addWidget(browser, 1);
    column->addWidget(buttons);
    resize(600, 620); // the five sections without a scroll bar
}

QString HelpDialog::text() {
    const QStringList sections{
        tr("Explore"),
        tr("The Explore tab shows the moves that players at your rating chose in this position, "
           "how often, and how their games ended. Play a move on the board, or double-click a "
           "row, to go deeper. Choose your rating in the Rating menu at the top. Under the table, "
           "the engine shows its best lines."),
        tr("Practice"),
        tr("Press Start a practice game and play an opening against the coach. The coach answers "
           "with moves that players at your rating really play. When you play a Dubious move or "
           "leave the book (the moves the database knows at your rating), the coach stops you and "
           "shows better choices. Once the game leaves the book, the engine plays the coach's "
           "moves."),
        tr("Repertoire"),
        tr("Your repertoire is the move you will play in each position, one for White and one for "
           "Black. Play your move on the board to save it, and play the opponent's replies to go "
           "on. Coverage shows how much of what opponents at your rating play you have an answer "
           "to. The positions with no answer yet show what to prepare next."),
        tr("Drill"),
        tr("Every move in your repertoire becomes a card. Press Start the drill, then play your "
           "move in each position the cards show. A card you answer right comes back after a "
           "longer wait, and one you miss comes back soon."),
        tr("The database and the engine"),
        tr("The opening database is built from rated games played on Lichess, grouped into rating "
           "bands such as 1600–1799 by the players' average rating. The Rating menu picks the band "
           "you see. Each move can carry the badges Book (the move strong players choose), Popular "
           "(often played at this rating) and Dubious (popular here, but it loses ground). The "
           "engine, Stockfish, runs on your computer, and nothing you do leaves it."),
    };
    QString html;
    for (qsizetype i = 0; i + 1 < sections.size(); i += 2) {
        html += QStringLiteral("<h3>%1</h3><p>%2</p>")
                    .arg(sections.at(i).toHtmlEscaped(), sections.at(i + 1).toHtmlEscaped());
    }
    return html;
}

void add_toolbar_tooltips(const QWidget& window) {
    window.findChild<QAction*>(QStringLiteral("new_game_action"))
        ->setToolTip(HelpDialog::tr("Go back to the starting position."));
    window.findChild<QAction*>(QStringLiteral("back_action"))
        ->setToolTip(HelpDialog::tr("Take back the last move."));
    window.findChild<QAction*>(QStringLiteral("flip_action"))
        ->setToolTip(HelpDialog::tr("Turn the board around."));
    window.findChild<QWidget*>(QStringLiteral("tier_selector"))
        ->setToolTip(HelpDialog::tr("Choose the rating of the players whose games you see."));
}

} // namespace chesscoach::app
