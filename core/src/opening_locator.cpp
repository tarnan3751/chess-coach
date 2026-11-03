// The opening database's location and its import (chesscoach/opening_locator.hpp). The import
// stages under a name of its own and renames within the data directory, so a reader never sees a
// partial file at openings.db.
#include <chesscoach/file_swap.hpp>
#include <chesscoach/opening_locator.hpp>
#include <chesscoach/opening_schema.hpp>
#include <chesscoach/spar.hpp>

#include <SQLiteCpp/Backup.h>
#include <SQLiteCpp/SQLiteCpp.h>
#include <sqlite3.h>

#include <string_view>
#include <system_error>

namespace chesscoach {

namespace {

bool is_file(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
}

// Whether path lies inside directory, both as the file system resolves them.
bool inside(const std::filesystem::path& path, const std::filesystem::path& directory) {
    std::error_code error;
    const auto file = std::filesystem::weakly_canonical(path, error);
    if (error) {
        return false;
    }
    const auto root = std::filesystem::weakly_canonical(directory, error);
    if (error) {
        return false;
    }
    const auto relative = file.lexically_relative(root);
    return !relative.empty() && *relative.begin() != "..";
}

// The schema_version the file's meta records, or 0 when it cannot be read as a number.
int recorded_schema_version(const std::filesystem::path& path) {
    try {
        const SQLite::Database db(path, SQLite::OPEN_READONLY);
        SQLite::Statement query(db, "SELECT value FROM meta WHERE key = 'schema_version'");
        if (!query.executeStep()) {
            return 0;
        }
        const std::string text = query.getColumn(0).getString();
        if (text.empty() || text.size() > 9 ||
            text.find_first_not_of("0123456789") != std::string::npos) {
            return 0;
        }
        return std::stoi(text);
    } catch (const SQLite::Exception&) {
        return 0;
    }
}

// Copies source to destination with SQLite's online backup API, which reads through a -wal beside
// a WAL-mode source as a plain file copy would not, then leaves the copy in rollback mode, so a
// read-only connection opens it without a -shm. Both connections wait up to 2 s for a lock, as
// the profile store's do, so a commit another tool makes meanwhile is waited out. False when the
// copy does not complete or does not leave rollback mode.
bool back_up(const std::filesystem::path& source, const std::filesystem::path& destination) {
    constexpr int busy_timeout_ms = 2000;
    try {
        SQLite::Database from(source, SQLite::OPEN_READONLY);
        from.setBusyTimeout(busy_timeout_ms);
        // SQLite's open flags are ints, combined with | as its API documents.
        // NOLINTNEXTLINE(bugprone-signed-bitwise)
        const int flags = SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE;
        SQLite::Database to(destination, flags);
        to.setBusyTimeout(busy_timeout_ms);
        {
            SQLite::Backup backup(to, from);
            if (backup.executeStep() != SQLITE_DONE) {
                return false;
            }
        }
        return to.execAndGet("PRAGMA journal_mode = DELETE").getString() == "delete";
    } catch (const SQLite::Exception&) {
        return false;
    }
}

ImportError refused(ImportError::Reason reason) {
    return ImportError{
        .reason = reason,
        .store_error = StoreError::missing_file,
        .source_version = 0,
    };
}

} // namespace

std::optional<std::filesystem::path> locate_opening_database(const LocatorInput& in) {
    if (in.explicit_db) {
        return in.explicit_db;
    }
    if (auto in_data_dir = in.data_dir / "openings.db"; is_file(in_data_dir)) {
        return in_data_dir;
    }
    if (in.bundle_resources) {
        if (auto bundled = *in.bundle_resources / "openings.db"; is_file(bundled)) {
            return bundled;
        }
    }
    return std::nullopt;
}

tl::expected<ImportedDatabase, ImportError>
import_opening_database(const std::filesystem::path& data_dir,
                        const std::filesystem::path& source) {
    const auto current = data_dir / "openings.db";
    const auto importing = data_dir / "openings.db.importing";
    const auto previous = data_dir / "openings.db.previous";
    if (!is_file(source)) {
        return tl::unexpected(refused(ImportError::Reason::missing_source));
    }
    std::error_code error;
    if (std::filesystem::equivalent(source, current, error)) {
        return tl::unexpected(refused(ImportError::Reason::already_in_place));
    }
    if (inside(source, data_dir)) {
        return tl::unexpected(refused(ImportError::Reason::inside_data_dir));
    }

    ImportedDatabase imported{.path = current, .data_version = 0, .months = {}, .replaced = false};
    {
        const auto store = OpeningStore::open(source);
        if (!store) {
            const int version =
                store.error() == StoreError::wrong_version ? recorded_schema_version(source) : 0;
            return tl::unexpected(ImportError{
                .reason = version > opening_schema_version ? ImportError::Reason::newer_version
                                                           : ImportError::Reason::not_openings,
                .store_error = store.error(),
                .source_version = version,
            });
        }
        imported.data_version = opening_data_version(*store);
        if (const std::string* months = store->meta().find("months")) {
            imported.months = *months;
        }
    }

    const auto unwritable = [&] {
        std::error_code ignored;
        std::filesystem::remove(importing, ignored);
        return tl::unexpected(refused(ImportError::Reason::unwritable));
    };
    std::filesystem::create_directories(data_dir, error);
    if (error) {
        return unwritable();
    }
    std::filesystem::remove(importing, error);
    if (!back_up(source, importing)) {
        return unwritable();
    }
    // The copy is what was validated, or it is not put in place.
    if (const auto copy = OpeningStore::open(importing);
        !copy || opening_data_version(*copy) != imported.data_version) {
        return unwritable();
    }

    // The current openings.db is kept as openings.db.previous, a hard link and so instant however
    // large the file, and the copy renamed onto it: an openings.db is there at every instant.
    imported.replaced = is_file(current);
    if (!replace_keeping_previous(importing, current, previous)) {
        return unwritable();
    }
    return imported;
}

} // namespace chesscoach
