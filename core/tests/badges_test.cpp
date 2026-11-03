// Unit tests for chesscoach/badges.hpp (core; unit): the bits of docs/database.md §7.3 decoded
// from the stored value, set, and given back.
#include <chesscoach/badges.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

using chesscoach::Badge;
using chesscoach::Badges;

TEST_CASE("Badges decode the stored bits", "[badges]") {
    CHECK(Badges{}.raw() == 0);
    CHECK_FALSE(Badges{}.has(Badge::book));
    const Badges book_popular{3};
    CHECK(book_popular.has(Badge::book));
    CHECK(book_popular.has(Badge::popular));
    CHECK_FALSE(book_popular.has(Badge::dubious));
    const Badges popular_dubious{6};
    CHECK_FALSE(popular_dubious.has(Badge::book));
    CHECK(popular_dubious.has(Badge::popular));
    CHECK(popular_dubious.has(Badge::dubious));
    for (std::uint8_t raw = 0; raw < 8; ++raw) {
        CHECK(Badges{raw}.raw() == raw);
    }
}

TEST_CASE("Badges set a bit and keep the others", "[badges]") {
    Badges badges;
    badges.set(Badge::popular);
    CHECK(badges.raw() == 2);
    badges.set(Badge::dubious);
    CHECK(badges.raw() == 6);
    badges.set(Badge::popular);
    CHECK(badges.raw() == 6);
    badges.set(Badge::book);
    CHECK(badges == Badges{7});
    CHECK(Badges{8}.raw() == 8);
}
