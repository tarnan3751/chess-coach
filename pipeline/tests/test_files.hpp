// Scratch files for the pipeline's tests, under the build directory's test-scratch/ (never the
// source tree), and a zstd compressor for them (pipeline; tests).
#pragma once

#include <zstd.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace chesscoach::pipeline::testing {

// A fresh, empty directory named after the test, under the build's test-scratch/.
inline std::filesystem::path scratch_dir(std::string_view test) {
    const std::filesystem::path dir = std::filesystem::path(CHESSCOACH_TEST_SCRATCH_DIR) / test;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

inline std::string zstd_compress(std::string_view text, int level = 3) {
    std::string compressed(ZSTD_compressBound(text.size()), '\0');
    const std::size_t size =
        ZSTD_compress(compressed.data(), compressed.size(), text.data(), text.size(), level);
    if (ZSTD_isError(size) != 0) {
        throw std::runtime_error(ZSTD_getErrorName(size));
    }
    compressed.resize(size);
    return compressed;
}

// The value of an optional a test has just required, or a test failure: a check the static
// analyzer can follow, which Catch2's REQUIRE is not.
template <typename T> const T& engaged(const std::optional<T>& value) {
    if (!value) {
        throw std::logic_error("an expected value is absent");
    }
    return *value;
}

inline void write_file(const std::filesystem::path& path, std::string_view bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

} // namespace chesscoach::pipeline::testing
