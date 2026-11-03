// The writer: the tree and its meta into a new openings.db (docs/database.md §6), through
// SQLiteCpp over core's schema (openings_schema_sql). journal_mode=OFF and synchronous=OFF for
// the build only, one transaction per batch of rows, rows in key order (signed, as stored) so
// each WITHOUT ROWID table grows at its end. The file is written at its given path, which the
// build names <out>.partial and renames once the write succeeded; a failure is a value.
#pragma once

#include "tree.hpp"

#include <tl/expected.hpp>

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace chesscoach::pipeline {

using Meta = std::vector<std::pair<std::string, std::string>>;

[[nodiscard]] tl::expected<void, std::string> write_database(const std::filesystem::path& path,
                                                             const Meta& meta, const Tree& tree);

} // namespace chesscoach::pipeline
