// chess-coach-cli eval (eval.hpp).
#include "eval.hpp"

#include <fmt/format.h>

#include <sstream>
#include <string>

namespace chesscoach::cli {

Result<void> run_eval(const EvalOptions& options, std::ostream& out) {
    const auto resolved = resolve_position(options.fen, options.moves);
    if (!resolved) {
        return tl::unexpected(resolved.error());
    }
    const Position& position = resolved->position;
    std::ostringstream text;
    text << "fen: " << position.fen() << '\n';
    text << "key: " << position.key() << '\n';
    if (options.db) {
        auto store = open_store(*options.db);
        if (!store) {
            return tl::unexpected(store.error());
        }
        if (const auto row = store->position(position.key());
            row && row->eval_depth && row->best_move) {
            const auto best = position.san_of(*row->best_move);
            const std::string score = row->eval_mate
                                          ? fmt::format("mate {}", *row->eval_mate)
                                          : fmt::format("cp {}", row->eval_cp.value_or(0));
            text << fmt::format("stored: {} depth {} best {}\n", score, *row->eval_depth,
                                best ? *best : *row->best_move);
        }
    }
    auto profiles = open_profiles(options.data_dir);
    if (!profiles) {
        return tl::unexpected(profiles.error());
    }
    const auto answer = search_through_cache(*profiles, position, options.query, options.engine);
    if (!answer) {
        return tl::unexpected(answer.error());
    }
    text << "engine: " << answer->search.engine << '\n';
    for (const auto& line : answer->search.lines) {
        text << fmt::format("{}: {}\n", line.multipv, line_text(position, line));
    }
    text << "cached: " << (answer->cached ? "yes" : "no") << '\n';
    out << text.str();
    return {};
}

} // namespace chesscoach::cli
