// The evaluation merge (eval_merge.hpp).
#include "eval_merge.hpp"

#include "chunker.hpp"
#include "work_pool.hpp"
#include "zstd_istream.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <functional>
#include <mutex>
#include <utility>
#include <vector>

namespace chesscoach::pipeline {

namespace {

constexpr std::size_t batch_bytes = std::size_t{4} << 20U;

// The value of the line's "fen" field, read without parsing the JSON.
std::optional<std::string_view> fen_field(std::string_view line) {
    constexpr std::string_view field = R"("fen":")";
    const std::size_t start = line.find(field);
    if (start == std::string_view::npos) {
        return std::nullopt;
    }
    const std::size_t begin = start + field.size();
    const std::size_t end = line.find('"', begin);
    if (end == std::string_view::npos) {
        return std::nullopt;
    }
    return line.substr(begin, end - begin);
}

// UCI_Chess960 castling, the king onto its own rook, as the king's two-square move.
std::optional<std::string_view> castling_rewrite(std::string_view uci) {
    if (uci == "e1h1") {
        return "e1g1";
    }
    if (uci == "e1a1") {
        return "e1c1";
    }
    if (uci == "e8h8") {
        return "e8g8";
    }
    if (uci == "e8a8") {
        return "e8c8";
    }
    return std::nullopt;
}

// The best move as §5.3 writes it: the first move of the line, legal in the position, the
// king-onto-rook castling form rewritten; nullopt when neither form is legal.
std::optional<std::string> best_move(const Position& position, std::string_view first) {
    Position probe = position;
    if (const auto played = probe.play_uci(first)) {
        return played->uci;
    }
    const auto rewritten = castling_rewrite(first);
    if (!rewritten) {
        return std::nullopt;
    }
    probe = position;
    const auto played = probe.play_uci(*rewritten);
    if (!played || !played->san.starts_with("O-O")) {
        return std::nullopt;
    }
    return played->uci;
}

// The evaluation a line's JSON yields under §9's rules 2, 3 and 5: the deepest of its evals,
// ties to the greater knodes, then the earlier in the list, and that eval's first pv, which must
// hold exactly one integer score, cp or mate, and its line. nullopt when an eval lacks an integer
// depth or knodes or the chosen eval has no usable first pv: the merge's unusable line.
struct Choice {
    std::int64_t depth = 0;
    std::int64_t knodes = 0;
    std::optional<std::int64_t> cp;
    std::optional<std::int64_t> mate;
    std::string_view first_move; // into the JSON's line, UCI as the line writes it
};

std::optional<Choice> choose(const nlohmann::json& json) {
    const auto evals = json.find("evals");
    const nlohmann::json* chosen = nullptr;
    std::int64_t chosen_depth = 0;
    std::int64_t chosen_knodes = 0;
    if (evals != json.end() && evals->is_array()) {
        for (const nlohmann::json& eval : *evals) {
            const auto depth = eval.find("depth");
            const auto knodes = eval.find("knodes");
            if (!eval.is_object() || depth == eval.end() || knodes == eval.end() ||
                !depth->is_number_integer() || !knodes->is_number_integer()) {
                chosen = nullptr;
                break;
            }
            const auto d = depth->get<std::int64_t>();
            const auto k = knodes->get<std::int64_t>();
            if (chosen == nullptr || d > chosen_depth || (d == chosen_depth && k > chosen_knodes)) {
                chosen = &eval;
                chosen_depth = d;
                chosen_knodes = k;
            }
        }
    }
    const nlohmann::json* pv = nullptr;
    if (chosen != nullptr) {
        const auto pvs = chosen->find("pvs");
        if (pvs != chosen->end() && pvs->is_array() && !pvs->empty() && pvs->front().is_object()) {
            pv = &pvs->front();
        }
    }
    if (pv == nullptr) {
        return std::nullopt;
    }
    const auto cp = pv->find("cp");
    const auto mate = pv->find("mate");
    const auto text = pv->find("line");
    const bool has_cp = cp != pv->end() && cp->is_number_integer();
    const bool has_mate = mate != pv->end() && mate->is_number_integer();
    if (has_cp == has_mate || text == pv->end() || !text->is_string()) {
        return std::nullopt;
    }
    const std::string_view moves = text->get_ref<const std::string&>();
    Choice choice{
        .depth = chosen_depth,
        .knodes = chosen_knodes,
        .cp = std::nullopt,
        .mate = std::nullopt,
        .first_move = moves.substr(0, moves.find(' ')),
    };
    if (has_cp) {
        choice.cp = cp->get<std::int64_t>();
    } else {
        choice.mate = mate->get<std::int64_t>();
    }
    return choice;
}

struct Batch {
    std::uint64_t sequence = 0;
    std::string text;
};

} // namespace

