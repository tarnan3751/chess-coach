// Unit tests for the 16-bit move code: every from-to pair and promotion round-trips, and text
// that is not a UCI move is refused (pipeline; unit).
#include "move_code.hpp"

#include "test_files.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using chesscoach::pipeline::decode_move;
using chesscoach::pipeline::encode_move;

TEST_CASE("encode_move and decode_move round-trip every UCI move", "[move_code]") {
    for (char from_file = 'a'; from_file <= 'h'; ++from_file) {
        for (char from_rank = '1'; from_rank <= '8'; ++from_rank) {
            for (char to_file = 'a'; to_file <= 'h'; ++to_file) {
                for (char to_rank = '1'; to_rank <= '8'; ++to_rank) {
                    const std::string uci{from_file, from_rank, to_file, to_rank};
                    const auto code = encode_move(uci);
                    REQUIRE(code.has_value());
                    CHECK(decode_move(chesscoach::pipeline::testing::engaged(code)) == uci);
                    for (const char piece : {'n', 'b', 'r', 'q'}) {
                        const std::string promotion = uci + piece;
                        REQUIRE(encode_move(promotion).has_value());
                        CHECK(decode_move(chesscoach::pipeline::testing::engaged(
                                  encode_move(promotion))) == promotion);
                    }
                }
            }
        }
    }
}

TEST_CASE("encode_move refuses text that is not a UCI move", "[move_code]") {
    for (const char* text : {"", "e2", "e2e9", "i2e4", "e2e4k", "e7e8Q", "e2e4qq", "0000"}) {
        CHECK_FALSE(encode_move(text).has_value());
    }
}
