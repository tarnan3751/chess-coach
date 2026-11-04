// Unit tests for chesscoach/opening_locator.hpp (core; unit): the locator's order, an explicit path
// taken whether or not it is there, then the data directory and the bundle's resources; the
// import's round trip, which keeps the replaced file as openings.db.previous, compared by canonical
// dump; a source in WAL mode, read through its -wal; and every refusal, after which the data
// directory is as it was. Each database is a small file in core's schema (openings_schema_sql)
// whose meta varies. The swap the import ends with is replace_keeping_previous, which
// file_swap_test checks, a failed final rename included.
#include <chesscoach/opening_locator.hpp>
#include <chesscoach/opening_schema.hpp>

#include "scratch_dir.hpp"

#include <SQLiteCpp/SQLiteCpp.h>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

using chesscoach::ImportError;
using chesscoach::LocatorInput;
using chesscoach::StoreError;
using chesscoach::testing::ScratchDir;

namespace {

// SQLite's open flags are ints, combined with | as its API documents.
int create_flags() {
    return SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE; // NOLINT(bugprone-signed-bitwise)
}

// An opening database with core's tables and the meta given, and no rows.
void write_openings(const std::filesystem::path& path, std::string_view schema_version,
                    std::string_view data_version, std::string_view months = "2013-01") {
    std::filesystem::create_directories(path.parent_path());
    SQLite::Database db(path, create_flags());
    db.exec(chesscoach::openings_schema_sql());
    SQLite::Statement insert(db, "INSERT INTO meta (key, value) VALUES (?1, ?2)");
    for (const auto& [key, value] : {
             std::pair{std::string_view("schema_version"), schema_version},
             std::pair{std::string_view("data_version"), data_version},
             std::pair{std::string_view("months"), months},
         }) {
        insert.bind(1, std::string(key));
        insert.bind(2, std::string(value));
        insert.exec();
        insert.reset();
    }
}

void write_text(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << text;
}

std::string read_bytes(const std::filesystem::path& path) {
    const std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// The canonical dump of the opening database at path (§11), its content whatever its bytes: an
// import copies through SQLite's backup API, which need not reproduce the source byte for byte.
std::string dump_of(const std::filesystem::path& path) {
    const auto store = chesscoach::OpeningStore::open(path);
    REQUIRE(store.has_value());
    std::ostringstream out;
    store->dump(out);
    return out.str();
}

// The names in the directory, sorted, to show what an import left there.
std::string listing(const std::filesystem::path& directory) {
    std::vector<std::string> names;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        names.push_back(entry.path().filename().string());
    }
    std::ranges::sort(names);
    std::string text;
    for (const auto& name : names) {
        text += name + ' ';
    }
    return text;
}

} // namespace

TEST_CASE("The locator takes --db, there or not, else the data directory's file, else the bundle's",
          "[locator]") {
    const ScratchDir scratch("locator", "order");
    const auto explicit_db = scratch.path("mine.db");
    const auto data_dir = scratch.path("data");
    const auto bundle = scratch.path("bundle");
    const auto locate = [&](bool with_explicit, bool with_bundle) {
        return chesscoach::locate_opening_database(LocatorInput{
            .explicit_db = with_explicit ? std::optional(explicit_db) : std::nullopt,
            .data_dir = data_dir,
            .bundle_resources = with_bundle ? std::optional(bundle) : std::nullopt,
        });
    };
    // An explicit path is the answer whenever it is given, though nothing is there.
    CHECK(locate(true, true) == explicit_db);
    CHECK_FALSE(locate(false, true).has_value());
    write_openings(bundle / "openings.db", "1", "1");
    CHECK(locate(false, true) == bundle / "openings.db");
    CHECK_FALSE(locate(false, false).has_value());
    CHECK(locate(true, true) == explicit_db);
    write_openings(data_dir / "openings.db", "1", "2");
    CHECK(locate(false, true) == data_dir / "openings.db");
    CHECK(locate(false, false) == data_dir / "openings.db");
    CHECK(locate(true, true) == explicit_db);
    write_openings(explicit_db, "1", "3");
    CHECK(locate(true, true) == explicit_db);
    // Without one, a directory where the data directory's file should be is not a file.
    std::filesystem::remove(data_dir / "openings.db");
    std::filesystem::create_directories(data_dir / "openings.db");
    CHECK(locate(false, true) == bundle / "openings.db");
}

