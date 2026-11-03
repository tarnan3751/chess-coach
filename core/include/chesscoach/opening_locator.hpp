// Where the opening database is, and how a new one is put in place (PLAN.md §5.4 and §12 item 2;
// docs/database.md §6.1; docs/reference/core.md, **Where the opening database is**). The locator
// takes an explicit path whenever one is given, there or not (PLAN.md §11), and otherwise the
// first file that exists of <data_dir>/openings.db and the app bundle's <resources>/openings.db.
// The import validates a file through OpeningStore, copies it
// into the data directory as openings.db.importing and renames it into place, keeping the file it
// replaces as openings.db.previous; nothing ever writes an openings.db in place. No Qt.
#pragma once

#include <chesscoach/opening_store.hpp>

#include <tl/expected.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace chesscoach {

struct LocatorInput {
    std::optional<std::filesystem::path> explicit_db = std::nullopt; // --db
    std::filesystem::path data_dir;
    // The app bundle's resources directory, where a release may ship the database (§12 item 2).
    std::optional<std::filesystem::path> bundle_resources = std::nullopt;
};

// explicit_db whenever it is given, whether or not the file is there, so the caller's open reports
// a missing one rather than another file being read; without it, the first regular file of
// <data_dir>/openings.db and <bundle_resources>/openings.db, or nullopt when neither is.
[[nodiscard]] std::optional<std::filesystem::path> locate_opening_database(const LocatorInput& in);

// The database an import put in place, with what its meta records.
struct ImportedDatabase {
    std::filesystem::path path;    // <data_dir>/openings.db
    std::int64_t data_version = 0; // opening_data_version (spar.hpp)
    std::string months;            // meta's months as stored: YYYY-MM, comma-separated
    bool replaced = false;         // an earlier openings.db is now openings.db.previous
};

struct ImportError {
    enum class Reason : std::uint8_t {
        missing_source,   // no regular file at the source path
        not_openings,     // OpeningStore refuses it (store_error says why)
        newer_version,    // its schema_version, source_version, is above opening_schema_version
        already_in_place, // it is the data directory's openings.db itself
        inside_data_dir,  // it lies elsewhere inside the data directory
        unwritable,       // the data directory or a file in it cannot be created, copied or renamed
    };
    Reason reason = Reason::missing_source;
    StoreError store_error = StoreError::missing_file; // not_openings
    int source_version = 0;                            // newer_version, and an older one
};

// Puts a copy of source in place as <data_dir>/openings.db: refused, changing nothing, unless
// OpeningStore opens it; copied to openings.db.importing through SQLite's backup API, which reads
// through a -wal beside a WAL-mode source, and opened again; then the current
// openings.db, if any, is renamed to openings.db.previous, replacing an earlier one, and the copy
// to openings.db, the first rename undone when the second fails, so an openings.db that was there
// stays. The source is never written. A reader holding the old file keeps reading it until it
// opens the path again.
[[nodiscard]] tl::expected<ImportedDatabase, ImportError>
import_opening_database(const std::filesystem::path& data_dir, const std::filesystem::path& source);

} // namespace chesscoach
