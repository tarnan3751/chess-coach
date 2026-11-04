// ZstdOstreambuf (zstd_ostream.hpp): streaming compression into one zstd file, one frame.
#include "zstd_ostream.hpp"

#include <fmt/format.h>
#include <fmt/std.h>

#include <utility>

namespace chesscoach::pipeline {

void ZstdOstreambuf::FileCloser::operator()(std::FILE* file) const {
    static_cast<void>(std::fclose(file));
}

void ZstdOstreambuf::ContextFreer::operator()(ZSTD_CCtx* context) const {
    static_cast<void>(ZSTD_freeCCtx(context));
}

ZstdOstreambuf::ZstdOstreambuf(const std::filesystem::path& path, int level, int workers)
    : file_(std::fopen(path.c_str(), "wb")), context_(ZSTD_createCCtx()), in_(ZSTD_CStreamInSize()),
      out_(ZSTD_CStreamOutSize()) {
    if (!file_) {
        fail(fmt::format("cannot create {}", path));
        return;
    }
    if (!context_) {
        fail("cannot create a zstd compression context");
        return;
    }
    const std::size_t set = ZSTD_CCtx_setParameter(context_.get(), ZSTD_c_compressionLevel, level);
    if (ZSTD_isError(set) != 0) {
        fail(fmt::format("zstd refuses level {}: {}", level, ZSTD_getErrorName(set)));
        return;
    }
    // A zstd library without threads refuses workers; it then compresses in this thread.
    static_cast<void>(ZSTD_CCtx_setParameter(context_.get(), ZSTD_c_nbWorkers, workers));
    setp(in_.data(), in_.data() + in_.size());
}

void ZstdOstreambuf::fail(std::string message) {
    if (error_.empty()) {
        error_ = std::move(message);
    }
    setp(nullptr, nullptr);
}

bool ZstdOstreambuf::compress(ZSTD_EndDirective directive) {
    ZSTD_inBuffer input{
        .src = pbase(),
        .size = static_cast<std::size_t>(pptr() - pbase()),
        .pos = 0,
    };
    while (true) {
        ZSTD_outBuffer output{.dst = out_.data(), .size = out_.size(), .pos = 0};
        const std::size_t remaining =
            ZSTD_compressStream2(context_.get(), &output, &input, directive);
        if (ZSTD_isError(remaining) != 0) {
            fail(fmt::format("zstd compression failed: {}", ZSTD_getErrorName(remaining)));
            return false;
        }
        if (output.pos > 0) {
            if (std::fwrite(out_.data(), 1, output.pos, file_.get()) != output.pos) {
                fail("write error");
                return false;
            }
            written_ += output.pos;
        }
        const bool consumed = input.pos == input.size;
        if (directive == ZSTD_e_continue ? consumed : (consumed && remaining == 0)) {
            break;
        }
    }
    setp(in_.data(), in_.data() + in_.size());
    return true;
}

ZstdOstreambuf::int_type ZstdOstreambuf::overflow(int_type ch) {
    if (!error_.empty() || finished_ || !compress(ZSTD_e_continue)) {
        return traits_type::eof();
    }
    if (!traits_type::eq_int_type(ch, traits_type::eof())) {
        *pptr() = traits_type::to_char_type(ch);
        pbump(1);
    }
    return traits_type::not_eof(ch);
}

bool ZstdOstreambuf::finish() {
    if (!error_.empty()) {
        return false;
    }
    if (finished_) {
        return true;
    }
    if (!compress(ZSTD_e_end)) {
        return false;
    }
    finished_ = true;
    setp(nullptr, nullptr);
    if (std::fflush(file_.get()) != 0 || std::fclose(file_.release()) != 0) {
        fail("write error");
        return false;
    }
    return true;
}

} // namespace chesscoach::pipeline
