// Unit tests of the app's wording (app; unit; wording.hpp): the rating bands from core's bounds,
// numbers with separators and counts with their nouns, the pawns rounded in integers and the cases
// of a loss by its two evaluations, a depth in moves, a day, a database's months as one month, a
// run, a list with gaps or as stored when one does not read, and the engine's state, short and
// with the way forward. Pure functions: no application object is needed.
#include "wording.hpp"

#include <chesscoach/opening_store.hpp>
#include <chesscoach/position.hpp>

#include "engine_worker.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QTime>

using chesscoach::app::counted;
using chesscoach::app::day;
using chesscoach::app::engine_words;
using chesscoach::app::EngineState;
using chesscoach::app::loss_cell;
using chesscoach::app::loss_sentence;
using chesscoach::app::month_range;
using chesscoach::app::moves_from_plies;
using chesscoach::app::number;
using chesscoach::app::pawns;
using chesscoach::app::rating_band;

TEST_CASE("Rating bands read after \"players rated\"", "[wording]") {
    CHECK(rating_band(0) == QStringLiteral("under 1200"));
    CHECK(rating_band(1) == QStringLiteral("1200–1399"));
    CHECK(rating_band(3) == QStringLiteral("1600–1799"));
    CHECK(rating_band(5) == QStringLiteral("2000–2199"));
    CHECK(rating_band(6) == QStringLiteral("2200 and above"));
}

TEST_CASE("Numbers carry thousands separators and counts their noun", "[wording]") {
    CHECK(number(0) == QStringLiteral("0"));
    CHECK(number(173722) == QStringLiteral("173,722"));
    CHECK(counted(1, QStringLiteral("%1 card"), QStringLiteral("%1 cards")) ==
          QStringLiteral("1 card"));
    CHECK(counted(0, QStringLiteral("%1 card"), QStringLiteral("%1 cards")) ==
          QStringLiteral("0 cards"));
    CHECK(counted(1200, QStringLiteral("%1 card"), QStringLiteral("%1 cards")) ==
          QStringLiteral("1,200 cards"));
}

TEST_CASE("Pawns round to one decimal in integers", "[wording]") {
    CHECK(pawns(0) == QStringLiteral("0.0"));
    CHECK(pawns(94) == QStringLiteral("0.9"));
    CHECK(pawns(95) == QStringLiteral("1.0"));
    CHECK(pawns(109) == QStringLiteral("1.1"));
    CHECK(pawns(-109) == QStringLiteral("-1.1"));
}

TEST_CASE("A loss reads by the two evaluations, not the difference alone", "[wording]") {
    using chesscoach::app::MoveEvaluations;
    constexpr qint64 mate_in_3 = 99997; // §7.2's value of a mate in 3 for the mover
    // A mate against the mover after the move: it loses by force, from anywhere.
    CHECK(loss_sentence({.before = 30, .after = -mate_in_3}) ==
          QStringLiteral("The engine says it loses by force."));
    CHECK(loss_sentence({.before = -mate_in_3 - 2, .after = -mate_in_3}) ==
          QStringLiteral("The engine says it loses by force."));
    CHECK(loss_cell({.before = 30, .after = -mate_in_3}) == QStringLiteral("loses by force"));
    // A forced win before it and none after: it misses the win, though the mover may stand well.
    CHECK(loss_sentence({.before = mate_in_3, .after = 250}) ==
          QStringLiteral("The engine says it misses a forced win."));
    CHECK(loss_cell({.before = mate_in_3, .after = 250}) == QStringLiteral("misses a win"));
    // A mate kept, a little slower, gives away almost nothing.
    CHECK(loss_sentence({.before = mate_in_3, .after = mate_in_3 - 2}) ==
          QStringLiteral("The engine says it gives away almost nothing."));
    // Still winning after a large swing, +12 to +4: pawns, never "by force".
    CHECK(loss_sentence({.before = 1200, .after = 400}) ==
          QStringLiteral("The engine says it gives away about 8.0 pawns."));
    CHECK(loss_cell({.before = 1200, .after = 400}) == QStringLiteral("8.0"));
    // Otherwise the pawns, rounded, or almost nothing.
    CHECK(loss_sentence({.before = 30, .after = 26}) ==
          QStringLiteral("The engine says it gives away almost nothing."));
    CHECK(loss_sentence({.before = 30, .after = 25}) ==
          QStringLiteral("The engine says it gives away about 0.1 pawns."));
    CHECK(loss_sentence({.before = 30, .after = -65}) ==
          QStringLiteral("The engine says it gives away about 1 pawn."));
    CHECK(loss_sentence({.before = 30, .after = -79}) ==
          QStringLiteral("The engine says it gives away about 1.1 pawns."));
    CHECK(loss_sentence({.before = 100, .after = -1400}) ==
          QStringLiteral("The engine says it gives away about 15.0 pawns."));
    CHECK(loss_cell({.before = 30, .after = 30}) == QStringLiteral("almost none"));
    CHECK(loss_cell({.before = 30, .after = -79}) == QStringLiteral("1.1"));
}

