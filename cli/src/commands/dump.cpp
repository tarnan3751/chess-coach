// chess-coach-cli dump (dump.hpp).
#include "dump.hpp"

namespace chesscoach::cli {

Result<void> run_dump(const DumpOptions& options, std::ostream& out) {
    auto store = open_located_store(options.db, options.data_dir);
    if (!store) {
        return tl::unexpected(store.error());
    }
    store->dump(out);
    return {};
}

} // namespace chesscoach::cli
