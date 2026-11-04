// The app tests' opening database: a handful of positions, nodes, moves, names and evaluations at
// tiers 0, 3 and 6, written into core's schema (docs/database.md §6) with the real keys of the
// moves, from core's Position; the rows are the CLI fixture's (cli/tests/fixture.hpp) with their
// badges as §7.3 gives them, and a small tree at tier 4 for Spar: a Dubious first move and an
// off-book continuation; the writer is the one core's tests share (fixture_writer.hpp). And helpers
// that change a copy's data_version and take a profiles.db's cards away.
#pragma once

#include "fixture_writer.hpp"

#include <filesystem>

namespace chesscoach::app::test {

// The key of the position after the moves (SAN, separated by spaces) from the start position.
using chesscoach::testing::key_after;

// Writes the fixture database to path, which must not exist.
void write_fixture_db(const std::filesystem::path& path);

// Sets the data_version in meta of the opening database at path, as another build would have it.
void set_data_version(const std::filesystem::path& openings_db, int data_version);

// Deletes every card of the profiles.db at path, leaving its repertoire nodes without cards, as
// no store call does, for the Drill tests' sync to repair.
void drop_cards(const std::filesystem::path& profiles_db);

// The fixture database in a fresh temporary directory, removed with the object.
class FixtureDb : public chesscoach::testing::FixtureDb {
  public:
    FixtureDb();
};

} // namespace chesscoach::app::test
