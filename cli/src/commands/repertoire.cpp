// chess-coach-cli repertoire (repertoire.hpp).
#include "repertoire.hpp"

#include <chesscoach/pgn_import.hpp>
#include <chesscoach/repertoire.hpp>
#include <chesscoach/tier.hpp>

#include "engine_common.hpp"
#include "profiles.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <utility>
#include <vector>

namespace chesscoach::cli {

namespace {

// The moves of a line from the start position in SAN, separated by spaces; "(start)" for none.
std::string line_san(const std::vector<std::string>& line) {
    auto position = Position::start();
    std::string text;
    for (const auto& uci : line) {
        const auto played = position.play_uci(uci);
        if (!text.empty()) {
            text += ' ';
        }
        if (!played) {
            text += uci;
            break;
        }
        text += played->san;
    }
    return text.empty() ? "(start)" : text;
}

// A share of the start position's games as a percentage with one decimal.
std::string share_text(double mass, double total) {
    return fmt::format("{:.1f}%", total > 0.0 ? mass / total * 100.0 : 0.0);
}

tl::unexpected<CommandError> store_failure(ProfileError error) {
    return fail(fmt::format("profiles.db {}", describe(error)));
}

Result<void> add_line(ProfileStore& store, std::int64_t profile, Color colour,
                      const std::string& moves, std::ostream& out) {
    const auto resolved = resolve_position(std::nullopt, moves);
    if (!resolved) {
        return tl::unexpected(resolved.error());
    }
    int added = 0;
    int changed = 0;
    int unchanged = 0;
    auto position = Position::start();
    for (const auto& uci : resolved->moves) {
        if (position.side_to_move() == colour) {
            const auto set = store.set_choice(
                profile, colour,
                RepertoireChoice{.key = position.key(), .fen = position.fen(), .move = uci});
            if (!set) {
                return store_failure(set.error());
            }
            switch (*set) {
            case ChoiceChange::added:
                ++added;
                break;
            case ChoiceChange::changed:
                ++changed;
                break;
            case ChoiceChange::unchanged:
                ++unchanged;
                break;
            }
        }
        // Each move played once already, in resolve_position.
        static_cast<void>(position.play_uci(uci));
    }
    out << fmt::format("added: {}\nchanged: {}\nunchanged: {}\n", added, changed, unchanged);
    return {};
}

Result<void> remove_line(ProfileStore& store, std::int64_t profile, Color colour,
                         const std::string& moves, std::ostream& out) {
    const auto resolved = resolve_position(std::nullopt, moves);
    if (!resolved) {
        return tl::unexpected(resolved.error());
    }
    if (resolved->position.side_to_move() != colour) {
        return fail("the line ends where the other colour is to move");
    }
    const auto removed = store.remove_choice(profile, colour, resolved->position.key());
    if (!removed) {
        return store_failure(removed.error());
    }
    out << "removed: " << (*removed ? 1 : 0) << '\n';
    return {};
}

void list_nodes(const ProfileStore& store, std::int64_t profile, Color colour, std::ostream& out) {
    for (const auto& node : store.repertoire_nodes(profile, colour)) {
        std::string san = node.choice.move;
        if (const auto position = Position::from_fen(node.choice.fen)) {
            if (auto written = position->san_of(node.choice.move)) {
                san = std::move(*written);
            }
        }
        out << fmt::format("{} {} {} {}\n", node.choice.key, node.choice.move, san,
                           node.choice.fen);
    }
}

Result<void> import_pgn(ProfileStore& store, std::int64_t profile, Color colour,
                        const std::string& path, std::ostream& out) {
    std::ifstream in(path);
    if (!in) {
        return fail(fmt::format("cannot read {}", path));
    }
    const auto read = import_repertoire_pgn(in, colour);
    if (!read) {
        return fail(fmt::format("{}: {}", path,
                                read.error() == PgnImportError::malformed_header
                                    ? "a header without its closing bracket or quote"
                                    : "a header or a move longer than the parser reads"));
    }
    const auto applied = store.import_choices(profile, colour, read->choices);
    if (!applied) {
        return store_failure(applied.error());
    }
    out << fmt::format("games: {}\nvariations: {}\nchoices: {}\nadded: {}\nunchanged: {}\n"
                       "conflicts in the file: {}\nconflicts with the repertoire: {}\n"
                       "unparsable: {}\n",
                       read->games, read->variations, read->choices.size(), applied->added,
                       applied->unchanged, read->conflicts, applied->conflicts, read->unparsable);
    return {};
}

Result<void> report_coverage(const ProfileStore& store, std::int64_t profile, Color colour,
                             const RepertoireOptions& options, std::ostream& out) {
    const auto tier = tier_from_index(options.tier);
    if (!tier) {
        return fail(fmt::format("tier {} is not one of 0 to 6", options.tier));
    }
    const auto openings = open_located_store(options.db, options.data_dir);
    if (!openings) {
        return tl::unexpected(openings.error());
    }
    const auto coverage = compute_coverage(*openings, store.repertoire(profile, colour), colour,
                                           *tier, CoverageConfig{.max_plies = options.depth});
    if (options.action == "coverage") {
        out << fmt::format("covered: {}\nvisited: {}\ngaps: {}\n",
                           share_text(coverage.covered, coverage.total), coverage.positions_visited,
                           coverage.gaps.size());
    }
    const auto shown = static_cast<std::size_t>(std::max(options.limit, 0));
    for (std::size_t i = 0; i < coverage.gaps.size() && i < shown; ++i) {
        const auto& gap = coverage.gaps.at(i);
        out << fmt::format("{} {} | {}\n", share_text(gap.mass, coverage.total), line_san(gap.line),
                           gap.fen);
    }
    return {};
}

} // namespace

Result<void> run_repertoire(const RepertoireOptions& options, std::ostream& out,
                            const Clock& clock) {
    const auto colour = parse_colour(options.colour);
    if (!colour) {
        return tl::unexpected(colour.error());
    }
    // Checked before profiles.db is opened, so a rejected call creates nothing.
    if ((options.action == "coverage" || options.action == "gaps") &&
        (options.depth < 0 || options.depth > max_depth)) {
        return fail(fmt::format("--depth {} is not from 0 to {}", options.depth, max_depth));
    }
    auto opened = open_profile(options.data_dir, options.profile, clock);
    if (!opened) {
        return tl::unexpected(opened.error());
    }
    auto& store = opened->store;
    const std::int64_t profile = opened->profile.id;
    if (options.action == "add") {
        return add_line(store, profile, *colour, options.moves, out);
    }
    if (options.action == "remove") {
        return remove_line(store, profile, *colour, options.moves, out);
    }
    if (options.action == "list") {
        list_nodes(store, profile, *colour, out);
        return {};
    }
    if (options.action == "import") {
        return import_pgn(store, profile, *colour, options.pgn, out);
    }
    return report_coverage(store, profile, *colour, options, out);
}

} // namespace chesscoach::cli
