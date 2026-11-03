// chess-coach-cli drill (drill.hpp).
#include "drill.hpp"

#include <chesscoach/scheduler.hpp>

#include "engine_common.hpp"
#include "profiles.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace chesscoach::cli {

namespace {

std::optional<Grade> parse_grade(const std::string& text) {
    if (text == "again") {
        return Grade::again;
    }
    if (text == "hard") {
        return Grade::hard;
    }
    if (text == "good") {
        return Grade::good;
    }
    if (text == "easy") {
        return Grade::easy;
    }
    return std::nullopt;
}

} // namespace

Result<void> run_drill(const DrillOptions& options, std::ostream& out, const Clock& clock) {
    const auto colour = parse_colour(options.colour);
    if (!colour) {
        return tl::unexpected(colour.error());
    }
    const auto grade = parse_grade(options.grade);
    if (options.action == "review" && !grade) {
        return fail(fmt::format("grade \"{}\" is not again, hard, good or easy", options.grade));
    }
    auto opened = open_profile(options.data_dir, options.profile, clock);
    if (!opened) {
        return tl::unexpected(opened.error());
    }
    auto& store = opened->store;
    const std::int64_t profile = opened->profile.id;
    const std::int64_t now = options.at ? *options.at : clock();

    if (options.action == "sync") {
        const auto synced = store.sync_cards(profile, *colour);
        if (!synced) {
            return fail(fmt::format("profiles.db {}", describe(synced.error())));
        }
        out << fmt::format("created: {}\nreset: {}\ndeleted: {}\n", synced->created, synced->reset,
                           synced->deleted);
        return {};
    }
    if (options.action == "review" && grade) {
        // Only a card of this profile's repertoire for the colour, with the key it has there.
        const auto cards = store.cards(profile, *colour);
        const auto card = std::ranges::find(cards, options.card, &Card::id);
        if (card == cards.end()) {
            return fail(fmt::format("no card {} in {}'s {} repertoire", options.card,
                                    opened->profile.name, options.colour));
        }
        const auto reviewed =
            store.record_review(profile, *colour, options.card, card->key, *grade, now);
        if (!reviewed) {
            return fail(fmt::format("profiles.db {}", describe(reviewed.error())));
        }
        out << fmt::format("card {}: repetitions {} interval {} ease {} due {}\n", reviewed->id,
                           reviewed->state.repetitions, reviewed->state.interval_days,
                           reviewed->state.ease_milli, reviewed->state.due_at);
        return {};
    }
    std::unordered_map<Key, std::string> fens;
    for (auto& node : store.repertoire_nodes(profile, *colour)) {
        fens.emplace(node.choice.key, std::move(node.choice.fen));
    }
    for (const auto& card : store.due_cards(profile, *colour, now)) {
        const auto fen = fens.find(card.key);
        std::string san = card.move;
        if (fen != fens.end()) {
            if (const auto position = Position::from_fen(fen->second)) {
                if (auto written = position->san_of(card.move)) {
                    san = std::move(*written);
                }
            }
        }
        out << fmt::format("{} {} {} {} due {} {}\n", card.id, card.key, card.move, san,
                           card.state.due_at, fen == fens.end() ? std::string("-") : fen->second);
    }
    return {};
}

} // namespace chesscoach::cli
