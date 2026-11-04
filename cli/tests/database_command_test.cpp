// Unit tests of where the CLI finds the opening database and of chess-coach-cli database (cli;
// unit), on the fixture database and scratch data directories: resolve_db's order, an explicit
// --db that is not there as an error rather than a fall back, and its errors; a command that reads
// --data-dir's openings.db as it reads --db; database info; and database import, its round trip
// with the replaced file kept as openings.db.previous, and each refusal.

#include "commands/common.hpp"
#include "commands/database.hpp"
#include "commands/stats.hpp"
#include "fixture.hpp"
#include "scratch_dir.hpp"

#include <SQLiteCpp/SQLiteCpp.h>
#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>

using chesscoach::cli::DatabaseOptions;
using chesscoach::cli::resolve_db;
using chesscoach::cli::StatsOptions;
using chesscoach::cli::test::FixtureDb;
using chesscoach::testing::ScratchDir;

namespace {

// A command's output, or its error message prefixed with "error: ".
std::string run(const std::function<chesscoach::cli::Result<void>(std::ostream&)>& command) {
    std::ostringstream out;
    const auto result = command(out);
    return result ? out.str() : "error: " + result.error().message;
}

std::string database(const DatabaseOptions& options) {
    return run(
        [&options](std::ostream& out) { return chesscoach::cli::run_database(options, out); });
}

std::string stats(const StatsOptions& options) {
    return run([&options](std::ostream& out) { return chesscoach::cli::run_stats(options, out); });
}

std::string openings_in(const std::string& data_dir) {
    return (std::filesystem::path(data_dir) / "openings.db").string();
}

} // namespace

TEST_CASE("resolve_db takes --db, there or not, else --data-dir's openings.db", "[cli][database]") {
    const FixtureDb db;
    const ScratchDir scratch("database", "resolve");
    const std::string data = scratch.path("data").string();
    const std::string missing = scratch.path("missing.db").string();
    // --db as given, there or not, with or without a data directory; without either, a request.
    CHECK(resolve_db(missing, std::nullopt) == missing);
    CHECK(resolve_db(missing, data) == missing);
    CHECK(resolve_db("", std::nullopt).error().message ==
          "no opening database: give --db, or --data-dir with an openings.db in it");
    CHECK(resolve_db("", data).error().message ==
          fmt::format("no opening database: {} does not exist (give --db, or put one there with "
                      "database import)",
                      openings_in(data)));
    std::filesystem::create_directories(data);
    std::filesystem::copy_file(db.path(), openings_in(data));
    CHECK(resolve_db("", data) == openings_in(data));
    CHECK(resolve_db(db.path(), data) == db.path());
    // A --db that is not there is an error even beside a data directory that has one.
    CHECK(resolve_db(missing, data) == missing);
    CHECK(chesscoach::cli::open_located_store(missing, data).error().message ==
          "cannot open the opening database " + missing + ": no such file");
}

TEST_CASE("A command reads --data-dir's openings.db as it reads --db", "[cli][database]") {
    const FixtureDb db;
    const ScratchDir scratch("database", "command");
    const std::string data = scratch.path("data").string();
    const DatabaseOptions put{.action = "import", .data_dir = data, .db = "", .file = db.path()};
    REQUIRE(database(put).starts_with("imported: "));
    const std::string with_db = stats(StatsOptions{.db = db.path(), .data_dir = std::nullopt});
    CHECK(stats(StatsOptions{.db = "", .data_dir = data}) == with_db);
    CHECK(with_db.starts_with("admitted_tier_0: "));
}

TEST_CASE("database info prints where the database is and what its meta records",
          "[cli][database]") {
    const FixtureDb db;
    const ScratchDir scratch("database", "info");
    CHECK(database({.action = "info", .data_dir = "", .db = db.path(), .file = ""}) ==
          fmt::format("path: {}\nschema_version: 1\ndata_version: 0\nmonths: 2013-01\n"
                      "min_games: 10\nmax_ply: 8\n",
                      db.path()));
    const std::string data = scratch.path("data").string();
    CHECK(database({.action = "info", .data_dir = data, .db = "", .file = ""})
              .starts_with("error: no opening database: "));
}

TEST_CASE("database import puts a copy in place and keeps the one it replaces", "[cli][database]") {
    const FixtureDb db;
    const ScratchDir scratch("database", "import");
    const std::string data = scratch.path("data").string();
    const DatabaseOptions put{.action = "import", .data_dir = data, .db = "", .file = db.path()};
    CHECK(database(put) ==
          fmt::format("imported: {}\ndata_version: 0\nmonths: 2013-01\n", openings_in(data)));
    CHECK(database(put) ==
          fmt::format("imported: {}\ndata_version: 0\nmonths: 2013-01\nprevious: {}\n",
                      openings_in(data), openings_in(data) + ".previous"));
    CHECK(database({.action = "info", .data_dir = data, .db = "", .file = ""})
              .starts_with(fmt::format("path: {}\n", openings_in(data))));
}

TEST_CASE("database import refuses what it cannot put in place", "[cli][database]") {
    const FixtureDb db;
    const ScratchDir scratch("database", "refused");
    const std::string data = scratch.path("data").string();
    const auto import = [&data](const std::string& file) {
        return database({.action = "import", .data_dir = data, .db = "", .file = file});
    };
    REQUIRE(import(db.path()).starts_with("imported: "));

    CHECK(database({.action = "import", .data_dir = "", .db = "", .file = db.path()}) ==
          "error: database import needs --data-dir, the directory to put openings.db in");
    const std::string missing = scratch.path("missing.db").string();
    CHECK(import(missing) == "error: no such file " + missing);
    const std::string notes = scratch.path("notes.txt").string();
    {
        std::ofstream out(notes);
        out << "not a database\n";
    }
    CHECK(import(notes) ==
          "error: " + notes + " is not an opening database: not a readable SQLite database");
    const std::string newer = scratch.path("newer.db").string();
    std::filesystem::copy_file(db.path(), newer);
    {
        SQLite::Database file(newer, SQLite::OPEN_READWRITE);
        file.exec("UPDATE meta SET value = '2' WHERE key = 'schema_version'");
    }
    CHECK(import(newer) ==
          "error: " + newer + " has schema_version 2, newer than the 1 this build reads");
    CHECK(import(openings_in(data)) ==
          fmt::format("error: {} is already the openings.db of {}", openings_in(data), data));
    const std::string inside = (std::filesystem::path(data) / "copy.db").string();
    std::filesystem::copy_file(db.path(), inside);
    CHECK(import(inside) ==
          fmt::format("error: {} is inside the data directory {}; import a file from elsewhere",
                      inside, data));
    CHECK_FALSE(std::filesystem::exists(openings_in(data) + ".previous"));
}
