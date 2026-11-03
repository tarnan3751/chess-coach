// chess-coach-cli explore (explore.hpp).
#include "explore.hpp"

#include <chesscoach/badges.hpp>
#include <chesscoach/score.hpp>
#include <chesscoach/tier.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>

namespace chesscoach::cli {

namespace {

// The badges as letters in the order B, P, D, or "-" for none.
std::string badge_letters(Badges badges) {
    std::string letters;
    if (badges.has(Badge::book)) {
        letters += 'B';
    }
    if (badges.has(Badge::popular)) {
        letters += 'P';
    }
    if (badges.has(Badge::dubious)) {
        letters += 'D';
    }
    return letters.empty() ? "-" : letters;
}

// The SAN of a stored move, or an error naming it when the position has no such legal move.
Result<std::string> san_of(const Position& position, const std::string& uci) {
    auto san = position.san_of(uci);
    if (!san) {
        return fail(fmt::format("the database holds the move {}, which is not legal in {}", uci,
                                position.fen()));
    }
    return std::move(*san);
}

// The --score columns: sound, loss, expected, replies, penalty, score.
std::string score_columns(const ScoreComponents& components) {
    return fmt::format(" {} {} {:.3f} {} {:.3f} {:.3f}", components.sound ? "yes" : "no",
                       components.loss ? std::to_string(*components.loss) : std::string("-"),
                       components.expected_score, components.replies_needed, components.penalty,
                       components.score);
}

} // namespace

Result<void> run_explore(const ExploreOptions& options, std::ostream& out) {
    const auto tier = tier_from_index(options.tier);
    if (!tier) {
        return fail(fmt::format("tier {} is not one of 0 to 6", options.tier));
    }
    const auto resolved = resolve_position(options.fen, options.moves);
    if (!resolved) {
        return tl::unexpected(resolved.error());
    }
    auto store = open_store(options.db);
    if (!store) {
        return tl::unexpected(store.error());
    }
    const Position& position = resolved->position;
    const Key key = position.key();

    out << "fen: " << position.fen() << '\n';
    out << "key: " << key << '\n';
    if (const auto name = deepest_name(*store, resolved->path)) {
        out << "name: " << name->eco << ' ' << name->name << '\n';
    }
    if (const auto row = store->position(key); row && row->eval_depth && row->best_move) {
        const auto best = san_of(position, *row->best_move);
        if (!best) {
            return tl::unexpected(best.error());
        }
        const std::string score = row->eval_mate ? fmt::format("mate {}", *row->eval_mate)
                                                 : fmt::format("cp {}", row->eval_cp.value_or(0));
        out << fmt::format("eval: {} depth {} best {} {}\n", score, *row->eval_depth, *best,
                           *row->best_move);
    }

    if (!store->node(key, *tier)) {
        out << fmt::format("off book at tier {} ({})\n", tier_index(*tier), tier_label(*tier));
        return {};
    }
    auto moves = store->moves(key, *tier);
    // The most played first; equal totals by move text.
    std::ranges::sort(moves, [](const auto& a, const auto& b) {
        return a.total() != b.total() ? a.total() > b.total() : a.move < b.move;
    });
    std::int64_t sum = 0;
    for (const auto& move : moves) {
        sum += move.total();
    }
    std::map<std::string, ScoreComponents, std::less<>> scores;
    if (options.score) {
        for (auto& components : score_moves(*store, key, *tier)) {
            scores.emplace(components.move, std::move(components));
        }
    }
    for (const auto& move : moves) {
        const auto san = san_of(position, move.move);
        if (!san) {
            return tl::unexpected(san.error());
        }
        const std::int64_t total = move.total();
        const bool in_book = store->node(move.child, *tier).has_value();
        std::string columns;
        if (options.score) {
            const auto components = scores.find(move.move);
            if (components == scores.end()) {
                return fail(fmt::format("no score for the move {}: the position has no "
                                        "positions row",
                                        move.move));
            }
            columns = score_columns(components->second);
        }
        out << fmt::format("{} {} {} {} {}% {}% {}% {}% {}{}\n", *san, move.move,
                           badge_letters(move.badges), total, percent(move.white, total),
                           percent(move.draws, total), percent(move.black, total),
                           percent(total, sum), in_book ? "yes" : "no", columns);
    }
    return {};
}

} // namespace chesscoach::cli
