// The CLI tests' opening database: a handful of positions, nodes, moves, names and evaluations
// written into core's schema (docs/database.md §6), small enough that every expected output is
// written out by hand. Keys come from core's Position, so they are the real keys of the moves. The
// rows are the CLI's; the writer is the one core's tests share (fixture_writer.hpp).
#pragma once

#include "fixture_writer.hpp"

#include <filesystem>

namespace chesscoach::cli::test {

// The key of the position after the moves (SAN, separated by spaces) from the start position.
using chesscoach::testing::key_after;

// Writes the fixture database to path, which must not exist.
void write_fixture_db(const std::filesystem::path& path);

// The fixture database in a fresh temporary directory, removed with the object.
class FixtureDb : public chesscoach::testing::FixtureDb {
  public:
    FixtureDb();
};

} // namespace chesscoach::cli::test
