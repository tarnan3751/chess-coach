// The How to use dialog and the toolbar's tooltips (help_dialog.hpp).
#include "help_dialog.hpp"

#include "fonts.hpp"

#include <QAction>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QStringList>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QWidget>
#include <array>
#include <utility>

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
    resize(640, 720); // most of the text at once; the browser scrolls for the rest
}

QString HelpDialog::text() {
    const QStringList sections{
        tr("Explore"),
        tr("The Explore tab shows the moves that players at your rating chose in this position, "
           "how often, and how their games ended. Each move's bar splits those games into White "
           "wins, draws and Black wins. Play a move on the board, or double-click a row, to go "
           "deeper, and choose your rating in the Rating menu at the top. Under the table, the "
           "engine shows its best lines, scored in pawns, plus when White is better and minus "
           "when Black is."),
        tr("Practice"),
        tr("Press Start a practice game and play an opening against the coach, which answers with "
           "moves that players at your rating really play. The known lines are the moves played "
           "often enough at your rating to be in the database. When you leave them, or play a "
           "Dubious move, one that players at your rating often choose but that loses ground, the "
           "coach stops you and shows better choices. Once the game leaves the known lines, the "
           "engine plays the coach's moves."),
        tr("Repertoire"),
        tr("Your repertoire is the set of moves you will play, one for each position you meet, "
           "kept "
           "separately for White and for Black. Play your move on the board to save it, and play "
           "the opponent's replies to go on. Coverage shows how much of what opponents at your "
           "rating play you have an answer "
           "to. The positions with no answer yet show what to prepare next."),
        tr("Drill"),
        tr("Every move in your repertoire becomes a card. Press Start the drill, then play your "
           "move in each position the cards show, and after a right answer say how hard it was to "
           "find. A card you answer right comes back after a longer wait, and one you miss comes "
           "back tomorrow."),
        tr("The database and the engine"),
        tr("The opening database is built from rated games played on Lichess, grouped into rating "
           "bands such as 1600–1799 by the players' average rating, and the Rating menu picks the "
           "band you see. Each move can carry the badges Book (what strong players, rated 2200 and "
           "above, choose), Popular (often chosen at your rating) and Dubious (popular at your "
           "rating, but it loses ground). The engine, Stockfish, runs on your computer, and "
           "nothing you do leaves it. File > Back up your work saves everything you have made, "
           "your repertoires, cards and practice games, in one file."),
    };
    // The headings in Source Serif 4 SemiBold, the spec's heading face, when install_fonts has
    // registered it; else in the document's font.
    const QString heading =
        QFontDatabase::hasFamily(serif_family)
            ? QStringLiteral("<h3 style=\"font-family: '%1'; font-weight: 600\">").arg(serif_family)
            : QStringLiteral("<h3>");
    QString html;
    for (qsizetype i = 0; i + 1 < sections.size(); i += 2) {
        html += heading + sections.at(i).toHtmlEscaped() + QStringLiteral("</h3><p>") +
                sections.at(i + 1).toHtmlEscaped() + QStringLiteral("</p>");
    }
    return html;
}

void add_toolbar_tooltips(const QWidget& window) {
    // A control the toolbar does not have, or no longer has, gets none.
    const std::array actions{
        std::pair{QStringLiteral("new_game_action"),
                  HelpDialog::tr("Go back to the starting position.")},
        std::pair{QStringLiteral("back_action"), HelpDialog::tr("Take back the last move.")},
        std::pair{QStringLiteral("flip_action"), HelpDialog::tr("Turn the board around.")},
    };
    for (const auto& [name, tip] : actions) {
        if (auto* action = window.findChild<QAction*>(name)) {
            action->setToolTip(tip);
        }
    }
    if (auto* rating = window.findChild<QWidget*>(QStringLiteral("tier_selector"))) {
        rating->setToolTip(HelpDialog::tr("Choose the rating of the players whose games you see."));
    }
}

} // namespace chesscoach::app
