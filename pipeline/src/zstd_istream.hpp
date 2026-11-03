// ZstdStreambuf: a std::streambuf that reads a zstd-compressed file and yields its decompressed
// bytes as they are read, through ZSTD_decompressStream with ZSTD_DStreamInSize and
// ZSTD_DStreamOutSize buffers, so a dump of tens of gigabytes is never held or written whole
// (CLAUDE.md, **pipeline/ streams and never stores**). A file that cannot be opened, a corrupt
// frame and a stream that ends inside a frame end the stream early and set error(); they are
// values for the caller to report, never a crash.
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

class ZstdStreambuf : public std::streambuf {
  public:
    explicit ZstdStreambuf(const std::filesystem::path& path);

    // Why the stream ended early: empty while the stream is good and after it ended cleanly at
    // the end of its last frame.
    [[nodiscard]] const std::string& error() const { return error_; }

    // The compressed bytes read from the file so far.
    [[nodiscard]] std::uint64_t compressed_bytes_read() const { return compressed_read_; }

  protected:
    int_type underflow() override;

  private:
    struct FileCloser {
        void operator()(std::FILE* file) const;
    };
    struct StreamFreer {
        void operator()(ZSTD_DStream* stream) const;
    };

    void fail(std::string message);

    std::unique_ptr<std::FILE, FileCloser> file_;
    std::unique_ptr<ZSTD_DStream, StreamFreer> stream_;
    std::vector<char> in_;
    std::vector<char> out_;
    ZSTD_inBuffer input_{.src = nullptr, .size = 0, .pos = 0};
    std::size_t last_hint_ = 0; // ZSTD_decompressStream's last result: 0 at the end of a frame
    bool frame_seen_ = false;
    bool output_full_ = false;
    bool done_ = false;
    std::uint64_t compressed_read_ = 0;
    std::string error_;
};

} // namespace chesscoach::pipeline
