// The build's options, as `chess-coach-pipeline build` takes them (docs/reference/pipeline.md,
// **Running it**), and the checks on its inputs: every input is a month file named
// lichess_db_standard_rated_YYYY-MM.pgn.zst, no two of the same month (docs/database.md §6.1).
#pragma once

#include <tl/expected.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace chesscoach::pipeline {

struct BuildOptions {
    std::vector<std::filesystem::path> inputs;
    std::filesystem::path output;
    std::optional<std::filesystem::path> eval;  // absent: no evaluation merge
    std::optional<std::filesystem::path> names; // absent: no names (--names none)
    std::uint32_t min_games = 100;
    std::uint32_t max_ply = 30;
    std::size_t threads = 1;
    std::uint64_t data_version = 0;
    std::size_t sketch_bytes = 0; // 0: default_sketch_bytes of the inputs
};

struct MonthInput {
    std::string month; // YYYY-MM
    std::filesystem::path path;
};

// The YYYY-MM of a file named lichess_db_standard_rated_YYYY-MM.pgn.zst, or nullopt.
[[nodiscard]] std::optional<std::string> month_of(const std::filesystem::path& path);

// The inputs in ascending month order, or why they are refused: a name that is not a month
// file, or two inputs of the same month.
[[nodiscard]] tl::expected<std::vector<MonthInput>, std::string>
month_inputs(const std::vector<std::filesystem::path>& inputs);

// The file the build writes before renaming it to the output: <output>.partial.
[[nodiscard]] std::filesystem::path partial_path(const std::filesystem::path& output);

} // namespace chesscoach::pipeline