EvalCounts& EvalCounts::operator+=(const EvalCounts& other) {
    lines += other.lines;
    unreadable += other.unreadable;
    unusable += other.unusable;
    matched += other.matched;
    return *this;
}

bool better(const EvalCandidate& a, const EvalCandidate& b) {
    if (a.evaluation.depth != b.evaluation.depth) {
        return a.evaluation.depth > b.evaluation.depth;
    }
    if (a.knodes != b.knodes) {
        return a.knodes > b.knodes;
    }
    return a.order < b.order;
}

std::optional<EvalCandidate> read_eval_line(std::string_view line, std::uint64_t order,
                                            const Tree& tree, EvalCounts& counts) {
    if (line.empty()) {
        return std::nullopt;
    }
    ++counts.lines;
    const auto fen = fen_field(line);
    if (!fen) {
        ++counts.unreadable;
        return std::nullopt;
    }
    const auto position = Position::from_fen(*fen);
    if (!position) {
        ++counts.unreadable;
        return std::nullopt;
    }
    const Key key = position->key();
    if (tree.find(key) == nullptr) {
        return std::nullopt;
    }
    const nlohmann::json json = nlohmann::json::parse(line, nullptr, false);
    if (json.is_discarded() || !json.is_object()) {
        ++counts.unreadable;
        return std::nullopt;
    }
    ++counts.matched;
    const auto choice = choose(json);
    if (!choice) {
        ++counts.unusable;
        return std::nullopt;
    }
    const auto move = best_move(*position, choice->first_move);
    if (!move) {
        ++counts.unusable;
        return std::nullopt;
    }
    EvalCandidate candidate{.key = key, .knodes = choice->knodes, .order = order, .evaluation = {}};
    candidate.evaluation.depth = choice->depth;
    candidate.evaluation.best_move = *move;
    candidate.evaluation.cp = choice->cp;
    candidate.evaluation.mate = choice->mate;
    return candidate;
}

tl::expected<EvalCounts, std::string> merge_evaluations(const std::filesystem::path& path,
                                                        std::size_t threads, Tree& tree) {
    ZstdStreambuf source(path);
    std::vector<EvalCounts> counts(threads);
    std::vector<std::unordered_map<Key, EvalCandidate>> best(threads);
    const auto produce = [&](const std::function<bool(Batch)>& push) {
        Chunker chunker(source, ChunkKind::lines, batch_bytes);
        std::uint64_t sequence = 0;
        for (std::string text = chunker.next(); !text.empty(); text = chunker.next()) {
            if (!push(Batch{.sequence = sequence++, .text = std::move(text)})) {
                return;
            }
        }
    };
    const auto consume = [&](std::size_t worker, Batch& batch) {
        std::string_view rest = batch.text;
        std::uint64_t line_number = 0;
        while (!rest.empty()) {
            const std::size_t end = rest.find('\n');
            const std::string_view line = rest.substr(0, end);
            rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end + 1);
            const std::uint64_t order = (batch.sequence << 32U) | line_number++;
            auto candidate = read_eval_line(line, order, tree, counts.at(worker));
            if (!candidate) {
                continue;
            }
            auto& mine = best.at(worker);
            const auto found = mine.find(candidate->key);
            if (found == mine.end()) {
                mine.emplace(candidate->key, std::move(*candidate));
            } else if (better(*candidate, found->second)) {
                found->second = std::move(*candidate);
            }
        }
    };
    run_pool<Batch>(threads, produce, consume);
    if (!source.error().empty()) {
        return tl::unexpected(fmt::format("{}: {}", path.string(), source.error()));
    }
    std::unordered_map<Key, EvalCandidate> chosen;
    EvalCounts total;
    for (std::size_t worker = 0; worker < threads; ++worker) {
        total += counts.at(worker);
        for (auto& [key, candidate] : best.at(worker)) {
            const auto found = chosen.find(key);
            if (found == chosen.end()) {
                chosen.emplace(key, std::move(candidate));
            } else if (better(candidate, found->second)) {
                found->second = std::move(candidate);
            }
        }
    }
    for (auto& [key, candidate] : chosen) {
        tree.find(key)->evaluation = std::move(candidate.evaluation);
    }
    return total;
}

} // namespace chesscoach::pipeline
