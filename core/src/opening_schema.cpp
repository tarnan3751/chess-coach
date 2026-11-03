// The DDL of openings.db (chesscoach/opening_schema.hpp; docs/database.md §6.1 to §6.4).
#include <chesscoach/opening_schema.hpp>

namespace chesscoach {

const char* openings_schema_sql() noexcept {
    return "CREATE TABLE meta (\n"
           "    key TEXT PRIMARY KEY,\n"
           "    value TEXT NOT NULL\n"
           ");\n"
           "CREATE TABLE positions (\n"
           "    key INTEGER PRIMARY KEY,\n"
           "    fen TEXT NOT NULL,\n"
           "    eco TEXT,\n"
           "    name TEXT,\n"
           "    eval_cp INTEGER,\n"
           "    eval_mate INTEGER,\n"
           "    eval_depth INTEGER,\n"
           "    best_move TEXT\n"
           ") WITHOUT ROWID;\n"
           "CREATE TABLE nodes (\n"
           "    key INTEGER NOT NULL,\n"
           "    tier INTEGER NOT NULL,\n"
           "    games INTEGER NOT NULL,\n"
           "    white INTEGER NOT NULL,\n"
           "    draws INTEGER NOT NULL,\n"
           "    black INTEGER NOT NULL,\n"
           "    PRIMARY KEY (key, tier)\n"
           ") WITHOUT ROWID;\n"
           "CREATE TABLE moves (\n"
           "    key INTEGER NOT NULL,\n"
           "    tier INTEGER NOT NULL,\n"
           "    move TEXT NOT NULL,\n"
           "    child INTEGER NOT NULL,\n"
           "    white INTEGER NOT NULL,\n"
           "    draws INTEGER NOT NULL,\n"
           "    black INTEGER NOT NULL,\n"
           "    badges INTEGER NOT NULL,\n"
           "    PRIMARY KEY (key, tier, move)\n"
           ") WITHOUT ROWID;\n";
}

} // namespace chesscoach
