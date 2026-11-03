// The §3 header checks and the skip counters (game_filter.hpp).
#include "game_filter.hpp"

#include <numeric>

namespace chesscoach::pipeline {

namespace {

constexpr std::string_view rated_prefix = "Rated ";

// The speed word of a rated Event: the word right after "Rated ".
bool admitted_speed(std::string_view event) {
    const std::string_view rest = event.substr(rated_prefix.size());
    const std::string_view word = rest.substr(0, rest.find(' '));
    return word == "Blitz" || word == "Rapid" || word == "Classical";
}

// A decimal integer of one to nine digits, no sign and no space.
std::optional<int> parse_rating(const std::optional<std::string>& text) {
    if (!text || text->empty() || text->size() > 9) {
        return std::nullopt;
    }
    int value = 0;
    for (const char c : *text) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        value = (value * 10) + (c - '0');
    }
    return value;
}

} // namespace

std::string_view name(SkipReason reason) {
    switch (reason) {
    case SkipReason::malformed:
        return "malformed";
    case SkipReason::unrated:
        return "unrated";
    case SkipReason::speed:
        return "speed";
    case SkipReason::variant:
        return "variant";
    case SkipReason::rating:
        return "rating";
    case SkipReason::result:
        return "result";
    case SkipReason::moves:
        return "moves";
    }
    return "unknown";
}

void GameHeaders::clear() {
    event.reset();
    variant.reset();
    white_elo.reset();
    black_elo.reset();
    result.reset();
    setup_or_fen = false;
}

void GameHeaders::set(std::string_view tag, std::string_view value) {
    if (tag == "Event") {
        event = std::string(value);
    } else if (tag == "Variant") {
        variant = std::string(value);
    } else if (tag == "WhiteElo") {
        white_elo = std::string(value);
    } else if (tag == "BlackElo") {
        black_elo = std::string(value);
    } else if (tag == "Result") {
        result = std::string(value);
    } else if (tag == "SetUp" || tag == "FEN") {
        setup_or_fen = true;
    }
}

tl::expected<AdmittedGame, SkipReason> check_headers(const GameHeaders& headers) {
    if (!headers.event || !headers.event->starts_with(rated_prefix)) {
        return tl::unexpected(SkipReason::unrated);
    }
    if (!admitted_speed(*headers.event)) {
        return tl::unexpected(SkipReason::speed);
    }
    if ((headers.variant && *headers.variant != "Standard") || headers.setup_or_fen) {
        return tl::unexpected(SkipReason::variant);
    }
    const auto white_elo = parse_rating(headers.white_elo);
    const auto black_elo = parse_rating(headers.black_elo);
    if (!white_elo || !black_elo) {
        return tl::unexpected(SkipReason::rating);
    }
    AdmittedGame game{.white_elo = *white_elo, .black_elo = *black_elo, .result = GameResult::draw};
    if (headers.result == "1-0") {
        game.result = GameResult::white;
    } else if (headers.result == "0-1") {
        game.result = GameResult::black;
    } else if (headers.result != "1/2-1/2") {
        return tl::unexpected(SkipReason::result);
    }
    return game;
}

std::uint64_t SkipCounts::total() const {
    return std::accumulate(counts_.begin(), counts_.end(), std::uint64_t{0});
}

SkipCounts& SkipCounts::operator+=(const SkipCounts& other) {
    for (std::size_t i = 0; i < counts_.size(); ++i) {
        counts_.at(i) += other.counts_.at(i);
    }
    return *this;
}

} // namespace chesscoach::pipeline
