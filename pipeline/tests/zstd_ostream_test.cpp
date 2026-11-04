// Unit tests for ZstdOstreambuf: a stream larger than its buffers, written with worker threads,
// reads back whole through ZstdStreambuf; an empty stream is a valid frame; a file that cannot be
// created is an error, never a crash (pipeline; unit).
#include "zstd_ostream.hpp"

#include "test_files.hpp"
#include "zstd_istream.hpp"

#include <catch2/catch_test_macros.hpp>

#include <istream>
#include <iterator>
#include <ostream>
#include <string>

using chesscoach::pipeline::ZstdOstreambuf;
using chesscoach::pipeline::ZstdStreambuf;
namespace testing = chesscoach::pipeline::testing;

namespace {

std::string read_back(const std::filesystem::path& path) {
    ZstdStreambuf buffer(path);
    std::istream in(&buffer);
    std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    CHECK(buffer.error().empty());
    return text;
}

} // namespace

TEST_CASE("ZstdOstreambuf writes a stream larger than its buffers in one frame", "[zstd]") {
    const auto dir = testing::scratch_dir("zstd_ostream_large");
    std::string text;
    for (int i = 0; i < 100000; ++i) {
        text += "[Event \"Rated Blitz game\"]\n\n1. e4 e5 2. Nf3 " + std::to_string(i) + " 1-0\n\n";
    }
    {
        ZstdOstreambuf buffer(dir / "out.zst", 9, 4);
        std::ostream out(&buffer);
        out << text;
        CHECK(out.good());
        CHECK(buffer.finish());
        CHECK(buffer.error().empty());
        CHECK(buffer.compressed_bytes_written() > 0);
        CHECK(buffer.compressed_bytes_written() < text.size());
    }
    CHECK(read_back(dir / "out.zst") == text);
}

TEST_CASE("ZstdOstreambuf writes an empty stream as a frame", "[zstd]") {
    const auto dir = testing::scratch_dir("zstd_ostream_empty");
    {
        ZstdOstreambuf buffer(dir / "empty.zst", 9, 4);
        CHECK(buffer.finish());
    }
    CHECK(std::filesystem::file_size(dir / "empty.zst") > 0);
    CHECK(read_back(dir / "empty.zst").empty());
}

TEST_CASE("ZstdOstreambuf reports a file it cannot create", "[zstd]") {
    const auto dir = testing::scratch_dir("zstd_ostream_missing");
    ZstdOstreambuf buffer(dir / "missing" / "out.zst", 9, 4);
    CHECK_FALSE(buffer.error().empty());
    std::ostream out(&buffer);
    out << "text";
    CHECK_FALSE(out.good());
    CHECK_FALSE(buffer.finish());
}