TEST_CASE("A move's evaluations are the store's values from the mover's side", "[wording]") {
    using chesscoach::Color;
    using chesscoach::app::move_evaluations;
    chesscoach::PositionRow before;
    before.eval_cp = 30;
    chesscoach::PositionRow mated;
    mated.eval_mate = -3; // black mates in 3
    const auto white = move_evaluations(Color::white, before, mated);
    CHECK(white.before == 30);
    CHECK(white.after == -99997);
    const auto black = move_evaluations(Color::black, before, mated);
    CHECK(black.before == -30);
    CHECK(black.after == 99997);
}

TEST_CASE("A depth in plies is counted in moves, rounded up", "[wording]") {
    CHECK(moves_from_plies(2) == 1);
    CHECK(moves_from_plies(20) == 10);
    CHECK(moves_from_plies(21) == 11);
    CHECK(moves_from_plies(40) == 20);
}

TEST_CASE("A day is written in full", "[wording]") {
    const QDateTime noon(QDate(2026, 11, 4), QTime(12, 0));
    CHECK(day(noon.toSecsSinceEpoch()) == QStringLiteral("4 November 2026"));
}

TEST_CASE("A database's months are one, a run, or a list", "[wording]") {
    CHECK(month_range(QString()).isEmpty());
    CHECK(month_range(QStringLiteral("2026-10")) == QStringLiteral("October 2026"));
    CHECK(month_range(QStringLiteral("2026-10,2026-11,2026-12,2027-01,2027-02,2027-03")) ==
          QStringLiteral("October 2026 to March 2027"));
    CHECK(month_range(QStringLiteral("2013-01,2013-02")) ==
          QStringLiteral("January 2013 to February 2013"));
    CHECK(month_range(QStringLiteral("2013-01,2013-03")) ==
          QStringLiteral("January and March 2013"));
    CHECK(month_range(QStringLiteral("2013-01,2013-03,2013-04")) ==
          QStringLiteral("January, March and April 2013"));
    CHECK(month_range(QStringLiteral("2026-11,2027-01,2027-03")) ==
          QStringLiteral("November 2026, January and March 2027"));
    // A month that does not read leaves the text as meta stores it.
    CHECK(month_range(QStringLiteral("2013-01,junk")) == QStringLiteral("2013-01,junk"));
    CHECK(month_range(QStringLiteral("2013-13")) == QStringLiteral("2013-13"));
}

TEST_CASE("The engine's state reads short, or with the way forward", "[wording]") {
    const QString name = QStringLiteral("Stockfish 17");
    for (const bool forward : {false, true}) {
        CHECK(engine_words(EngineState::ready, name, forward) ==
              QStringLiteral("Stockfish 17 ready"));
        CHECK(engine_words(EngineState::searching, name, forward) ==
              QStringLiteral("Stockfish 17 thinking…"));
        CHECK(engine_words(EngineState::starting, QString(), forward) ==
              QStringLiteral("Engine starting…"));
        CHECK(engine_words(EngineState::not_started, QString(), forward) ==
              QStringLiteral("Engine not started"));
    }
    CHECK(engine_words(EngineState::not_found, QString(), false) ==
          QStringLiteral("No engine found"));
    CHECK(engine_words(EngineState::not_found, QString(), true) ==
          QStringLiteral("No engine found. Choose a Stockfish program in Settings."));
    CHECK(engine_words(EngineState::failed, QString(), false) ==
          QStringLiteral("The engine did not start"));
    CHECK(engine_words(EngineState::failed, QString(), true) ==
          QStringLiteral("The engine did not start. Choose another engine program in Settings."));
}
