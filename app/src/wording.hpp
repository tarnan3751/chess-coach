// How the app words the things its sentences hold (docs/reference/app.md, **Wording**): a rating
// band, a count with its noun, a number with thousands separators, a move's loss by its two
// evaluations, a day, the months an opening database holds and the engine's state, each written
// one way on every screen, in English (QLocale::English) whatever the system's locale. Pure
// functions of their arguments: no clock, no I/O.
#pragma once

#include <chesscoach/opening_store.hpp>
#include <chesscoach/position.hpp>

#include <QString>
#include <QtGlobal>
#include <cstdint>

namespace chesscoach::app {

enum class EngineState : std::uint8_t; // engine_worker.hpp

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

// A move's evaluations: docs/database.md §7.2's values, in centipawns from the mover's side, of the
// position before it and of the one after it. A mate for the mover is worth about +99,999 and one
// against about -99,999, so the difference alone cannot tell a lost game from a missed win.
struct MoveEvaluations {
    qint64 before = 0;
    qint64 after = 0;
};

// The evaluations of a move by mover from the store's rows of the two positions, through core's
// evaluation_value; both rows must be evaluated, as the move's loss needs.
[[nodiscard]] MoveEvaluations move_evaluations(Color mover, const PositionRow& before,
                                               const PositionRow& after);

// A move's loss as a sentence, chosen by its two evaluations: a mate against the mover after it,
// "The engine says it loses by force."; a mate for the mover both before and after it, however
// much slower, "The engine says it keeps the forced win."; a mate for the mover before it and none
// after, "The engine says it misses a forced win."; else the loss, before less after, "The engine
// says it gives away about 1.1 pawns." ("about 1 pawn" when it rounds to 1.0), or "almost
// nothing" under 5, however large the loss: a move from +12 to +4 still wins.
[[nodiscard]] QString loss_sentence(const MoveEvaluations& evaluations);

// The same cases in a table's cell: "loses by force", "keeps the win", "misses a win", "1.1",
// "almost none".
[[nodiscard]] QString loss_cell(const MoveEvaluations& evaluations);

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

// The engine's state, name being its own when it is ready or searching: "Stockfish 17 ready",
// "Stockfish 17 thinking…", "Engine starting…", "Engine not started", "No engine found", "The
// engine did not start". With the way forward, the last two say what to do: "No engine found.
// Choose a Stockfish program in Settings."
[[nodiscard]] QString engine_words(EngineState state, const QString& name, bool with_way_forward);

} // namespace chesscoach::app
