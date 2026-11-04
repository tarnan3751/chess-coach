// ZstdOstreambuf: a std::streambuf that compresses what is written to it into a zstd file as it
// is written, through ZSTD_compressStream2 with ZSTD_CStreamInSize and ZSTD_CStreamOutSize
// buffers, so a trimmed dump is never held whole (CLAUDE.md, **pipeline/ streams and never
// stores**). One frame per file, ended by finish(), which closes the file. A file that cannot be
// created and a write that fails are errors for the caller to report, never a crash.
#pragma once

#include <zstd.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <streambuf>
#include <string>
#include <vector>

namespace chesscoach::pipeline {

class ZstdOstreambuf : public std::streambuf {
  public:
    // Compresses at the level with the given number of worker threads, when the zstd library was
    // built with threads (otherwise in the calling thread, with the same frame format).
    ZstdOstreambuf(const std::filesystem::path& path, int level, int workers);

    // Compresses what is buffered, ends the frame and closes the file; false, with error() set,
    // when a step failed now or before.
    [[nodiscard]] bool finish();

    // Why the stream failed: empty while it is good.
    [[nodiscard]] const std::string& error() const { return error_; }

    // The compressed bytes written to the file so far.
    [[nodiscard]] std::uint64_t compressed_bytes_written() const { return written_; }

  protected:
    int_type overflow(int_type ch) override;

  private:
    struct FileCloser {
        void operator()(std::FILE* file) const;
    };
    struct ContextFreer {
        void operator()(ZSTD_CCtx* context) const;
    };

    // Feeds the put area to the compressor until it is consumed (and, to end the frame, until
    // the frame is flushed whole), writing what comes out; false on an error.
    bool compress(ZSTD_EndDirective directive);
    void fail(std::string message);

    std::unique_ptr<std::FILE, FileCloser> file_;
    std::unique_ptr<ZSTD_CCtx, ContextFreer> context_;
    std::vector<char> in_;
    std::vector<char> out_;
    std::uint64_t written_ = 0;
    bool finished_ = false;
    std::string error_;
};

} // namespace chesscoach::pipeline
