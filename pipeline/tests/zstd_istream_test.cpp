// Unit tests for ZstdStreambuf: a known stream decompresses to its text, several frames in a row
// read as one stream, and a missing, empty, truncated or corrupt file ends the stream with an
// error instead of a crash (pipeline; unit).
#include "zstd_istream.hpp"

#include "test_files.hpp"

#include <catch2/catch_test_macros.hpp>

#include <istream>
#include <iterator>
#include <string>

using chesscoach::pipeline::ZstdStreambuf;
namespace testing = chesscoach::pipeline::testing;

namespace {

std::string read_all(ZstdStreambuf& buffer) {
    std::istream in(&buffer);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::string sample_text() {
    std::string text;
    for (int i = 0; i < 20000; ++i) {
        text += "[Event \"Rated Blitz game\"]\n\n1. e4 e5 2. Nf3 Nc6 " + std::to_string(i) + "\n\n";
    }
    return text;
}

} // namespace

TEST_CASE("ZstdStreambuf decompresses a stream larger than its buffers", "[zstd]") {
    const auto dir = testing::scratch_dir("zstd_known");
    const std::string text = sample_text();
    testing::write_file(dir / "known.zst", testing::zstd_compress(text));
    ZstdStreambuf buffer(dir / "known.zst");
    CHECK(read_all(buffer) == text);
    CHECK(buffer.error().empty());
    CHECK(buffer.compressed_bytes_read() == std::filesystem::file_size(dir / "known.zst"));
}

TEST_CASE("ZstdStreambuf reads concatenated frames as one stream", "[zstd]") {
    const auto dir = testing::scratch_dir("zstd_frames");
    testing::write_file(dir / "frames.zst",
                        testing::zstd_compress("first ") + testing::zstd_compress("second"));
    ZstdStreambuf buffer(dir / "frames.zst");
    CHECK(read_all(buffer) == "first second");
    CHECK(buffer.error().empty());
}

TEST_CASE("ZstdStreambuf reports a truncated stream", "[zstd]") {
    const auto dir = testing::scratch_dir("zstd_truncated");
    const std::string compressed = testing::zstd_compress(sample_text());
    testing::write_file(dir / "truncated.zst", compressed.substr(0, compressed.size() / 2));
    ZstdStreambuf buffer(dir / "truncated.zst");
    const std::string text = read_all(buffer);
    CHECK(text.size() < sample_text().size());
    CHECK(buffer.error().find("truncated") != std::string::npos);
}

TEST_CASE("ZstdStreambuf reports a missing, empty or corrupt file", "[zstd]") {
    const auto dir = testing::scratch_dir("zstd_bad");
    {
        ZstdStreambuf buffer(dir / "missing.zst");
        CHECK(read_all(buffer).empty());
        CHECK(buffer.error().find("cannot open") != std::string::npos);
    }
    {
        testing::write_file(dir / "empty.zst", "");
        ZstdStreambuf buffer(dir / "empty.zst");
        CHECK(read_all(buffer).empty());
        CHECK_FALSE(buffer.error().empty());
    }
    {
        testing::write_file(dir / "corrupt.zst", "this is not zstd at all");
        ZstdStreambuf buffer(dir / "corrupt.zst");
        CHECK(read_all(buffer).empty());
        CHECK(buffer.error().find("corrupt") != std::string::npos);
    }
}
