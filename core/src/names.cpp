// Opening names from the chess-openings TSV files (chesscoach/names.hpp; docs/database.md §10).
// Each line's moves are replayed with play_san_fast, which keeps the SAN writer out of the loop.
#include <chesscoach/names.hpp>
#include <chesscoach/position.hpp>

#include <fstream>
#include <ios>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace chesscoach {

namespace {

// A move number such as "12.": digits, then one dot.
bool is_move_number(std::string_view token) {
    if (token.size() < 2 || token.back() != '.') {
        return false;
    }
    token.remove_suffix(1);
    return token.find_first_not_of("0123456789") == std::string_view::npos;
}

// The key after the line's moves from the start position, or nullopt when a move does not
// replay.
std::optional<Key> replay(std::string_view pgn) {
    auto position = Position::start();
    while (!pgn.empty()) {
        const auto space = pgn.find(' ');
        const auto token = pgn.substr(0, space);
        pgn.remove_prefix(space == std::string_view::npos ? pgn.size() : space + 1);
        if (token.empty() || is_move_number(token)) {
            continue;
        }
        if (!position.play_san_fast(token)) {
            return std::nullopt;
        }
    }
    return position.key();
}

} // namespace

const Opening* NameTable::find(Key key) const {
    const auto found = openings_.find(key);
    return found == openings_.end() ? nullptr : &found->second;
}

tl::expected<NameTable, NameError> load_names(const std::filesystem::path& tsv_dir) {
    NameTable table;
    for (const auto file : opening_tsv_files) {
        const auto path = tsv_dir / file;
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error)) {
            return tl::unexpected(
                NameError{.kind = NameErrorKind::missing_file, .file = std::string(file)});
        }
        std::ifstream in(path, std::ios::binary);
        std::string line;
        if (!in || !std::getline(in, line)) {
            return tl::unexpected(
                NameError{.kind = NameErrorKind::unreadable, .file = std::string(file)});
        }
        if (line != "eco\tname\tpgn") {
            return tl::unexpected(
                NameError{.kind = NameErrorKind::bad_header, .file = std::string(file)});
        }
        while (std::getline(in, line)) {
            ++table.lines_;
            const std::string_view text(line);
            const auto first = text.find('\t');
            const auto second =
                first == std::string_view::npos ? first : text.find('\t', first + 1);
            if (second == std::string_view::npos ||
                text.find('\t', second + 1) != std::string_view::npos) {
                ++table.failures_;
                continue;
            }
            const auto key = replay(text.substr(second + 1));
            if (!key) {
                ++table.failures_;
                continue;
            }
            const auto [entry, inserted] = table.openings_.try_emplace(
                *key, Opening{
                          .eco = std::string(text.substr(0, first)),
                          .name = std::string(text.substr(first + 1, second - first - 1)),
                      });
            if (!inserted) {
                ++table.collisions_;
            }
        }
        if (in.bad()) {
            return tl::unexpected(
                NameError{.kind = NameErrorKind::unreadable, .file = std::string(file)});
        }
    }
    return table;
}

} // namespace chesscoach
