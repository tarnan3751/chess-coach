// Repertoire coverage (chesscoach/repertoire.hpp). The walk goes ply by ply; each ply's positions
// are a map by key, so transpositions at one ply merge and are processed in key order, and each
// position's replies are taken by move text. Every floating-point value is one operation in its
// own statement, in that order, so the sums are the same on every platform.
#include <chesscoach/repertoire.hpp>

#include <algorithm>
#include <cstdint>
#include <map>
#include <utility>

namespace chesscoach {

namespace {

// A position on the walk at one ply: the mass that reaches it and the first line that does.
struct Walker {
    double mass = 0.0;
    Position position;
    std::vector<std::string> line;
};

// Adds the mass of a path to the next ply's positions: a position reached twice at one ply keeps
// the bytewise-smaller line, and its masses add.
void reach(std::map<Key, Walker>& next, Key key, double mass, Position position,
           std::vector<std::string> line) {
    const auto found = next.find(key);
    if (found == next.end()) {
        next.emplace(
            key, Walker{.mass = mass, .position = std::move(position), .line = std::move(line)});
        return;
    }
    const double merged = found->second.mass + mass;
    found->second.mass = merged;
    if (line < found->second.line) {
        found->second.line = std::move(line);
        found->second.position = std::move(position);
    }
}

// Adds a gap: a position met again at a later ply keeps its first line and FEN, and its masses
// add.
void add_gap(std::map<Key, Gap>& gaps, Key key, const Walker& walker) {
    const auto found = gaps.find(key);
    if (found == gaps.end()) {
        gaps.emplace(key, Gap{
                              .key = key,
                              .fen = walker.position.fen(),
                              .line = walker.line,
                              .mass = walker.mass,
                          });
        return;
    }
    const double merged = found->second.mass + walker.mass;
    found->second.mass = merged;
}

} // namespace

Coverage compute_coverage(const Repertoire& repertoire, Color colour, const ChildLookup& lookup,
                          const CoverageConfig& config) {
    Coverage coverage;
    double covered = 0.0;
    std::map<Key, Gap> gaps;
    std::map<Key, Walker> frontier;
    const Position start = Position::start();
    frontier.emplace(start.key(), Walker{.mass = 1.0, .position = start, .line = {}});

    for (int ply = 0; !frontier.empty(); ++ply) {
        std::map<Key, Walker> next;
        for (const auto& [key, walker] : frontier) {
            ++coverage.positions_visited;
            if (ply >= config.max_plies) {
                covered += walker.mass; // the depth is reached
                continue;
            }
            const ChildRows rows = lookup(key);
            if (!rows.node) {
                covered += walker.mass; // off book at the tier
                continue;
            }
            if (walker.position.side_to_move() == colour) {
                const auto chosen = repertoire.find(key);
                if (chosen == repertoire.end()) {
                    add_gap(gaps, key, walker);
                    continue;
                }
                auto child = walker.position;
                const auto played = child.play_uci(chosen->second);
                if (!played) {
                    add_gap(gaps, key, walker); // a chosen move that is not legal answers nothing
                    continue;
                }
                auto line = walker.line;
                line.push_back(played->uci);
                reach(next, played->child, walker.mass, std::move(child), std::move(line));
                continue;
            }

            std::vector<const MoveRow*> replies;
            replies.reserve(rows.moves.size());
            std::int64_t sum = 0;
            for (const MoveRow& reply : rows.moves) {
                replies.push_back(&reply);
                sum += reply.total();
            }
            if (sum == 0) {
                covered += walker.mass; // no replies
                continue;
            }
            std::ranges::sort(replies,
                              [](const MoveRow* a, const MoveRow* b) { return a->move < b->move; });
            for (const MoveRow* reply : replies) {
                const double weighted = walker.mass * static_cast<double>(reply->total());
                const double mass = weighted / static_cast<double>(sum);
                if (mass < config.min_mass) {
                    covered += mass; // truncated: too rare to follow
                    continue;
                }
                auto child = walker.position;
                const auto played = child.play_uci(reply->move);
                if (!played) {
                    covered += mass; // a stored reply that is not legal cannot be followed
                    continue;
                }
                auto line = walker.line;
                line.push_back(played->uci);
                reach(next, played->child, mass, std::move(child), std::move(line));
            }
        }
        frontier = std::move(next);
    }

    coverage.covered = covered;
    coverage.gaps.reserve(gaps.size());
    for (auto& [key, gap] : gaps) {
        coverage.gaps.push_back(std::move(gap));
    }
    std::ranges::sort(coverage.gaps, [](const Gap& a, const Gap& b) {
        return a.mass != b.mass ? a.mass > b.mass : a.line < b.line;
    });
    return coverage;
}

Coverage compute_coverage(const OpeningStore& store, const Repertoire& repertoire, Color colour,
                          Tier tier, const CoverageConfig& config) {
    const ChildLookup lookup = [&](Key key) {
        ChildRows rows;
        rows.node = store.node(key, tier);
        if (rows.node) {
            rows.moves = store.moves(key, tier);
        }
        return rows;
    };
    return compute_coverage(repertoire, colour, lookup, config);
}

} // namespace chesscoach
