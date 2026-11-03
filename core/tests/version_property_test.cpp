// Property tests for chesscoach/version.hpp (core; property): printing then parsing gives the
// version back, and the ordering is the lexicographic order of the components. RapidCheck prints
// the failing case and its seed on a failure.
#include <chesscoach/version.hpp>

#include <catch2/catch_test_macros.hpp>
#include <rapidcheck.h>

#include <compare>
#include <limits>
#include <tuple>

using chesscoach::Version;

namespace {

// A version whose three components each come from the given generator.
rc::Gen<Version> versions_of(const rc::Gen<int>& component) {
    return rc::gen::map(rc::gen::tuple(component, component, component),
                        [](const std::tuple<int, int, int>& parts) {
                            return Version{
                                .major = std::get<0>(parts),
                                .minor = std::get<1>(parts),
                                .patch = std::get<2>(parts),
                            };
                        });
}

// Non-negative components anywhere in the int range.
rc::Gen<Version> any_version() {
    return versions_of(rc::gen::inRange(0, std::numeric_limits<int>::max()));
}

// Small components, so that equal components, and equal versions, are frequent.
rc::Gen<Version> small_version() { return versions_of(rc::gen::inRange(0, 3)); }

std::tuple<int, int, int> as_tuple(const Version& v) { return {v.major, v.minor, v.patch}; }

} // namespace

TEST_CASE("parse_version(to_string(v)) == v", "[version][property]") {
    REQUIRE(rc::check("parse_version(to_string(v)) == v", [] {
        const auto version = *any_version();
        const auto parsed = chesscoach::parse_version(chesscoach::to_string(version));
        RC_ASSERT(parsed.has_value());
        RC_ASSERT(*parsed == version);
    }));
}

TEST_CASE("<=> agrees with the order of (major, minor, patch)", "[version][property]") {
    REQUIRE(rc::check("<=> agrees with the tuple order", [] {
        const auto a = *small_version();
        const auto b = *small_version();
        const auto order = a <=> b;
        RC_ASSERT(std::is_lt(order) == (as_tuple(a) < as_tuple(b)));
        RC_ASSERT(std::is_eq(order) == (as_tuple(a) == as_tuple(b)));
        RC_ASSERT(std::is_gt(order) == (as_tuple(b) < as_tuple(a)));
        RC_ASSERT((a == b) == (as_tuple(a) == as_tuple(b)));
    }));
}
