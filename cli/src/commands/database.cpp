// chess-coach-cli database (database.hpp).
#include "database.hpp"

#include <chesscoach/opening_locator.hpp>
#include <chesscoach/opening_schema.hpp>
#include <chesscoach/spar.hpp>

#include <fmt/format.h>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace chesscoach::cli {

namespace {

Result<void> info(const DatabaseOptions& options, std::ostream& out) {
    const auto path = resolve_db(options.db, options.data_dir.empty()
                                                 ? std::nullopt
                                                 : std::optional<std::string>(options.data_dir));
    if (!path) {
        return tl::unexpected(path.error());
    }
    const auto store = open_store(*path);
    if (!store) {
        return tl::unexpected(store.error());
    }
    const auto meta = [&store](std::string_view key) {
        const std::string* value = store->meta().find(key);
        return value != nullptr ? *value : std::string("-");
    };
    out << fmt::format("path: {}\nschema_version: {}\ndata_version: {}\nmonths: {}\n"
                       "min_games: {}\nmax_ply: {}\n",
                       *path, meta("schema_version"), opening_data_version(*store), meta("months"),
                       meta("min_games"), meta("max_ply"));
    return {};
}

Result<void> import(const DatabaseOptions& options, std::ostream& out) {
    if (options.data_dir.empty()) {
        return fail("database import needs --data-dir, the directory to put openings.db in");
    }
    const auto imported = import_opening_database(options.data_dir, options.file);
    if (!imported) {
        const ImportError& error = imported.error();
        switch (error.reason) {
        case ImportError::Reason::missing_source:
            return fail(fmt::format("no such file {}", options.file));
        case ImportError::Reason::not_openings:
            return fail(fmt::format("{} is not an opening database: {}", options.file,
                                    describe(error.store_error)));
        case ImportError::Reason::newer_version:
            return fail(fmt::format("{} has schema_version {}, newer than the {} this build reads",
                                    options.file, error.source_version, opening_schema_version));
        case ImportError::Reason::already_in_place:
            return fail(
                fmt::format("{} is already the openings.db of {}", options.file, options.data_dir));
        case ImportError::Reason::inside_data_dir:
            return fail(fmt::format("{} is inside the data directory {}; import a file from "
                                    "elsewhere",
                                    options.file, options.data_dir));
        case ImportError::Reason::unwritable:
            return fail(fmt::format("openings.db in {} cannot be replaced; nothing was changed",
                                    options.data_dir));
        }
        return fail("the import failed");
    }
    out << fmt::format("imported: {}\ndata_version: {}\nmonths: {}\n", imported->path.string(),
                       imported->data_version, imported->months);
    if (imported->replaced) {
        out << fmt::format(
            "previous: {}\n",
            (std::filesystem::path(options.data_dir) / "openings.db.previous").string());
    }
    return {};
}

} // namespace

Result<void> run_database(const DatabaseOptions& options, std::ostream& out) {
    if (options.action == "import") {
        return import(options, out);
    }
    return info(options, out);
}

} // namespace chesscoach::cli
