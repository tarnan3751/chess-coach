// The database worker (db_worker.hpp).
#include "db_worker.hpp"

#include <chesscoach/position.hpp>
#include <chesscoach/tier.hpp>

#include <QCoreApplication>
#include <QList>
#include <algorithm>
#include <exception>
#include <filesystem>
#include <optional>
#include <ranges>
#include <string>
#include <vector>

namespace chesscoach::app {

namespace {

QString describe(StoreError error) {
    switch (error) {
    case StoreError::missing_file:
        return QCoreApplication::translate("chesscoach::app::DbWorker", "no such file");
    case StoreError::unreadable:
        return QCoreApplication::translate("chesscoach::app::DbWorker",
                                           "not a readable SQLite database");
    case StoreError::no_meta:
        return QCoreApplication::translate("chesscoach::app::DbWorker",
                                           "no meta table or schema_version");
    case StoreError::wrong_version:
        return QCoreApplication::translate("chesscoach::app::DbWorker",
                                           "a schema_version this build does not read");
    case StoreError::schema_mismatch:
        return QCoreApplication::translate("chesscoach::app::DbWorker",
                                           "its tables do not match the schema");
    }
    return QCoreApplication::translate("chesscoach::app::DbWorker", "cannot open");
}

// The answer for the position at the end of the line at the tier.
ExploreData explore(const OpeningStore& store, const QVector<quint64>& line_keys, Tier tier) {
    ExploreData data;
    data.key = line_keys.back();
    data.tier = tier_index(tier);
    data.position = store.position(data.key);
    const std::string fen = data.position ? data.position->fen : std::string();
    data.fen = QString::fromStdString(fen);
    for (const quint64 key : std::views::reverse(line_keys)) {
        if (const auto row = store.position(key); row && row->name) {
            data.name = QString::fromStdString(row->eco.value_or("") + " " + *row->name);
            break;
        }
    }
    data.node = store.node(data.key, tier);
    if (!data.node) {
        return data;
    }

    std::vector<MoveRow> top;
    std::optional<qint64> top_sum;
    if (store.node(data.key, top_tier)) {
        top = store.moves(data.key, top_tier);
        qint64 sum = 0;
        for (const MoveRow& row : top) {
            sum += row.total();
        }
        top_sum = sum;
    }
    data.top_tier_sum = top_sum;
    // SAN needs the position, rebuilt from its stored FEN; a node's key always has a positions
    // row (docs/database.md §6.2), and a move that is not legal there keeps its UCI.
    const auto position = Position::from_fen(fen);
    const bool white_to_move = position.has_value() && position->side_to_move() == Color::white;
    for (const MoveRow& row : store.moves(data.key, tier)) {
        ExploreMove move;
        move.uci = QString::fromStdString(row.move);
        std::string san = row.move;
        if (position.has_value()) {
            san = position->san_of(row.move).value_or(row.move);
        }
        move.san = QString::fromStdString(san);
        move.badges = row.badges;
        move.total = row.total();
        move.white = row.white;
        move.draws = row.draws;
        move.black = row.black;
        if (move.total > 0) {
            const auto wins = static_cast<double>(white_to_move ? row.white : row.black);
            move.score_percent = 100.0 * (wins + (static_cast<double>(row.draws) / 2)) /
                                 static_cast<double>(move.total);
        }
        if (top_sum.has_value()) {
            const auto played = std::ranges::find(top, row.move, &MoveRow::move);
            const qint64 total = played != top.end() ? played->total() : 0;
            const qint64 sum = top_sum.value_or(0);
            move.top_share =
                sum > 0 ? 100.0 * static_cast<double>(total) / static_cast<double>(sum) : 0.0;
        }
        move.child_in_book = store.node(row.child, tier).has_value();
        data.moves.append(move);
    }
    std::ranges::sort(data.moves, [](const ExploreMove& a, const ExploreMove& b) {
        return a.total != b.total ? a.total > b.total : a.uci < b.uci;
    });
    return data;
}

} // namespace

void register_explore_types() {
    qRegisterMetaType<ExploreMove>();
    qRegisterMetaType<ExploreData>();
    qRegisterMetaType<QVector<quint64>>();
}

DbWorker::DbWorker(QObject* parent) : QObject(parent) {}

void DbWorker::open_database(const QString& path) {
    store_.reset();
    auto store = OpeningStore::open(std::filesystem::path(path.toStdString()));
    if (!store) {
        emit database_failed(tr("Cannot open %1: %2").arg(path, describe(store.error())));
        return;
    }
    const std::string* version = store->meta().find("data_version");
    store_.emplace(std::move(*store));
    emit database_opened(version != nullptr ? QString::fromStdString(*version) : QString());
}

void DbWorker::query(const QVector<quint64>& line_keys, int tier) {
    const auto at = tier_from_index(tier);
    if (!store_.has_value() || !at.has_value() || line_keys.isEmpty()) {
        return;
    }
    try {
        emit position_ready(explore(*store_, line_keys, *at));
    } catch (const std::exception& error) {
        // The store throws only when the file fails after a successful open (an I/O error, a
        // file corrupted on disk); the database is then closed.
        store_.reset();
        emit database_failed(tr("Reading the database failed: %1").arg(error.what()));
    }
}

} // namespace chesscoach::app
