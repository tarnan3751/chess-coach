// chess-coach-cli spar (spar.hpp).
#include "spar.hpp"

#include <chesscoach/engine.hpp>
#include <chesscoach/engine_reply_provider.hpp>
#include <chesscoach/spar.hpp>
#include <chesscoach/tier.hpp>

#include <fmt/chrono.h>
#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace chesscoach::cli {

namespace {

// The coach once off book: the engine, started on the first reply it is asked for, so a session
// that stays in book, and --replay, need no engine.
class LazyEngine final : public ReplyProvider {
  public:
    LazyEngine(EngineSettings settings, Tier tier, double seconds)
        : settings_(std::move(settings)), tier_(tier), seconds_(seconds) {}

    tl::expected<std::string, ProviderError> reply(const Position& position) override {
        ++calls_;
        if (!provider_) {
            const auto binary = find_engine(settings_.stockfish);
            if (!binary) {
                return failed(binary.error().message);
            }
            EngineOptions options;
            options.threads = settings_.threads;
            options.hash_mb = settings_.hash_mb;
            auto started = Engine::start(*binary, options);
            if (!started) {
                return failed(fmt::format("the engine {} could not be started", binary->string()));
            }
            engine_ = std::make_unique<Engine>(std::move(*started));
            provider_ = std::make_unique<EngineReplyProvider>(
                *engine_, tier_, ProviderConfig{.seconds = seconds_, .elo_offset = 100});
        }
        auto move = provider_->reply(position);
        if (!move) {
            return failed(move.error().message);
        }
        return move;
    }

    // What the last failed reply said.
    [[nodiscard]] const std::string& last_error() const { return last_error_; }

    // The replies asked of the engine so far.
    [[nodiscard]] int calls() const { return calls_; }

  private:
    tl::unexpected<ProviderError> failed(std::string message) {
        last_error_ = message;
        return tl::unexpected(ProviderError{.message = std::move(message)});
    }