TEST_CASE("An import puts a copy in place and keeps the file it replaces", "[locator]") {
    const ScratchDir scratch("locator", "round-trip");
    const auto data_dir = scratch.path("data");
    const auto first = scratch.path("first.db");
    const auto second = scratch.path("second.db");
    write_openings(first, "1", "3", "2013-01");
    write_openings(second, "1", "4", "2013-01,2013-02");

    const auto put = chesscoach::import_opening_database(data_dir, first);
    REQUIRE(put.has_value());
    CHECK(put->path == data_dir / "openings.db");
    CHECK(put->data_version == 3);
    CHECK(put->months == "2013-01");
    CHECK_FALSE(put->replaced);
    CHECK(dump_of(data_dir / "openings.db") == dump_of(first));
    CHECK(listing(data_dir) == "openings.db ");

    const auto replaced = chesscoach::import_opening_database(data_dir, second);
    REQUIRE(replaced.has_value());
    CHECK(replaced->data_version == 4);
    CHECK(replaced->months == "2013-01,2013-02");
    CHECK(replaced->replaced);
    CHECK(dump_of(data_dir / "openings.db") == dump_of(second));
    CHECK(dump_of(data_dir / "openings.db.previous") == dump_of(first));
    CHECK(listing(data_dir) == "openings.db openings.db.previous ");
    // The sources are only read.
    CHECK(chesscoach::OpeningStore::open(first).has_value());
    CHECK(chesscoach::OpeningStore::open(second).has_value());
}

TEST_CASE("An import reads through the -wal beside a source in WAL mode", "[locator]") {
    const ScratchDir scratch("locator", "wal");
    const auto data_dir = scratch.path("data");
    const auto source = scratch.path("wal.db");
    write_openings(source, "1", "5");
    // A row committed to the -wal and not yet in the file: the writer stays open, so the WAL is
    // not checkpointed into the file before the import reads it.
    SQLite::Database writer(source, SQLite::OPEN_READWRITE);
    REQUIRE(writer.execAndGet("PRAGMA journal_mode = WAL").getString() == "wal");
    writer.exec("INSERT INTO meta (key, value) VALUES ('extra', 'in the wal')");
    auto wal = source;
    wal += "-wal";
    REQUIRE(std::filesystem::file_size(wal) > 0);

    const auto imported = chesscoach::import_opening_database(data_dir, source);
    REQUIRE(imported.has_value());
    CHECK(imported->data_version == 5);
    const auto store = chesscoach::OpeningStore::open(data_dir / "openings.db");
    REQUIRE(store.has_value());
    const std::string* extra = store->meta().find("extra");
    REQUIRE(extra != nullptr);
    CHECK(*extra == "in the wal");
    // The copy is in rollback mode: nothing beside it.
    CHECK(listing(data_dir) == "openings.db ");
}

TEST_CASE("An import that cannot be used is refused and changes nothing", "[locator]") {
    const ScratchDir scratch("locator", "refused");
    const auto data_dir = scratch.path("data");
    write_openings(data_dir / "openings.db", "1", "7");
    write_openings(data_dir / "openings.db.previous", "1", "6");
    const auto before = read_bytes(data_dir / "openings.db");
    const auto refusal = [&](const std::filesystem::path& source) {
        const auto result = chesscoach::import_opening_database(data_dir, source);
        REQUIRE_FALSE(result.has_value());
        return result.error();
    };
    using Reason = ImportError::Reason;

    CHECK(refusal(scratch.path("missing.db")).reason == Reason::missing_source);
    write_text(scratch.path("notes.txt"), "not a database\n");
    const auto text = refusal(scratch.path("notes.txt"));
    CHECK(text.reason == Reason::not_openings);
    CHECK(text.store_error == StoreError::unreadable);
    {
        SQLite::Database db(scratch.path("no-meta.db"), create_flags());
        db.exec("CREATE TABLE notes (text TEXT)");
    }
    CHECK(refusal(scratch.path("no-meta.db")).store_error == StoreError::no_meta);
    write_openings(scratch.path("newer.db"), "2", "9");
    const auto newer = refusal(scratch.path("newer.db"));
    CHECK(newer.reason == Reason::newer_version);
    CHECK(newer.source_version == 2);
    write_openings(scratch.path("older.db"), "0", "9");
    const auto older = refusal(scratch.path("older.db"));
    CHECK(older.reason == Reason::not_openings);
    CHECK(older.store_error == StoreError::wrong_version);
    CHECK(older.source_version == 0);
    CHECK(refusal(data_dir / "openings.db").reason == Reason::already_in_place);
    CHECK(refusal(data_dir / "openings.db.previous").reason == Reason::inside_data_dir);
    write_openings(data_dir / "sub" / "other.db", "1", "8");
    CHECK(refusal(data_dir / "sub" / "other.db").reason == Reason::inside_data_dir);

    CHECK(read_bytes(data_dir / "openings.db") == before);
    CHECK(listing(data_dir) == "openings.db openings.db.previous sub ");
}
