// Unit tests of the app's wording (app; unit; wording.hpp): the rating bands from core's bounds,
// numbers with separators and counts with their nouns, the pawns rounded in integers and the three
// cases of a loss, a day, and a database's months as one month, a run, a list with gaps or as
// stored when one does not read. Pure functions: no application object is needed.
#include "wording.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QTime>

using chesscoach::app::counted;
using chesscoach::app::day;
using chesscoach::app::loss_cell;
using chesscoach::app::loss_sentence;
using chesscoach::app::month_range;
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

TEST_CASE("A loss is almost nothing, some pawns, or by force", "[wording]") {
    const QString nothing = QStringLiteral("The engine says it gives away almost nothing.");
    CHECK(loss_sentence(0) == nothing);
    CHECK(loss_sentence(4) == nothing);
    CHECK(loss_sentence(5) == QStringLiteral("The engine says it gives away about 0.1 pawns."));
    CHECK(loss_sentence(95) == QStringLiteral("The engine says it gives away about 1 pawn."));
    CHECK(loss_sentence(104) == QStringLiteral("The engine says it gives away about 1 pawn."));
    CHECK(loss_sentence(109) == QStringLiteral("The engine says it gives away about 1.1 pawns."));
    CHECK(loss_sentence(999) == QStringLiteral("The engine says it gives away about 10.0 pawns."));
    CHECK(loss_sentence(1000) == QStringLiteral("The engine says it loses by force."));
    CHECK(loss_sentence(99997) == QStringLiteral("The engine says it loses by force."));
    CHECK(loss_cell(4) == QStringLiteral("almost none"));
    CHECK(loss_cell(109) == QStringLiteral("1.1"));
    CHECK(loss_cell(1000) == QStringLiteral("loses by force"));
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
