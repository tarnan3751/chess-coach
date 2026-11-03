// The opening database's location and its import (chesscoach/opening_locator.hpp). The import
// stages under a name of its own and renames within the data directory, so a reader never sees a
// partial file at openings.db.
#include <chesscoach/opening_locator.hpp>
#include <chesscoach/opening_schema.hpp>
#include <chesscoach/spar.hpp>

#include <SQLiteCpp/SQLiteCpp.h>

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
    std::filesystem::copy_file(source, importing, error);
    if (error) {
        return unwritable();
    }
    // The copy is what was validated, or it is not put in place.
    if (const auto copy = OpeningStore::open(importing);
        !copy || opening_data_version(*copy) != imported.data_version) {
        return unwritable();
    }

    imported.replaced = is_file(current);
    if (imported.replaced) {
        std::filesystem::rename(current, previous, error);
        if (error) {
            return unwritable();
        }
    }
    std::filesystem::rename(importing, current, error);
    if (error) {
        // The openings.db this import was to replace goes back.
        if (imported.replaced) {
            std::error_code ignored;
            std::filesystem::rename(previous, current, ignored);
        }
        return unwritable();
    }
    return imported;
}

} // namespace chesscoach
