// GameScanner (game_scanner.hpp): a chess::pgn::Visitor over one game's text.
#include "game_scanner.hpp"

#include <chess-library/chess.hpp>

#include <numeric>
#include <optional>
#include <sstream>

namespace chesscoach::pipeline {

namespace {

// The parser's read buffer is 64 * 64 bytes: a game's text is short, and each game has its own
// parser.
using Parser = chess::pgn::StreamParser<64>;

class GameVisitor final : public chess::pgn::Visitor {
  public:
    GameVisitor(std::uint32_t max_ply, const FenWanted& fen_wanted, ScanCounts& counts,
                const std::function<void(const ScannedGame&)>& sink)
        : max_ply_(max_ply), fen_wanted_(&fen_wanted), counts_(&counts), sink_(&sink) {}

    void startPgn() override {
        headers_.clear();
        in_game_ = true;
        admitted_ = false;
        bad_move_ = false;
        game_.keys.clear();
        game_.moves.clear();
        game_.fens.clear();
    }

    void header(std::string_view key, std::string_view value) override { headers_.set(key, value); }

    void startMoves() override {
        const auto checked = check_headers(headers_);
        if (!checked) {
            skip_reason_ = checked.error();
            skipPgn(true);
            return;
        }
        admitted_ = true;
        game_.tier =
            static_cast<std::uint8_t>(tier_index(tier_of(checked->white_elo, checked->black_elo)));
        game_.result = checked->result;
        position_ = Position::start();
        visit();
    }

    void move(std::string_view token, std::string_view /*comment*/) override {
        const std::string_view san = strip_glyphs(token);
        if (san.empty() || san.front() == '$') {
            return; // a comment without a move, or a NAG
        }
        if (game_.moves.size() == max_ply_) {
            skipPgn(true);
            return;
        }
        const auto played = position_.play_san_fast(san);
        const auto code = played ? encode_move(played->uci) : std::nullopt;
        if (!code) {
            bad_move_ = true;
            skipPgn(true);
            return;
        }
        game_.moves.push_back(*code);
        visit();
    }

    void endPgn() override {
        in_game_ = false;
        if (!admitted_) {
            counts_->skipped.add(skip_reason_);
        } else if (bad_move_) {
            counts_->skipped.add(SkipReason::moves);
        } else {
            counts_->admitted.at(game_.tier) += 1;
            (*sink_)(game_);
        }
    }

    // A game the parser started and could not finish is malformed.
    void finish_unended() {
        if (in_game_) {
            in_game_ = false;
            counts_->skipped.add(SkipReason::malformed);
        }
    }

  private:
    void visit() {
        const Key key = position_.key();
        game_.keys.push_back(key);
        if (*fen_wanted_ && (*fen_wanted_)(game_.tier, key)) {
            game_.fens.emplace_back(key, position_.fen());
        }
    }

    std::uint32_t max_ply_;
    const FenWanted* fen_wanted_;
    ScanCounts* counts_;
    const std::function<void(const ScannedGame&)>* sink_;
    GameHeaders headers_;
    Position position_ = Position::start();
    ScannedGame game_;
    SkipReason skip_reason_ = SkipReason::malformed;
    bool in_game_ = false;
    bool admitted_ = false;
    bool bad_move_ = false;
};

bool is_glyph(char c) { return c == '!' || c == '?'; }

} // namespace

std::uint64_t ScanCounts::admitted_total() const {
    return std::accumulate(admitted.begin(), admitted.end(), std::uint64_t{0});
}

ScanCounts& ScanCounts::operator+=(const ScanCounts& other) {
    for (std::size_t tier = 0; tier < admitted.size(); ++tier) {
        admitted.at(tier) += other.admitted.at(tier);
    }
    skipped += other.skipped;
    return *this;
}

std::string_view strip_glyphs(std::string_view token) {
    std::size_t end = token.size();
    for (int i = 0; i < 2 && end > 0 && is_glyph(token.at(end - 1)); ++i) {
        --end;
    }
    return token.substr(0, end);
}

GameScanner::GameScanner(std::uint32_t max_ply, FenWanted fen_wanted)
    : max_ply_(max_ply), fen_wanted_(std::move(fen_wanted)) {}

void GameScanner::scan(std::string_view text, const std::function<void(const ScannedGame&)>& sink) {
    std::istringstream stream{std::string(text)};
    Parser parser(stream);
    GameVisitor visitor(max_ply_, fen_wanted_, counts_, sink);
    const auto error = parser.readGames(visitor);
    if (error && error != chess::pgn::StreamParserError::NotEnoughData) {
        visitor.finish_unended();
    }
}

} // namespace chesscoach::pipeline
