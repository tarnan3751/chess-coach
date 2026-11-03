// The read-only opening store over SQLiteCpp (chesscoach/opening_store.hpp; docs/database.md §6,
// §11). Keys are bound and read as the int64 with the same bits (§5.2). The dump reads each table
// in two primary-key ranges, the keys whose int64 is non-negative and then the negative ones,
// which together are the order of the keys as unsigned, so nothing is sorted in memory.
#include <chesscoach/opening_schema.hpp>
#include <chesscoach/opening_store.hpp>
#include <chesscoach/position.hpp>

#include <SQLiteCpp/SQLiteCpp.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace chesscoach {

// The connection, the meta table read at open, and the lookups, prepared once from the connection
// they are declared after.
struct OpeningStore::Impl {
    Impl(SQLite::Database database, Meta rows)
        : db(std::move(database)), meta(std::move(rows)),
          position(db, "SELECT fen, eco, name, eval_cp, eval_mate, eval_depth, best_move "
                       "FROM positions WHERE key = ?1"),
          node(db, "SELECT games, white, draws, black FROM nodes WHERE key = ?1 AND tier = ?2"),
          moves(db, "SELECT move, child, white, draws, black, badges FROM moves "
                    "WHERE key = ?1 AND tier = ?2 ORDER BY move") {}

    SQLite::Database db;
    Meta meta;
    SQLite::Statement position;
    SQLite::Statement node;
    SQLite::Statement moves;
};

namespace {

// Large enough to map any database the size budget allows (§8); SQLite caps it at its build's
// maximum.
constexpr std::string_view mmap_pragma = "PRAGMA mmap_size = 1073741824";

std::optional<std::string> optional_text(const SQLite::Column& column) {
    return column.isNull() ? std::nullopt : std::optional(column.getString());
}

std::optional<std::int64_t> optional_integer(const SQLite::Column& column) {
    return column.isNull() ? std::nullopt : std::optional(column.getInt64());
}

// The meta table's rows, or nullopt when the table is not one of key and value columns. The
// statement is finalized on return, before the connection can move into the store, so a
// connection closed by a failed open has no statement left on it.
std::optional<Meta> read_meta(const SQLite::Database& db) {
    try {
        Meta meta;
        SQLite::Statement rows(db, "SELECT key, value FROM meta");
        while (rows.executeStep()) {
            meta.entries.emplace(rows.getColumn(0).getString(), rows.getColumn(1).getString());
        }
        return meta;
    } catch (const SQLite::Exception&) {
        return std::nullopt;
    }
}

// Every row of one table in the dump's order: a line per row, the record's name first, then each
// column as stored; the columns named in key_columns hold keys and print as unsigned.
void dump_table(const SQLite::Database& db, std::ostream& out, std::string_view record,
                const char* query, std::initializer_list<int> key_columns) {
    SQLite::Statement rows(db, query);
    constexpr std::array<std::pair<std::int64_t, std::int64_t>, 2> ranges{
        {
            {0, std::numeric_limits<std::int64_t>::max()},
            {std::numeric_limits<std::int64_t>::min(), -1},
        },
    };
    std::string line;
    for (const auto& [low, high] : ranges) {
        rows.reset();
        rows.bind(1, low);
        rows.bind(2, high);
        while (rows.executeStep()) {
            line = record;
            for (int index = 0; index < rows.getColumnCount(); ++index) {
                const auto column = rows.getColumn(index);
                line += '\t';
                if (column.isNull()) {
                    line += '-';
                } else if (std::ranges::find(key_columns, index) != key_columns.end()) {
                    line += std::to_string(key_from_sql(column.getInt64()));
                } else if (column.isInteger()) {
                    line += std::to_string(column.getInt64());
                } else {
                    line += column.getString();
                }
            }
            line += '\n';
            out << line;
        }
    }
}

} // namespace

const std::string* Meta::find(std::string_view key) const {
    const auto found = entries.find(key);
    return found == entries.end() ? nullptr : &found->second;
}

OpeningStore::OpeningStore(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
OpeningStore::OpeningStore(OpeningStore&& other) noexcept = default;
OpeningStore& OpeningStore::operator=(OpeningStore&& other) noexcept = default;
OpeningStore::~OpeningStore() = default;

tl::expected<OpeningStore, StoreError> OpeningStore::open(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) {
        return tl::unexpected(StoreError::missing_file);
    }
    try {
        SQLite::Database db(path, SQLite::OPEN_READONLY);
        db.exec(std::string(mmap_pragma));
        if (!db.tableExists("meta")) {
            return tl::unexpected(StoreError::no_meta);
        }
        auto meta = read_meta(db);
        if (!meta) {
            return tl::unexpected(StoreError::no_meta);
        }
        const auto* version = meta->find("schema_version");
        if (version == nullptr) {
            return tl::unexpected(StoreError::no_meta);
        }
        if (*version != std::to_string(opening_schema_version)) {
            return tl::unexpected(StoreError::wrong_version);
        }
        try {
            return OpeningStore(std::make_unique<Impl>(std::move(db), std::move(*meta)));
        } catch (const SQLite::Exception&) {
            // A lookup does not prepare: positions, nodes or moves is missing or misshapen.
            return tl::unexpected(StoreError::schema_mismatch);
        }
    } catch (const SQLite::Exception&) {
        return tl::unexpected(StoreError::unreadable);
    }
}

