// The database worker (db_worker.hpp).
#include "db_worker.hpp"

#include <chesscoach/opening_locator.hpp>
#include <chesscoach/position.hpp>
#include <chesscoach/tier.hpp>

#include <QFileInfo>
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

// Why a file did not open, as a sentence.
QString describe(StoreError error) {
    switch (error) {
    case StoreError::missing_file:
        return DbWorker::tr("The file is not there.");
    case StoreError::unreadable:
        return DbWorker::tr("It is not a database file, or it is damaged.");
    case StoreError::no_meta:
    case StoreError::schema_mismatch:
        return DbWorker::tr("It is not a chess-coach opening database.");
    case StoreError::wrong_version:
        return DbWorker::tr("It was made for another version of chess-coach.");
    }
    return DbWorker::tr("It could not be opened.");
}

// Why an import was refused, as a sentence or two, the second the way forward when there is one.
QString describe(const ImportError& error) {
    switch (error.reason) {
    case ImportError::Reason::missing_source:
        return DbWorker::tr("The file is not there.");
    case ImportError::Reason::not_openings:
        return describe(error.store_error);
    case ImportError::Reason::newer_version:
        return DbWorker::tr("It was made for a newer chess-coach. Update chess-coach to use it.");
    case ImportError::Reason::already_in_place:
        return DbWorker::tr("It is the opening database chess-coach uses already.");
    case ImportError::Reason::inside_data_dir:
        return DbWorker::tr("It is inside chess-coach's own data folder. Move it somewhere else, "
                            "then import it.");
    case ImportError::Reason::unwritable:
        return DbWorker::tr("chess-coach cannot write to its data folder. Check that the disk has "
                            "free space.");
    }
    return {};
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
        if (top_sum.has_value()) {
            const auto played = std::ranges::find(top, row.move, &MoveRow::move);
            const qint64 total = played != top.end() ? played->total() : 0;
            const qint64 sum = top_sum.value_or(0);
            move.top_share =
                sum > 0 ? 100.0 * static_cast<double>(total) / static_cast<double>(sum) : 0.0;
        }
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
        emit database_failed(tr("The opening database %1 could not be opened. %2 Import one with "
                                "File > Import database.")
                                 .arg(path, describe(store.error())));
        return;
    }
    const std::string* version = store->meta().find("data_version");
    const std::string* months = store->meta().find("months");
    const QString data_version = version != nullptr ? QString::fromStdString(*version) : QString();
    const QString month_list = months != nullptr ? QString::fromStdString(*months) : QString();
    store_.emplace(std::move(*store));
    emit database_opened(data_version, month_list);
}

void DbWorker::import_database(const QString& data_dir, const QString& source,
                               const QString& current) {
    // The import replaces the file the store reads, so the store lets it go first.
    store_.reset();
    const auto imported = import_opening_database(std::filesystem::path(data_dir.toStdString()),
                                                  std::filesystem::path(source.toStdString()));
    if (!imported) {
        emit database_import_failed(
            tr("%1 was not imported. %2")
                .arg(QFileInfo(source).fileName(), describe(imported.error())));
        // The database it had, as it was; a --db that named no file stays without a store, and
        // the failure is all there is to report.
        if (!current.isEmpty() && QFileInfo::exists(current)) {
            open_database(current);
        }
        return;
    }
    const QString path = QString::fromStdString(imported->path.string());
    emit database_imported(path, imported->data_version, QString::fromStdString(imported->months));
    open_database(path);
}

void DbWorker::query(const QVector<quint64>& line_keys, int tier) {
    const auto at = tier_from_index(tier);
    if (!store_.has_value() || !at.has_value() || line_keys.isEmpty()) {
        return;
    }
    try {
        emit position_ready(explore(*store_, line_keys, *at));
    } catch (const std::exception&) {
        // The store throws only when the file fails after a successful open (an I/O error, a
        // file corrupted on disk); the database is then closed.
        store_.reset();
        emit database_failed(tr("The opening database could not be read. It may be damaged. "
                                "Import another one with File > Import database."));
    }
}

} // namespace chesscoach::app
