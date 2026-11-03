// The evaluation merge of docs/database.md §9: the Lichess evaluation database streamed once,
// after pass two, its lines read in parallel. A line's "fen" field is read first; only a line
// whose normalized key survives is parsed in full (nlohmann::json), its deepest evaluation
// chosen (ties to the greater knodes, then the earlier), and its first pv's score and first move
// kept, the move rewritten from UCI_Chess960 castling and checked legal. Of the lines of one key
// the best by the same rule wins, the earlier line on a full tie, so the result does not depend
// on the number of threads.
#pragma once

#include <chesscoach/position.hpp>

#include "tree.hpp"

#include <tl/expected.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace chesscoach::pipeline {

struct EvalCounts {
    std::uint64_t lines = 0;      // lines read
    std::uint64_t unreadable = 0; // no "fen" field, a FEN that does not parse, malformed JSON
    std::uint64_t unusable = 0;   // a surviving line whose chosen evaluation cannot be used
    std::uint64_t matched = 0;    // lines of a surviving position that were used or compared

    EvalCounts& operator+=(const EvalCounts& other);
};

// One usable line's choice for its key; order is the line's place in the file.
struct EvalCandidate {
    Key key = 0;
    std::int64_t knodes = 0;
    std::uint64_t order = 0;
    Evaluation evaluation;
};

// Whether a is preferred to b: the greater depth, then the greater knodes, then the earlier.
[[nodiscard]] bool better(const EvalCandidate& a, const EvalCandidate& b);

// Reads one line: a candidate when its key survives and it is usable; counts the outcome.
[[nodiscard]] std::optional<EvalCandidate>
read_eval_line(std::string_view line, std::uint64_t order, const Tree& tree, EvalCounts& counts);

// Streams the file and sets the evaluation of every surviving position it has a usable line for.
[[nodiscard]] tl::expected<EvalCounts, std::string>
merge_evaluations(const std::filesystem::path& path, std::size_t threads, Tree& tree);

} // namespace chesscoach::pipeline
