// How the app words the things its sentences hold (docs/reference/app.md, **Wording**): a rating
// band, a count with its noun, a number with thousands separators, a loss in pawns or in words, a
// day and the months an opening database holds, each written one way on every screen, in English
// (QLocale::English) whatever the system's locale. Pure functions of their arguments: no clock,
// no I/O.
#pragma once

#include <QString>
#include <QtGlobal>

namespace chesscoach::app {

// The tier's rating band: "under 1200", "1200–1399" to "2000–2199", "2200 and above"; written to
// read after "players rated" and in the Rating menu.
[[nodiscard]] QString rating_band(int tier);

// n with thousands separators: "173,722".
[[nodiscard]] QString number(qint64 n);

// one when n is 1, else many, with n put in place of %1: counted(2, tr("%1 card"),
// tr("%1 cards")) is "2 cards".
[[nodiscard]] QString counted(qint64 n, const QString& one, const QString& many);

// Centipawns as pawns rounded to one decimal: 109 is "1.1", 95 is "1.0".
[[nodiscard]] QString pawns(qint64 centipawns);

// A move's loss in centipawns from the mover's side, as a sentence: under 5, "The engine says it
// gives away almost nothing."; from 5 to 999, "The engine says it gives away about 1.1 pawns.", or
// "about 1 pawn" when it rounds to 1.0; from 1,000, as a forced mate reads, "The engine says it
// loses by force."
[[nodiscard]] QString loss_sentence(qint64 centipawns);

// The same three cases in a table's cell: "almost none", "1.1", "loses by force".
[[nodiscard]] QString loss_cell(qint64 centipawns);

// A depth in plies as a game counts moves, white's and black's together, rounded up: 20 plies are
// 10 moves, 21 are 11.
[[nodiscard]] int moves_from_plies(int plies);

// The day of a time in Unix seconds, in local time: "4 November 2026".
[[nodiscard]] QString day(qint64 seconds);

// The months of an opening database's meta (YYYY-MM, ascending, comma-separated): "October 2026";
// "October 2026 to March 2027" for a run of months one after another; each listed, the year after
// a year's last, when there are gaps: "January, March and April 2013", "November 2026, January and
// March 2027". Empty for none, and as stored when one does not read as a month.
[[nodiscard]] QString month_range(const QString& stored);

} // namespace chesscoach::app
