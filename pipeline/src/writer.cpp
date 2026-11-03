// write_database (writer.hpp).
#include "writer.hpp"

#include <chesscoach/key.hpp>
#include <chesscoach/opening_schema.hpp>

#include "move_code.hpp"

#include <SQLiteCpp/SQLiteCpp.h>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>

namespace chesscoach::pipeline {

namespace {

constexpr std::size_t rows_per_transaction = 50000;

// Commits every rows_per_transaction rows and at the end.
class Batcher {
  public:
    explicit Batcher(SQLite::Database& db) : db_(&db) {}

    void row() {
        if (!transaction_) {
            transaction_ = std::make_unique<SQLite::Transaction>(*db_);
        }
        if (++rows_ == rows_per_transaction) {
            commit();
        }
    }

    void commit() {
        if (transaction_) {
            transaction_->commit();
            transaction_.reset();
        }
        rows_ = 0;
    }

  private:
    SQLite::Database* db_;
    std::unique_ptr<SQLite::Transaction> transaction_;
    std::size_t rows_ = 0;
};

void bind_optional(SQLite::Statement& statement, int index,
                   const std::optional<std::string>& text) {
    if (text) {
        statement.bind(index, *text);
    } else {
        statement.bind(index);
    }
}

void bind_optional(SQLite::Statement& statement, int index,
                   const std::optional<std::int64_t>& value) {
    if (value) {
        statement.bind(index, *value);
    } else {
        statement.bind(index);
    }
}

void write_meta(SQLite::Database& db, const Meta& meta) {
    SQLite::Transaction transaction(db);
    SQLite::Statement insert(db, "INSERT INTO meta (key, value) VALUES (?, ?)");
    for (const auto& [key, value] : meta) {
        insert.bind(1, key);
        insert.bind(2, value);
        insert.exec();
        insert.reset();
    }
    transaction.commit();
}

void write_positions(SQLite::Database& db, const Tree& tree) {
    SQLite::Statement insert(db, "INSERT INTO positions (key, fen, eco, name, eval_cp, eval_mate, "
                                 "eval_depth, best_move) VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    Batcher batcher(db);
    for (const PositionRecord& position : tree.positions) {
        batcher.row();
        insert.bind(1, key_to_sql(position.key));
        insert.bind(2, position.fen);
        bind_optional(insert, 3, position.eco);
        bind_optional(insert, 4, position.name);
        const auto& evaluation = position.evaluation;
        bind_optional(insert, 5, evaluation ? evaluation->cp : std::nullopt);
        bind_optional(insert, 6, evaluation ? evaluation->mate : std::nullopt);
        bind_optional(insert, 7, evaluation ? std::optional(evaluation->depth) : std::nullopt);
        bind_optional(insert, 8, evaluation ? std::optional(evaluation->best_move) : std::nullopt);
        insert.exec();
        insert.reset();
    }
    batcher.commit();
}

void write_nodes_and_moves(SQLite::Database& db, const Tree& tree) {
    SQLite::Statement node_insert(db, "INSERT INTO nodes (key, tier, games, white, draws, black) "
                                      "VALUES (?, ?, ?, ?, ?, ?)");
    Batcher batcher(db);
    for (const SurvivingNode& node : tree.nodes) {
        batcher.row();
        node_insert.bind(1, key_to_sql(node.id.key));
        node_insert.bind(2, std::int64_t{node.id.tier});
        node_insert.bind(3, static_cast<std::int64_t>(node.visits.total()));
        node_insert.bind(4, std::int64_t{node.visits.white});
        node_insert.bind(5, std::int64_t{node.visits.draws});
        node_insert.bind(6, std::int64_t{node.visits.black});
        node_insert.exec();
        node_insert.reset();
    }
    batcher.commit();

    SQLite::Statement move_insert(db, "INSERT INTO moves (key, tier, move, child, white, draws, "
                                      "black, badges) VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    for (const SurvivingNode& node : tree.nodes) {
        for (const MoveCounts& move : node.moves) {
            batcher.row();
            move_insert.bind(1, key_to_sql(node.id.key));
            move_insert.bind(2, std::int64_t{node.id.tier});
            move_insert.bind(3, decode_move(move.move));
            move_insert.bind(4, key_to_sql(move.child));
            move_insert.bind(5, std::int64_t{move.results.white});
            move_insert.bind(6, std::int64_t{move.results.draws});
            move_insert.bind(7, std::int64_t{move.results.black});
            move_insert.bind(8, std::int64_t{move.badges});
            move_insert.exec();
            move_insert.reset();
        }
    }
    batcher.commit();
}

} // namespace

tl::expected<void, std::string> write_database(const std::filesystem::path& path, const Meta& meta,
                                               const Tree& tree) {
    try {
        // NOLINTNEXTLINE(bugprone-signed-bitwise): SQLiteCpp's flags are ints
        SQLite::Database db(path, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        db.exec("PRAGMA journal_mode=OFF");
        db.exec("PRAGMA synchronous=OFF");
        db.exec(std::string(openings_schema_sql()));
        write_meta(db, meta);
        write_positions(db, tree);
        write_nodes_and_moves(db, tree);
    } catch (const std::exception& error) {
        return tl::unexpected(std::string("writing ") + path.string() + ": " + error.what());
    }
    return {};
}

} // namespace chesscoach::pipeline
