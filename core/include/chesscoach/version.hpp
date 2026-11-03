// The project version: the Version type, its parser and printer, and the version this build
// carries, read from the header CMake generates from the root CMakeLists.txt's project version.
// Pure: no I/O and no clock; a parse failure is a value (CLAUDE.md, **Code conventions**).
#pragma once

#include <tl/expected.hpp>

#include <compare>
#include <cstdint>
#include <string>
#include <string_view>

namespace chesscoach {

// A semantic version, MAJOR.MINOR.PATCH, ordered component by component.
struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;

    friend auto operator<=>(const Version&, const Version&) = default;
    friend bool operator==(const Version&, const Version&) = default;
};

enum class VersionError : std::uint8_t {
    empty,     // the text is empty
    malformed, // anything but three dot-separated decimal components
};

// Parses exactly MAJOR.MINOR.PATCH: three decimal components that each fit an int, with no
// sign, no whitespace and no leading zero (0 is a component, 01 is not).
[[nodiscard]] tl::expected<Version, VersionError> parse_version(std::string_view text);

// MAJOR.MINOR.PATCH in decimal; parse_version(to_string(v)) == v for every v whose components
// are non-negative.
[[nodiscard]] std::string to_string(const Version& version);

// The version of this build: the root CMakeLists.txt's project version.
[[nodiscard]] Version current_version();

} // namespace chesscoach