    EngineSettings settings_;
    Tier tier_;
    double seconds_;
    std::unique_ptr<Engine> engine_;
    std::unique_ptr<EngineReplyProvider> provider_;
    std::string last_error_;
    int calls_ = 0;
};

std::string_view colour_name(Color colour) { return colour == Color::white ? "white" : "black"; }

std::string date_of(std::int64_t seconds) {
    return fmt::format("{:%Y-%m-%d}", std::chrono::sys_seconds{std::chrono::seconds{seconds}});
}

// "1. e4 e5 2. Nf3": the SAN moves from the start, numbered.
std::string san_line(const std::vector<std::string>& sans) {
    std::string line;
    for (std::size_t ply = 0; ply < sans.size(); ++ply) {
        if (!line.empty()) {
            line += ' ';
        }
        if (ply % 2 == 0) {
            line += fmt::format("{}. ", (ply / 2) + 1);
        }
        line += sans.at(ply);
    }
    return line;
}

// The SAN of every recorded UCI move, played from the start.
std::vector<std::string> sans_of(const std::vector<std::string>& moves) {
    Position position = Position::start();
    std::vector<std::string> sans;
    sans.reserve(moves.size());
    for (const std::string& move : moves) {
        auto played = position.play_uci(move);
        sans.push_back(played ? played->san : move);
    }
    return sans;
}

// The UCI of a move typed in UCI or SAN, when it is legal at the position.
std::optional<std::string> to_uci(const Position& position, const std::string& text) {
    const auto legal = position.legal_moves_uci();
    if (std::ranges::find(legal, text) != legal.end()) {
        return text;
    }
    auto uci = position.uci_of(text);
    if (!uci) {
        return std::nullopt;
    }
    return std::move(*uci);
}

void print_interruption(std::ostream& out, const Interruption& interruption,
                        const std::string& san) {
    out << fmt::format("coach: {}: {} ({}), badges {}, loss {}\n",
                       interruption.reason == Interruption::Reason::dubious ? "Dubious move"
                                                                            : "out of book",
                       san, interruption.played, badge_letters(interruption.badges),
                       interruption.loss ? std::to_string(*interruption.loss) : "-");
    for (const BetterChoice& choice : interruption.better) {
        const MoveRow& row = choice.row;
        out << fmt::format("  better: {} ({}) {} {} games {}% {}% {}%\n", choice.san, row.move,
                           badge_letters(row.badges), row.total(), percent(row.white, row.total()),
                           percent(row.draws, row.total()), percent(row.black, row.total()));
    }
}

void print_summary(std::ostream& out, const Summary& summary) {
    out << fmt::format("summary: {} moves, {} interruptions, reached {}, result {}\n",
                       summary.moves.size(), summary.interruptions.size(),
                       summary.phase_reached == Phase::off_book ? "off book" : "book",
                       summary.result.value_or("none"));
    out << "moves: " << san_line(sans_of(summary.moves)) << '\n';
}

std::string_view describe(SparError error) {
    switch (error) {
    case SparError::bad_config:
        return "--end-move must be at least 1";
    case SparError::provider_failed:
        return "the engine failed";
    case SparError::provider_illegal_move:
        return "the engine played a move that is not legal";
    case SparError::replay_illegal_move:
        return "the record holds a move that is not legal where it is played";
    case SparError::replay_mismatch:
        return "the record does not replay: a book reply differs, or it runs past the end";
    case SparError::replay_database_mismatch:
        return "it was played against another build of the opening database (data_version)";
    }
    return "the session failed";
}

Result<Profile> find_profile(const ProfileStore& profiles, const std::string& name) {
    for (const Profile& profile : profiles.profiles()) {
        if (profile.name == name) {
            return profile;
        }
    }
    return fail(fmt::format("no profile named {}", name));
}

Result<void> list_sessions(const ProfileStore& profiles, const Profile& profile,
                           std::ostream& out) {
    out << "id date tier colour moves interruptions result\n";
    for (const SparSessionRow& row : profiles.spar_sessions(profile.id)) {
        out << fmt::format("{} {} {} {} {} {} {}\n", row.id, date_of(row.started_at),
                           tier_index(row.tier), colour_name(row.colour), row.moves.size(),
                           row.interruptions, row.result.value_or("-"));
    }
    return {};
}

Result<void> replay_session(const OpeningStore& store, const ProfileStore& profiles,
                            std::int64_t id, std::ostream& out) {
    const auto row = profiles.spar_session(id);
    if (!row) {
        return fail(fmt::format("no saved session {}", id));
    }
    const auto summary = replay_spar_session(store, *row);
    if (!summary) {
        return fail(fmt::format("session {}: {}", id, describe(summary.error())));
    }
    if (std::cmp_not_equal(summary->interruptions.size(), row->interruptions) ||
        summary->result != row->result) {
        return fail(fmt::format("session {} replays to other interruptions or another result", id));
    }
    out << fmt::format("replay of session {}: tier {}, {}, seed {}\n", id, tier_index(row->tier),
                       colour_name(row->colour), row->seed);
    const auto sans = sans_of(row->moves);
    for (const Interruption& interruption : summary->interruptions) {
        const auto at = std::ranges::find(row->moves, interruption.played);
        const auto ply = static_cast<std::size_t>(at - row->moves.begin());
        print_interruption(out, interruption,
                           ply < sans.size() ? sans.at(ply) : interruption.played);
    }
    print_summary(out, *summary);
    return {};
}

} // namespace

