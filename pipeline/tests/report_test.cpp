// Unit tests for Report: name<TAB>value lines in order, the last value of a name, and a peak
// resident size that is plausible (pipeline; unit).
#include "report.hpp"

#include <catch2/catch_test_macros.hpp>

#include <sstream>

using chesscoach::pipeline::peak_rss_bytes;
using chesscoach::pipeline::Report;

TEST_CASE("Report writes name<TAB>value lines in order", "[report]") {
    Report report;
    report.add("games_read", std::uint64_t{12});
    report.add("months", "2013-01,2013-02");
    report.add("games_read", std::uint64_t{13});
    std::ostringstream out;
    report.write(out);
    CHECK(out.str() == "games_read\t12\nmonths\t2013-01,2013-02\ngames_read\t13\n");
    CHECK(report.value("games_read") == "13");
    CHECK(report.value("absent").empty());
}

TEST_CASE("peak_rss_bytes is at least a megabyte and below a terabyte", "[report]") {
    const auto peak = peak_rss_bytes();
    CHECK(peak > std::uint64_t{1} << 20U);
    CHECK(peak < std::uint64_t{1} << 40U);
}
