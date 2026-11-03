// The schema of openings.db (docs/database.md §6): the one DDL the pipeline's writer executes, and
// the version the store accepts. Changing either is tier A, and a change to the schema increments
// the version (§14).
#pragma once

namespace chesscoach {

// The value of meta's schema_version key in every file this build writes or reads.
inline constexpr int opening_schema_version = 1;

// CREATE TABLE statements for meta, positions, nodes and moves, exactly as §6 states them, as one
// script for sqlite3_exec. Creates the tables only; the writer inserts every row, meta's included.
[[nodiscard]] const char* openings_schema_sql() noexcept;

} // namespace chesscoach
