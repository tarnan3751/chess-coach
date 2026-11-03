// The app tests' opening database: a handful of positions, nodes, moves, names and evaluations at
// tiers 0, 3 and 6, written into core's schema (docs/database.md §6) with the real keys of the
// moves, from core's Position; the rows are the CLI fixture's (cli/tests/fixture.hpp), with a small
// tree at tier 4 for Spar: a Dubious first move and an off-book continuation. And a helper that
// takes a profiles.db's cards away.
#pragma once

#include <chesscoach/key.hpp>

#include <filesystem>
#include <string>
#include <string_view>

namespace chesscoach::app::test {

// The key of the position after the moves (SAN, separated by spaces) from the start position.
[[nodiscard]] Key key_after(std::string_view moves);

// Writes the fixture database to path, which must not exist.
void write_fixture_db(const std::filesystem::path& path);

// Deletes every card of the profiles.db at path, leaving its repertoire nodes without cards, as
// no store call does, for the Drill tests' sync to repair.
void drop_cards(const std::filesystem::path& profiles_db);

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

} // namespace chesscoach::app::test
