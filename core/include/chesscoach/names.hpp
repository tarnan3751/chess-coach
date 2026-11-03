// Opening names (docs/database.md §10): the chess-openings files a.tsv to e.tsv replayed through
// Position, the key of each line's final position mapped to the line's ECO code and name, the
// first line in reading order winning; later lines reaching a named key, and lines whose moves do
// not replay, are counted. The pipeline stores the names in positions rows; the app reads them
// from openings.db, never from here. Reads the five files and nothing else.
#pragma once

#include <chesscoach/key.hpp>

#include <tl/expected.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

namespace chesscoach {

struct Opening {
    std::string eco;
    std::string name;

    friend bool operator==(const Opening&, const Opening&) = default;
};

// The files read, in reading order: the value of meta's openings_tsv key.
inline constexpr std::array<std::string_view, 5> opening_tsv_files{
    "a.tsv", "b.tsv", "c.tsv", "d.tsv", "e.tsv",
};

enum class NameErrorKind : std::uint8_t {
    missing_file, // no regular file of that name in the directory
    unreadable,   // the file could not be read
    bad_header,   // the first line is not eco, name and pgn separated by tabs
};

struct NameError {
    NameErrorKind kind{};
    std::string file; // the base name, one of opening_tsv_files
};

class NameTable;

// Loads the five files of opening_tsv_files from tsv_dir; every one is required.
[[nodiscard]] tl::expected<NameTable, NameError> load_names(const std::filesystem::path& tsv_dir);

class NameTable {
  public:
    // The opening of the line that reached the key first, or nullptr when no line ends there.
    [[nodiscard]] const Opening* find(Key key) const;

    // The named positions.
    [[nodiscard]] std::size_t size() const noexcept { return openings_.size(); }

    // The lines read, headers excluded: size() + collisions() + failures().
    [[nodiscard]] std::size_t lines() const noexcept { return lines_; }

    // Lines whose final position an earlier line had named, which are ignored.
    [[nodiscard]] std::size_t collisions() const noexcept { return collisions_; }

    // Lines that are not three tab-separated fields, or whose moves do not replay.
    [[nodiscard]] std::size_t failures() const noexcept { return failures_; }

  private:
    friend tl::expected<NameTable, NameError> load_names(const std::filesystem::path& tsv_dir);

    std::unordered_map<Key, Opening> openings_;
    std::size_t lines_ = 0;
    std::size_t collisions_ = 0;
    std::size_t failures_ = 0;
};

} // namespace chesscoach