Result<void> run_spar(const SparOptions& options, std::istream& in, std::ostream& out,
                      const Clock& clock) {
    // Every option is checked before a file is opened or created.
    if (options.list && options.replay) {
        return fail("--list and --replay exclude each other");
    }
    const bool session_wanted = !options.list && !options.replay;
    if (session_wanted && (!options.tier || !options.colour)) {
        return fail("a new session needs --tier and --colour");
    }
    const auto tier = session_wanted ? tier_from_index(*options.tier) : std::optional<Tier>{};
    if (session_wanted && !tier) {
        return fail(fmt::format("tier {} is not one of 0 to 6", *options.tier));
    }
    if (session_wanted && *options.colour != "white" && *options.colour != "black") {
        return fail(fmt::format("--colour must be white or black, not {}", *options.colour));
    }
    std::optional<OpeningStore> store;
    if (!options.list) {
        auto opened = open_located_store(options.db, options.data_dir);
        if (!opened) {
            return tl::unexpected(opened.error());
        }
        store.emplace(std::move(*opened));
    }
    auto profiles = ProfileStore::open(options.data_dir, clock);
    if (!profiles) {
        return fail(fmt::format("profiles.db in {} cannot be opened", options.data_dir));
    }
    const auto profile = find_profile(*profiles, options.profile);
    if (!profile) {
        return tl::unexpected(profile.error());
    }
    if (options.list) {
        return list_sessions(*profiles, *profile, out);
    }
    if (options.replay) {
        return replay_session(*store, *profiles, *options.replay, out);
    }

    const Color colour = *options.colour == "white" ? Color::white : Color::black;
    const std::uint64_t seed = options.seed.value_or(static_cast<std::uint64_t>(clock()));
    auto session = SparSession::start(*store, *tier, colour, seed,
                                      SparConfig{.end_move = options.end_move, .sampler = {}});
    if (!session) {
        return fail(std::string(describe(session.error())));
    }
    const std::int64_t started_at = clock();
    out << fmt::format("seed: {}\n", seed);
    LazyEngine engine(options.engine, *tier, options.seconds);
    const std::vector<std::string_view> script =
        options.script ? words(*options.script) : std::vector<std::string_view>{};
    std::size_t next = 0;
    std::vector<std::string> sans;
    bool announced = false; // the move out of book, said once
    const auto announce = [&] {
        if (!announced && session->summary().phase_reached == Phase::off_book) {
            out << fmt::format("out of book at tier {} ({}): the engine plays from here\n",
                               tier_index(*tier), tier_label(*tier));
            announced = true;
        }
    };
    announce();
    bool stopped = false;
    while (session->phase() != Phase::ended) {
        if (session->user_to_move()) {
            std::optional<std::string> uci;
            std::string text;
            while (!uci) {
                if (options.script) {
                    if (next == script.size()) {
                        break;
                    }
                    text = script.at(next++);
                } else {
                    out << (sans.empty() ? std::string("> ") : san_line(sans) + " > ");
                    out.flush();
                    if (!(in >> text)) {
                        out << '\n'; // the end of the input ends the prompt's line
                        break;
                    }
                }
                uci = to_uci(session->position(), text);
                if (!uci) {
                    if (options.script) {
                        return fail(fmt::format("the script's move {} is not legal after {}", text,
                                                sans.empty() ? "the start" : san_line(sans)));
                    }
                    out << fmt::format("not a legal move here: {}\n", text);
                }
            }
            if (!uci) {
                stopped = true;
                break;
            }
            const auto outcome = session->play_user_move(*uci);
            if (!outcome) {
                return fail(fmt::format("the move {} cannot be played", *uci));
            }
            sans.push_back(outcome->san);
            out << fmt::format("you: {} ({})\n", outcome->san, *uci);
            if (outcome->interruption) {
                print_interruption(out, *outcome->interruption, outcome->san);
            }
        } else {
            const Position before = session->position();
            const int calls = engine.calls();
            const auto reply = session->coach_reply(engine);
            if (engine.calls() != calls) {
                announce(); // the book had no reply: say so before the engine's move
            }
            if (!reply) {
                return fail(reply.error() == SparError::provider_failed
                                ? engine.last_error()
                                : std::string(describe(reply.error())));
            }
            const std::string san = before.san_of(*reply).value_or(*reply);
            sans.push_back(san);
            out << fmt::format("coach: {} ({})\n", san, *reply);
        }
        announce();
    }
    const Summary summary = session->summary();
    if (stopped) {
        out << "session stopped before its end\n";
    } else if (summary.result) {
        out << fmt::format("session ended: {}\n", *summary.result);
    } else {
        out << fmt::format("session ended at move {}\n", options.end_move);
    }
    const auto ended_at =
        session->phase() == Phase::ended ? std::optional<std::int64_t>(clock()) : std::nullopt;
    const auto id = profiles->save_spar_session(profile->id, *session, started_at, ended_at);
    if (!id) {
        return fail("the session could not be saved in profiles.db");
    }
    print_summary(out, summary);
    out << fmt::format("saved: session {} under {}\n", *id, profile->name);
    return {};
}

} // namespace chesscoach::cli
