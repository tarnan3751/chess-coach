// `chess-coach-pipeline trim` (docs/reference/pipeline.md, **Trim**): the owner's inputs reduced
// to what the build reads, so twelve months fit on disk, each written under its own name in an
// output directory. A month file keeps the games the build's header checks could admit or count
// themselves (check_headers, not a restatement of it), with the tags those checks read and the
// first max_ply moves; the evaluation file keeps, line for line, the evaluation the merge would
// choose (trimmed_eval_line). Both stream through zstd in and out, holding one game or one line
// at a time; the build's semantics are untouched, which the trim tests show on the sample corpus.
#pragma once

#include "game_filter.hpp"

#include <tl/expected.hpp>

#include <cstdint>
#include <filesystem>
#include <istream>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace chesscoach::pipeline {

struct TrimOptions {
    std::filesystem::path out_dir;
    std::vector<std::filesystem::path> inputs;
    std::uint32_t max_ply = 30; // the build's default knob
};

enum class TrimKind : std::uint8_t { games, evals };

// What a file is by its name: a month of games (lichess_db_standard_rated_YYYY-MM.pgn.zst), the
// evaluation database (lichess_db_eval.jsonl.zst), or neither.
[[nodiscard]] std::optional<TrimKind> trim_kind(const std::filesystem::path& path);

// Whether a game is written: check_headers admits it, or refuses it for a variant or a result,
// the two checks the build makes again from the tags written, so it counts those games as it
// would have; a game unrated, of another speed or without a decimal rating is dropped.
[[nodiscard]] bool trim_keeps(const GameHeaders& headers);

// A game's movetext as trim writes it: its first max_ply moves as the build's parser reads them,
// without comments, NAGs ($n), move numbers (1. and 1..., glued to a move or not) and glyphs
// standing alone (!, ?!), numbered again from 1., then the result; a result token in the
// movetext ends it.
[[nodiscard]] std::string trimmed_movetext(std::string_view movetext, std::uint32_t max_ply,
                                           std::string_view result);

struct GameTrimCounts {
    std::uint64_t seen = 0;
    std::uint64_t kept = 0;
};

// Copies the games trim_keeps keeps from in to out: per game the tags Event, Result, WhiteElo,
// BlackElo and, when present, Variant, SetUp and FEN, in that order, a blank line, the trimmed
// movetext and a blank line. A game is a run of tag lines and the moves after them, which start
// at a blank line or a movetext line; it ends as the build's chunker ends it, at a line beginning
// "[Event ", or at any line beginning "[" once its moves have started. One whose moves never
// start, or with a tag line the parser rejects, both of which the build counts malformed, is
// seen and dropped. Stops reading once out has failed.
GameTrimCounts trim_games(std::istream& in, std::ostream& out, std::uint32_t max_ply);

struct EvalTrimCounts {
    std::uint64_t lines = 0;
    std::uint64_t passed_through = 0; // written unchanged: no usable evaluation to trim to
};

// Copies the evaluation file line for line, each line trimmed (trimmed_eval_line) or, when it
// has nothing to trim to, unchanged. Stops reading once out has failed.
EvalTrimCounts trim_evals(std::istream& in, std::ostream& out);

// Trims every input into out_dir under its own name, through <name>.partial renamed when the file
// is whole: an input of no known kind, two inputs of one name and an output that exists are
// refused before anything is written, and an input that fails leaves nothing behind for its file
// (the files before it stay). One report line per file: what it held and kept, its bytes
// in and out and the seconds it took.
[[nodiscard]] tl::expected<void, std::string> run_trim(const TrimOptions& options,
                                                       std::ostream& report);

} // namespace chesscoach::pipeline
