// VisitSketch: pass one's count-min sketch of (tier, key) visits (docs/database.md §8). Four
// rows of saturating 16-bit counters; a visit adds one to one counter per row, and the estimate
// is the least of the four. Counters stop at the saturation value (min_games), so the guarantee
// is estimate(t, k) >= min(true visits of (t, k), saturation): every (tier, key) that reaches
// min_games is a candidate, whatever the sketch's size, which sets only how many false
// positives pass two counts and discards. add() is safe from many threads at once, and the
// final counters do not depend on the order of the adds. A size the sketch cannot take is an
// error value (create).
#pragma once

#include <tl/expected.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace chesscoach::pipeline {

// The largest sketch --sketch-mib accepts: 16 GiB (docs/reference/pipeline.md, **The sketch**).
inline constexpr std::size_t max_sketch_bytes = std::size_t{1} << 34U;

class VisitSketch {
  public:
    static constexpr std::size_t rows = 4;

    // A sketch of total_bytes bytes (rows * width counters of two bytes), total_bytes a power
    // of two from 64 to max_sketch_bytes, its counters saturating at saturation, at least 1; or
    // why it cannot be made.
    [[nodiscard]] static tl::expected<VisitSketch, std::string> create(std::size_t total_bytes,
                                                                       std::uint16_t saturation);

    void add(std::uint8_t tier, std::uint64_t key);
    [[nodiscard]] std::uint16_t estimate(std::uint8_t tier, std::uint64_t key) const;

    [[nodiscard]] std::size_t width() const { return width_; }
    [[nodiscard]] std::size_t bytes() const { return width_ * rows * sizeof(std::uint16_t); }
    [[nodiscard]] std::uint16_t saturation() const { return saturation_; }

  private:
    VisitSketch(std::size_t total_bytes, std::uint16_t saturation);

    [[nodiscard]] std::size_t cell(std::size_t row, std::uint8_t tier, std::uint64_t key) const;

    std::size_t width_;
    unsigned shift_;
    std::uint16_t saturation_;
    std::vector<std::atomic<std::uint16_t>> cells_;
};

// The default sketch size for inputs of compressed_bytes in total: the least power of two of at
// least twice the input, within 1 MiB and 1 GiB.
[[nodiscard]] std::size_t default_sketch_bytes(std::uint64_t compressed_bytes);

} // namespace chesscoach::pipeline
