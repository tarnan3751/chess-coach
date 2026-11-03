// chess-coach-cli stats (stats.hpp). The counts are the store's (OpeningStore::counts).
#include "stats.hpp"

#include <chesscoach/tier.hpp>

#include <fmt/format.h>

#include <cstddef>
#include <ostream>

namespace chesscoach::cli {

Result<void> run_stats(const StatsOptions& options, std::ostream& out) {
    auto store = open_store(options.db);
    if (!store) {
        return tl::unexpected(store.error());
    }
    // meta as the dump writes it: by key, without the two keys that change with every build.
    for (const auto& [key, value] : store->meta().entries) {
        if (key == "built_at" || key == "pipeline_version") {
            continue;
        }
        out << key << ": " << value << '\n';
    }
    const auto counts = store->counts();
    for (int index = 0; index < tier_count; ++index) {
        // Tier's values are the indices 0 to tier_count - 1.
        const auto tier = static_cast<Tier>(index);
        const auto i = static_cast<std::size_t>(index);
        out << fmt::format("tier {} ({}): {} nodes, {} moves, {} games at the start\n", index,
                           tier_label(tier), counts.nodes.at(i), counts.moves.at(i),
                           counts.start_games.at(i));
    }
    out << "named positions: " << counts.named << '\n';
    out << "evaluated positions: " << counts.evaluated << '\n';
    return {};
}

} // namespace chesscoach::cli
