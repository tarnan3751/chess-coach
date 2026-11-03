// Chunker: cuts a decompressed stream into batches of whole records for the worker threads, so
// the stream is read once, in order, by one thread, and parsed by many. A games batch ends where
// a line begins with "[Event " (Lichess writes Event first in every game); a lines batch ends at
// a line end. split_games cuts a games batch into the text of each game. Holds one batch and the
// tail of the next in memory, never the stream (CLAUDE.md, **pipeline/ streams and never
// stores**).
#pragma once

#include <cstddef>
#include <cstdint>
#include <streambuf>
#include <string>
#include <string_view>
#include <vector>

namespace chesscoach::pipeline {

enum class ChunkKind : std::uint8_t {
    games, // cut before a line beginning with "[Event "
    lines, // cut after a '\n'
};

class Chunker {
  public:
    // Batches of about batch_bytes bytes, more when one record is longer.
    Chunker(std::streambuf& source, ChunkKind kind, std::size_t batch_bytes);

    // The next batch of whole records, in stream order; empty once the stream is exhausted. The
    // last batch holds whatever the stream ended with, a partial record included.
    [[nodiscard]] std::string next();

  private:
    // The end of the last whole record in pending_, never 0, or npos when there is none.
    [[nodiscard]] std::size_t last_cut() const;

    std::streambuf* source_;
    ChunkKind kind_;
    std::size_t batch_bytes_;
    std::string pending_;
    bool exhausted_ = false;
};

// The games of a games batch: each view starts at a line beginning with "[Event " (the first
// may start wherever the batch does) and runs to the next. Views into batch.
[[nodiscard]] std::vector<std::string_view> split_games(std::string_view batch);

} // namespace chesscoach::pipeline
