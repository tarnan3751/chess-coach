// run_build (build.hpp): the passes and the stages after them, in order.
#include "build.hpp"

#include <chesscoach/names.hpp>
#include <chesscoach/opening_schema.hpp>
#include <chesscoach/tier.hpp>
#include <chesscoach/version.hpp>

#include "aggregate.hpp"
#include "badges.hpp"
#include "chunker.hpp"
#include "eval_merge.hpp"
#include "game_scanner.hpp"
#include "sketch.hpp"
#include "tree.hpp"
#include "work_pool.hpp"
#include "writer.hpp"
#include "zstd_istream.hpp"

#include <fmt/chrono.h>
#include <fmt/format.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <limits>
#include <memory>
#include <mutex>
#include <system_error>
#include <vector>

namespace chesscoach::pipeline {

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::size_t batch_bytes = std::size_t{4} << 20U;
constexpr std::uint64_t progress_every = 1000000;

double seconds_since(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

// Prints "<pass>: N games read, S skipped" each time the games read pass a multiple of
// progress_every.
class Progress {
  public:
    Progress(std::ostream& out, std::string_view pass) : out_(&out), pass_(pass) {}

    void add(std::uint64_t games, std::uint64_t skipped) {
        const std::uint64_t before = games_.fetch_add(games);
        const std::uint64_t now_skipped = skipped_.fetch_add(skipped) + skipped;
        if ((before + games) / progress_every != before / progress_every) {
            const std::scoped_lock lock(mutex_);
            *out_ << fmt::format("{}: {} games read, {} skipped\n", pass_, before + games,
                                 now_skipped);
            out_->flush();
        }
    }

  private:
    std::ostream* out_;
    std::string pass_;
    std::atomic<std::uint64_t> games_{0};
    std::atomic<std::uint64_t> skipped_{0};
    std::mutex mutex_;
};

// Streams every input through the scanners, one per worker, calling after_batch(worker) once a
// batch's games are scanned. An unreadable input is an error.
tl::expected<void, std::string>
scan_inputs(const std::vector<MonthInput>& inputs, std::vector<GameScanner>& scanners,
            Progress& progress, const std::function<void(std::size_t, const ScannedGame&)>& sink,
            const std::function<void(std::size_t)>& after_batch) {
    for (const MonthInput& input : inputs) {
        ZstdStreambuf source(input.path);
        const auto produce = [&](const std::function<bool(std::string)>& push) {
            Chunker chunker(source, ChunkKind::games, batch_bytes);
            for (std::string batch = chunker.next(); !batch.empty(); batch = chunker.next()) {
                if (!push(std::move(batch))) {
                    return;
                }
            }
        };
        const auto consume = [&](std::size_t worker, std::string& batch) {
            GameScanner& scanner = scanners.at(worker);
            const ScanCounts before = scanner.counts();
            for (const std::string_view game : split_games(batch)) {
                scanner.scan(game, [&](const ScannedGame& scanned) { sink(worker, scanned); });
            }
            after_batch(worker);
            const ScanCounts& after = scanner.counts();
            const std::uint64_t skipped = after.skipped.total() - before.skipped.total();
            progress.add(after.admitted_total() - before.admitted_total() + skipped, skipped);
        };
        run_pool<std::string>(scanners.size(), produce, consume);
        if (!source.error().empty()) {
            return tl::unexpected(fmt::format("{}: {}", input.path.string(), source.error()));
        }
    }
    return {};
}

ScanCounts total_counts(const std::vector<GameScanner>& scanners) {
    ScanCounts total;
    for (const GameScanner& scanner : scanners) {
        total += scanner.counts();
    }
    return total;
}

std::string join_months(const std::vector<MonthInput>& inputs) {
    std::string months;
    for (const MonthInput& input : inputs) {
        months += months.empty() ? input.month : "," + input.month;
    }
    return months;
}

std::string tier_bounds() {
    std::string bounds;
    for (const int bound : tier_lower_bounds) {
        bounds += bounds.empty() ? std::to_string(bound) : "," + std::to_string(bound);
    }
    return bounds;
}

std::string_view names_error(NameErrorKind kind) {
    switch (kind) {
    case NameErrorKind::missing_file:
        return "missing";
    case NameErrorKind::unreadable:
        return "unreadable";
    case NameErrorKind::bad_header:
        return "not an eco, name, pgn header";
    }
    return "unknown";
}

std::string utc_now() {
    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    return fmt::format("{:%Y-%m-%dT%H:%M:%SZ}", now);
}

// The base names of the TSV files load_names reads, in reading order (meta's openings_tsv).
std::string names_files() {
    std::string joined;
    for (const std::string_view file : opening_tsv_files) {
        joined += joined.empty() ? std::string(file) : "," + std::string(file);
    }
    return joined;
}

// The survivors as the tree the later stages fill in: one position per surviving key.
tl::expected<Tree, std::string> make_tree(Aggregate::Survivors survivors) {
    Tree tree;
    tree.nodes = std::move(survivors.nodes);
    for (const SurvivingNode& node : tree.nodes) {
        if (!tree.positions.empty() && tree.positions.back().key == node.id.key) {
            continue;
        }
        const auto fen = survivors.fens.find(node.id.key);
        if (fen == survivors.fens.end()) {
            return tl::unexpected(
                fmt::format("internal error: no FEN for the surviving key {}", node.id.key));
        }
        tree.by_key.emplace(node.id.key, tree.positions.size());
        tree.positions.push_back(PositionRecord{
            .key = node.id.key,
            .fen = std::move(fen->second),
            .evaluation = std::nullopt,
            .eco = std::nullopt,
            .name = std::nullopt,
        });
    }
    return tree;
}

} // namespace

tl::expected<void, std::string> run_build(const BuildOptions& options, Report& report,
                                          std::ostream& progress) {
    const auto start = Clock::now();
    const auto inputs = month_inputs(options.inputs);
    if (!inputs) {
        return tl::unexpected(inputs.error());
    }
    const std::filesystem::path partial = partial_path(options.output);
    std::error_code error;
    if (std::filesystem::exists(options.output, error) || std::filesystem::exists(partial, error)) {
        return tl::unexpected(fmt::format("{} or {} exists already; the build writes a new file",
                                          options.output.string(), partial.string()));
    }
    if (options.min_games < 1 || options.min_games > std::numeric_limits<std::uint16_t>::max()) {
        return tl::unexpected("--min-games must be between 1 and 65535");
    }
    std::uint64_t compressed = 0;
    for (const MonthInput& input : *inputs) {
        const auto size = std::filesystem::file_size(input.path, error);
        if (error) {
            return tl::unexpected(fmt::format("{}: {}", input.path.string(), error.message()));
        }
        compressed += size;
    }
    const std::size_t threads = options.threads == 0 ? 1 : options.threads;
    const std::size_t sketch_bytes =
        options.sketch_bytes != 0 ? options.sketch_bytes : default_sketch_bytes(compressed);
    const auto min_games = static_cast<std::uint16_t>(options.min_games);

    // Pass one: the sketch.
    auto sketch = std::make_unique<VisitSketch>(sketch_bytes, min_games);
    std::vector<GameScanner> first(threads, GameScanner(options.max_ply, nullptr));
    Progress first_progress(progress, "pass one");
    const auto pass_one = scan_inputs(
        *inputs, first, first_progress,
        [&](std::size_t, const ScannedGame& game) {
            for (const Key key : game.keys) {
                sketch->add(game.tier, key);
            }
        },
        [](std::size_t) {});
    if (!pass_one) {
        return tl::unexpected(pass_one.error());
    }
    const ScanCounts counts = total_counts(first);
    const double pass_one_seconds = seconds_since(start);

    // Pass two: exact counts for the candidates.
    Aggregate aggregate;
    std::vector<std::unique_ptr<BatchCounts>> batches;
    std::vector<GameScanner> second;
    batches.reserve(threads);
    second.reserve(threads);
    for (std::size_t worker = 0; worker < threads; ++worker) {
        batches.push_back(std::make_unique<BatchCounts>(*sketch, min_games, aggregate));
        BatchCounts* batch = batches.back().get();
        second.emplace_back(options.max_ply, [batch](std::uint8_t tier, Key key) {
            return batch->fen_wanted(tier, key);
        });
    }
    Progress second_progress(progress, "pass two");
    const auto pass_two = scan_inputs(
        *inputs, second, second_progress,
        [&](std::size_t worker, const ScannedGame& game) { batches.at(worker)->add(game); },
        [&](std::size_t worker) { batches.at(worker)->flush(aggregate); });
    if (!pass_two) {
        return tl::unexpected(pass_two.error());
    }
    if (total_counts(second) != counts) {
        return tl::unexpected("the inputs read differently in pass two than in pass one");
    }
    const double pass_two_seconds = seconds_since(start) - pass_one_seconds;
    batches.clear();
    sketch.reset();

    // Pruning, then the stages that fill the survivors in.
    const std::size_t candidates = aggregate.candidate_count();
    auto tree = make_tree(aggregate.take_survivors(options.min_games));
    if (!tree) {
        return tl::unexpected(tree.error());
    }
    std::optional<EvalCounts> eval_counts;
    if (options.eval) {
        auto merged = merge_evaluations(*options.eval, threads, *tree);
        if (!merged) {
            return tl::unexpected(merged.error());
        }
        eval_counts = *merged;
    }
    std::string tsv_files = "none";
    std::uint64_t named = 0;
    std::uint64_t name_collisions = 0;
    std::uint64_t name_failures = 0;
    if (options.names) {
        auto names = load_names(*options.names);
        if (!names) {
            return tl::unexpected(fmt::format("{}: cannot read the opening names ({})",
                                              (*options.names / names.error().file).string(),
                                              names_error(names.error().kind)));
        }
        for (PositionRecord& position : tree->positions) {
            if (const Opening* opening = names->find(position.key)) {
                position.eco = opening->eco;
                position.name = opening->name;
                ++named;
            }
        }
        name_collisions = names->collisions();
        name_failures = names->failures();
        tsv_files = names_files();
    }
    compute_badges(*tree);

    Meta meta{
        {"schema_version", std::to_string(opening_schema_version)},
        {"data_version", std::to_string(options.data_version)},
        {"months", join_months(*inputs)},
        {"min_games", std::to_string(options.min_games)},
        {"max_ply", std::to_string(options.max_ply)},
        {"tiers", tier_bounds()},
        {"built_at", utc_now()},
        {"pipeline_version", to_string(current_version())},
        {"eval_db", options.eval ? options.eval->filename().string() : "none"},
        {"openings_tsv", tsv_files},
        {"games_admitted", std::to_string(counts.admitted_total())},
        {"games_skipped", std::to_string(counts.skipped.total())},
    };
    for (std::size_t tier = 0; tier < counts.admitted.size(); ++tier) {
        meta.emplace_back(fmt::format("admitted_tier_{}", tier),
                          std::to_string(counts.admitted.at(tier)));
    }
    for (const SkipReason reason : all_skip_reasons) {
        meta.emplace_back(fmt::format("skipped_{}", name(reason)),
                          std::to_string(counts.skipped.count(reason)));
    }
    const auto written = write_database(partial, meta, *tree);
    if (!written) {
        std::filesystem::remove(partial, error);
        return tl::unexpected(written.error());
    }
    std::filesystem::rename(partial, options.output, error);
    if (error) {
        std::filesystem::remove(partial, error);
        return tl::unexpected(fmt::format("renaming {}: {}", partial.string(), error.message()));
    }

    const double total_seconds = seconds_since(start);
    report.add("output", options.output.string());
    for (const auto& [key, value] : meta) {
        report.add(key, value);
    }
    report.add("threads", threads);
    report.add("sketch_bytes", sketch_bytes);
    report.add("sketch_width", sketch_bytes / (VisitSketch::rows * sizeof(std::uint16_t)));
    report.add("candidates", candidates);
    report.add("nodes", tree->nodes.size());
    std::uint64_t moves = 0;
    std::array<std::uint64_t, static_cast<std::size_t>(tier_count)> per_tier{};
    for (const SurvivingNode& node : tree->nodes) {
        moves += node.moves.size();
        per_tier.at(node.id.tier) += 1;
    }
    for (std::size_t tier = 0; tier < per_tier.size(); ++tier) {
        report.add(fmt::format("nodes_tier_{}", tier), per_tier.at(tier));
    }
    report.add("moves", moves);
    report.add("positions", tree->positions.size());
    if (eval_counts) {
        report.add("eval_lines", eval_counts->lines);
        report.add("eval_unreadable", eval_counts->unreadable);
        report.add("eval_matched", eval_counts->matched);
        report.add("eval_unusable", eval_counts->unusable);
    }
    std::uint64_t evaluated = 0;
    for (const PositionRecord& position : tree->positions) {
        if (position.evaluation) {
            ++evaluated;
        }
    }
    report.add("positions_evaluated", evaluated);
    report.add("positions_named", named);
    report.add("name_collisions", name_collisions);
    report.add("name_failures", name_failures);
    const std::uint64_t games = counts.admitted_total() + counts.skipped.total();
    report.add("games_read", games);
    report.add("pass_one_seconds", fmt::format("{:.3f}", pass_one_seconds));
    report.add("pass_two_seconds", fmt::format("{:.3f}", pass_two_seconds));
    report.add("total_seconds", fmt::format("{:.3f}", total_seconds));
    report.add("games_per_second",
               fmt::format("{:.0f}", static_cast<double>(games) / total_seconds));
    report.add("peak_rss_bytes", peak_rss_bytes());
    return {};
}

} // namespace chesscoach::pipeline
