// The CLI tests' opening database: a handful of positions, nodes, moves, names and evaluations
// written into core's schema (docs/database.md §6), small enough that every expected output is
// written out by hand. Keys come from core's Position, so they are the real keys of the moves.
#pragma once

#include <chesscoach/key.hpp>

#include <filesystem>
#include <string>
#include <string_view>

namespace chesscoach::cli::test {

// The key of the position after the moves (SAN, separated by spaces) from the start position.
[[nodiscard]] Key key_after(std::string_view moves);

// Writes the fixture database to path, which must not exist.
void write_fixture_db(const std::filesystem::path& path);

// The fixture database in a fresh temporary directory, removed with the object.
class FixtureDb {
  public:
    FixtureDb();
    FixtureDb(const FixtureDb&) = delete;
    FixtureDb& operator=(const FixtureDb&) = delete;
    FixtureDb(FixtureDb&&) = delete;
    FixtureDb& operator=(FixtureDb&&) = delete;
    ~FixtureDb();

    [[nodiscard]] std::string path() const { return file_.string(); }

  private:
    std::filesystem::path directory_;
    std::filesystem::path file_;
};

} // namespace chesscoach::cli::test