const Meta& OpeningStore::meta() const noexcept { return impl_->meta; }

std::optional<PositionRow> OpeningStore::position(Key key) const {
    auto& query = impl_->position;
    query.reset();
    query.bind(1, key_to_sql(key));
    if (!query.executeStep()) {
        return std::nullopt;
    }
    PositionRow row{
        .key = key,
        .fen = query.getColumn(0).getString(),
        .eco = optional_text(query.getColumn(1)),
        .name = optional_text(query.getColumn(2)),
        .eval_cp = optional_integer(query.getColumn(3)),
        .eval_mate = optional_integer(query.getColumn(4)),
        .eval_depth = optional_integer(query.getColumn(5)),
        .best_move = optional_text(query.getColumn(6)),
    };
    query.reset();
    return row;
}

std::optional<NodeRow> OpeningStore::node(Key key, Tier tier) const {
    auto& query = impl_->node;
    query.reset();
    query.bind(1, key_to_sql(key));
    query.bind(2, tier_index(tier));
    if (!query.executeStep()) {
        return std::nullopt;
    }
    NodeRow row{
        .key = key,
        .tier = tier,
        .games = query.getColumn(0).getInt64(),
        .white = query.getColumn(1).getInt64(),
        .draws = query.getColumn(2).getInt64(),
        .black = query.getColumn(3).getInt64(),
    };
    query.reset();
    return row;
}

std::vector<MoveRow> OpeningStore::moves(Key key, Tier tier) const {
    auto& query = impl_->moves;
    query.reset();
    query.bind(1, key_to_sql(key));
    query.bind(2, tier_index(tier));
    std::vector<MoveRow> rows;
    while (query.executeStep()) {
        rows.push_back(MoveRow{
            .key = key,
            .tier = tier,
            .move = query.getColumn(0).getString(),
            .child = key_from_sql(query.getColumn(1).getInt64()),
            .white = query.getColumn(2).getInt64(),
            .draws = query.getColumn(3).getInt64(),
            .black = query.getColumn(4).getInt64(),
            .badges = Badges{static_cast<std::uint8_t>(query.getColumn(5).getInt())},
        });
    }
    return rows;
}

StoreCounts OpeningStore::counts() const {
    StoreCounts counts;
    // Each row is a tier and a number; a tier outside 0 to 6 is no tier and is not counted.
    const auto per_tier = [&](const char* query, std::array<std::int64_t, tier_count>& into,
                              std::optional<Key> key) {
        SQLite::Statement rows(impl_->db, query);
        if (key) {
            rows.bind(1, key_to_sql(*key));
        }
        while (rows.executeStep()) {
            if (const auto tier = tier_from_index(rows.getColumn(0).getInt64())) {
                into.at(static_cast<std::size_t>(tier_index(*tier))) = rows.getColumn(1).getInt64();
            }
        }
    };
    per_tier("SELECT tier, COUNT(*) FROM nodes GROUP BY tier", counts.nodes, std::nullopt);
    per_tier("SELECT tier, COUNT(*) FROM moves GROUP BY tier", counts.moves, std::nullopt);
    per_tier("SELECT tier, games FROM nodes WHERE key = ?1", counts.start_games,
             Position::start().key());
    SQLite::Statement positions(impl_->db,
                                "SELECT COUNT(*), COUNT(name), COUNT(eval_depth) FROM positions");
    if (positions.executeStep()) {
        counts.positions = positions.getColumn(0).getInt64();
        counts.named = positions.getColumn(1).getInt64();
        counts.evaluated = positions.getColumn(2).getInt64();
    }
    return counts;
}

void OpeningStore::dump(std::ostream& out) const {
    for (const auto& [key, value] : impl_->meta.entries) {
        if (key != "built_at" && key != "pipeline_version") {
            out << "meta\t" << key << '\t' << value << '\n';
        }
    }
    dump_table(impl_->db, out, "position",
               "SELECT key, fen, eco, name, eval_cp, eval_mate, eval_depth, best_move "
               "FROM positions WHERE key BETWEEN ?1 AND ?2 ORDER BY key",
               {0});
    dump_table(impl_->db, out, "node",
               "SELECT key, tier, games, white, draws, black FROM nodes "
               "WHERE key BETWEEN ?1 AND ?2 ORDER BY key, tier",
               {0});
    dump_table(impl_->db, out, "move",
               "SELECT key, tier, move, child, white, draws, black, badges FROM moves "
               "WHERE key BETWEEN ?1 AND ?2 ORDER BY key, tier, move",
               {0, 3});
}

} // namespace chesscoach
