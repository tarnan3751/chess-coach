// Unit tests for the month-file checks of the build's inputs (docs/database.md §6.1) (pipeline;
// unit).
#include "options.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <vector>

using chesscoach::pipeline::month_inputs;
using chesscoach::pipeline::month_of;
using chesscoach::pipeline::partial_path;

TEST_CASE("month_of reads the month of a month file's name", "[options]") {
    CHECK(month_of("lichess_db_standard_rated_2013-01.pgn.zst") == "2013-01");
    CHECK(month_of(std::filesystem::path("dir") / "lichess_db_standard_rated_2024-12.pgn.zst") ==
          "2024-12");
    CHECK_FALSE(month_of("lichess_db_standard_rated_2013-13.pgn.zst"));
    CHECK_FALSE(month_of("lichess_db_standard_rated_2013-00.pgn.zst"));
    CHECK_FALSE(month_of("lichess_db_standard_rated_2013-1.pgn.zst"));
    CHECK_FALSE(month_of("lichess_db_standard_rated_2013-01.pgn"));
    CHECK_FALSE(month_of("sample.pgn.zst"));
    CHECK_FALSE(month_of("lichess_db_standard_rated_201a-01.pgn.zst"));
}

TEST_CASE("month_inputs orders the inputs by month and refuses a repeated month", "[options]") {
    const std::vector<std::filesystem::path> two_months{
        std::filesystem::path("b") / "lichess_db_standard_rated_2013-02.pgn.zst",
        std::filesystem::path("a") / "lichess_db_standard_rated_2013-01.pgn.zst",
    };
    const auto ordered = month_inputs(two_months);
    REQUIRE(ordered.has_value());
    REQUIRE(ordered->size() == 2);
    CHECK(ordered->at(0).month == "2013-01");
    CHECK(ordered->at(1).month == "2013-02");

    const std::vector<std::filesystem::path> one_month_twice{
        std::filesystem::path("a") / "lichess_db_standard_rated_2013-01.pgn.zst",
        std::filesystem::path("b") / "lichess_db_standard_rated_2013-01.pgn.zst",
    };
    const auto repeated = month_inputs(one_month_twice);
    REQUIRE_FALSE(repeated.has_value());
    CHECK(repeated.error().find("2013-01") != std::string::npos);

    const auto unnamed = month_inputs({"games.pgn.zst"});
    REQUIRE_FALSE(unnamed.has_value());
    CHECK(unnamed.error().find("games.pgn.zst") != std::string::npos);
}

TEST_CASE("partial_path appends .partial", "[options]") {
    CHECK(partial_path(std::filesystem::path("out") / "openings.db") ==
          std::filesystem::path("out") / "openings.db.partial");
}
