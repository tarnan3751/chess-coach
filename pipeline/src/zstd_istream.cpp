// ZstdStreambuf (zstd_istream.hpp): streaming decompression of one zstd file, frame after frame.
#include "zstd_istream.hpp"

#include <fmt/format.h>
#include <fmt/std.h>

#include <utility>

namespace chesscoach::pipeline {

void ZstdStreambuf::FileCloser::operator()(std::FILE* file) const {
    static_cast<void>(std::fclose(file));
}

void ZstdStreambuf::StreamFreer::operator()(ZSTD_DStream* stream) const {
    static_cast<void>(ZSTD_freeDStream(stream));
}

ZstdStreambuf::ZstdStreambuf(const std::filesystem::path& path)
    : file_(std::fopen(path.c_str(), "rb")), stream_(ZSTD_createDStream()),
      in_(ZSTD_DStreamInSize()), out_(ZSTD_DStreamOutSize()) {
    if (!file_) {
        fail(fmt::format("cannot open {}", path));
    } else if (!stream_) {
        fail("cannot create a zstd decompression stream");
    }
}

void ZstdStreambuf::fail(std::string message) {
    error_ = std::move(message);
    done_ = true;
}

ZstdStreambuf::int_type ZstdStreambuf::underflow() {
    if (gptr() < egptr()) {
        return traits_type::to_int_type(*gptr());
    }
    while (!done_) {
        // A full output buffer may leave decoded bytes inside the stream: flush them before
        // reading more input.
        if (input_.pos == input_.size && !output_full_) {
            const std::size_t read = std::fread(in_.data(), 1, in_.size(), file_.get());
            if (read == 0) {
                if (std::ferror(file_.get()) != 0) {
                    fail("read error");
                } else if (!frame_seen_) {
                    fail("not a zstd stream: no frame");
                } else if (last_hint_ != 0) {
                    fail("truncated zstd stream: the input ends inside a frame");
                } else {
                    done_ = true;
                }
                break;
            }
            compressed_read_ += read;
            input_ = ZSTD_inBuffer{.src = in_.data(), .size = read, .pos = 0};
        }
        ZSTD_outBuffer output{.dst = out_.data(), .size = out_.size(), .pos = 0};
        const std::size_t hint = ZSTD_decompressStream(stream_.get(), &output, &input_);
        if (ZSTD_isError(hint) != 0) {
            fail(fmt::format("corrupt zstd stream: {}", ZSTD_getErrorName(hint)));
            break;
        }
        frame_seen_ = true;
        last_hint_ = hint;
        output_full_ = output.pos == output.size;
        if (output.pos > 0) {
            setg(out_.data(), out_.data(), out_.data() + output.pos);
            return traits_type::to_int_type(*gptr());
        }
    }
    return traits_type::eof();
}

} // namespace chesscoach::pipeline
