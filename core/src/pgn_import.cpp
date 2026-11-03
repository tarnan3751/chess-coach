// A repertoire from a PGN file (chesscoach/pgn_import.hpp), read with chess-library's streaming
// PGN parser with variations on. A visitor keeps one line per open parenthesis: the line's current
// position, the position before its last move (where a variation of that move starts), and whether
// a move that did not play has ended it. Moves are played only through Position.
#include <chesscoach/pgn_import.hpp>

#include <chess-library/chess.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace chesscoach {

namespace {

class RepertoireVisitor : public chess::pgn::Visitor {
  public:
    explicit RepertoireVisitor(Color colour) : colour_(colour) {}

    void startPgn() override {
        ++result_.games;
        fen_.reset();
        lines_.clear();
    }

    void header(std::string_view key, std::string_view value) override {
        if (key == "FEN") {
            fen_ = std::string(value);
        }
    }

    void startMoves() override {
        if (!fen_) {
            lines_.push_back(
                Line{.position = Position::start(), .before_last = {}, .ended = false});
            return;
        }
        auto start = Position::from_fen(*fen_);
        if (!start) {
            ++result_.unparsable;
            lines_.push_back(Line{.position = Position::start(), .before_last = {}, .ended = true});
            return;
        }
        lines_.push_back(Line{.position = std::move(*start), .before_last = {}, .ended = false});
    }

    void move(std::string_view san, std::string_view /*comment*/) override {
        if (lines_.empty() || lines_.back().ended || san.empty() || san.front() == '$') {
            return; // no line, a line already ended, a comment without a move, or a NAG
        }
        Line& line = lines_.back();
        Position before = line.position;
        const auto played = line.position.play_san(san);
        if (!played) {
            ++result_.unparsable;
            line.ended = true;
            return;
        }
        if (before.side_to_move() == colour_) {
            choose(before, played->uci);
        }
        line.before_last = std::move(before);
    }

    void startVariation() override {
        ++result_.variations;
        // A variation of an ended line, or before any move, is not followed.
        std::optional<Position> start;
        if (!lines_.empty() && !lines_.back().ended) {
            start = lines_.back().before_last;
        }
        if (!start) {
            lines_.push_back(Line{.position = Position::start(), .before_last = {}, .ended = true});
            return;
        }
        lines_.push_back(Line{.position = std::move(*start), .before_last = {}, .ended = false});
    }

    void endVariation() override {
        if (lines_.size() > 1) {
            lines_.pop_back();
        }
    }

    void endPgn() override { lines_.clear(); }

    [[nodiscard]] PgnImport take() { return std::move(result_); }

  private:
    struct Line {
        Position position;
        std::optional<Position> before_last;
        bool ended = false;
    };

    void choose(const Position& position, const std::string& move) {
        const Key key = position.key();
        const auto chosen = chosen_.find(key);
        if (chosen == chosen_.end()) {
            chosen_.emplace(key, result_.choices.size());
            result_.choices.push_back(
                RepertoireChoice{.key = key, .fen = position.fen(), .move = move});
            return;
        }
        if (result_.choices.at(chosen->second).move != move) {
            ++result_.conflicts;
        }
    }

    Color colour_;
    PgnImport result_;
    std::optional<std::string> fen_;
    std::vector<Line> lines_;
    std::unordered_map<Key, std::size_t> chosen_; // a chosen position's index in choices
};

} // namespace

tl::expected<PgnImport, PgnImportError> import_repertoire_pgn(std::istream& in, Color colour) {
    RepertoireVisitor visitor(colour);
    chess::pgn::StreamParser<> parser(in);
    parser.setParseVariations(true);
    const auto error = parser.readGames(visitor);
    switch (error.code()) {
    case chess::pgn::StreamParserError::None:
    case chess::pgn::StreamParserError::NotEnoughData: // an empty stream: no games
        return visitor.take();
    case chess::pgn::StreamParserError::ExceededMaxStringLength:
        return tl::unexpected(PgnImportError::too_long);
    case chess::pgn::StreamParserError::InvalidHeaderMissingClosingBracket:
    case chess::pgn::StreamParserError::InvalidHeaderMissingClosingQuote:
        return tl::unexpected(PgnImportError::malformed_header);
    }
    return tl::unexpected(PgnImportError::malformed_header);
}

} // namespace chesscoach
