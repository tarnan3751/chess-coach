// Unit tests for chesscoach/version.hpp: what parse_version accepts and rejects, the ordering,
// printing, and the version of this build (core; unit).
#include <chesscoach/version.hpp>
#include <chesscoach/version_info.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string_view>

using chesscoach::parse_version;
using chesscoach::Version;
using chesscoach::VersionError;

TEST_CASE("parse_version accepts MAJOR.MINOR.PATCH", "[version]") {
    CHECK(parse_version("0.1.0") == Version{0, 1, 0});
    CHECK(parse_version("0.0.0") == Version{0, 0, 0});
    CHECK(parse_version("1.2.3") == Version{1, 2, 3});
    CHECK(parse_version("10.20.30") == Version{10, 20, 30});
    CHECK(parse_version("2147483647.0.0") == Version{2147483647, 0, 0});
}

TEST_CASE("parse_version rejects an empty text as empty", "[version]") {
    const auto parsed = parse_version("");
    REQUIRE_FALSE(parsed.has_value());
    CHECK(parsed.error() == VersionError::empty);
}

TEST_CASE("parse_version rejects anything else as malformed", "[version]") {
    for (
        const std::string_view text : {
            "1",      "1.2",    "1.2.3.4",   "1..3",   ".1.2",           "1.2.",
            "...",    "01.2.3", "1.02.3",    "1.2.03", "00.0.0",         "+1.2.3",
            "-1.2.3", "1.-2.3", " 1.2.3",    "1.2.3 ", "1. 2.3",         "1.2.3\n",
            "a.b.c",  "1.2.3a", "1.2.3-rc1", "v1.2.3", "2147483648.0.0", "99999999999999999999.0.0",
        }) {
        CAPTURE(text);
        const auto parsed = parse_version(text);
        REQUIRE_FALSE(parsed.has_value());
        CHECK(parsed.error() == VersionError::malformed);
    }
}

TEST_CASE("Versions order component by component", "[version]") {
    CHECK(Version{1, 2, 3} == Version{1, 2, 3});
    CHECK(Version{1, 2, 3} < Version{1, 2, 10});
    CHECK(Version{1, 2, 10} < Version{1, 10, 0});
    CHECK(Version{1, 10, 0} < Version{2, 0, 0});
    CHECK(Version{0, 9, 9} < Version{1, 0, 0});
    CHECK(Version{2, 0, 0} > Version{1, 99, 99});
    CHECK(Version{1, 2, 3} != Version{1, 2, 4});
}

TEST_CASE("to_string prints MAJOR.MINOR.PATCH and parses back", "[version]") {
    CHECK(to_string(Version{0, 1, 0}) == "0.1.0");
    CHECK(to_string(Version{10, 20, 30}) == "10.20.30");
    CHECK(parse_version(to_string(Version{4, 0, 17})) == Version{4, 0, 17});
}

TEST_CASE("current_version is the project version", "[version]") {
    const auto current = chesscoach::current_version();
    CHECK(current ==
          Version{CHESSCOACH_VERSION_MAJOR, CHESSCOACH_VERSION_MINOR, CHESSCOACH_VERSION_PATCH});
    CHECK(to_string(current) == CHESSCOACH_VERSION_STRING);
    CHECK(parse_version(CHESSCOACH_VERSION_STRING) == current);
}
