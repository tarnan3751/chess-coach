// chess-coach-cli name (name.hpp).
#include "name.hpp"

namespace chesscoach::cli {

Result<void> run_name(const NameOptions& options, std::ostream& out) {
    if (!options.fen && !options.moves) {
        return fail("name needs --fen or --moves");
    }
    const auto resolved = resolve_position(options.fen, options.moves);
    if (!resolved) {
        return tl::unexpected(resolved.error());
    }
    auto store = open_located_store(options.db, options.data_dir);
    if (!store) {
        return tl::unexpected(store.error());
    }
    if (const auto name = deepest_name(*store, resolved->path)) {
        out << name->eco << ' ' << name->name << '\n';
    } else {
        out << "unnamed\n";
    }
    return {};
}

} // namespace chesscoach::cli
