// The profile worker (profile_worker.hpp).
#include "profile_worker.hpp"

#include <chesscoach/pgn_import.hpp>
#include <chesscoach/position.hpp>
#include <chesscoach/repertoire.hpp>
#include <chesscoach/scheduler.hpp>
#include <chesscoach/score.hpp>
#include <chesscoach/tier.hpp>

#include "default_profile.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <exception>
#include <fstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace chesscoach::app {

namespace {

QString translate(const char* text) {
    return QCoreApplication::translate("chesscoach::app::ProfileWorker", text);
}

} // namespace

QString ProfileWorker::describe(ProfileError error) {
    switch (error) {
    case ProfileError::unwritable:
        return translate("profiles.db cannot be created or written there");
    case ProfileError::not_profiles:
        return translate("the file is not a chess-coach profile");
    case ProfileError::newer_version:
        return translate("the file comes from a newer chess-coach");
    case ProfileError::migration_failed:
        return translate("the file could not be brought up to this version");
    case ProfileError::invalid_name:
    case ProfileError::duplicate_name:
    case ProfileError::no_such_profile:
    case ProfileError::last_profile:
        return translate("the profile is not there");
    case ProfileError::storage:
        return translate("the write failed");
    case ProfileError::invalid_choice:
        return translate("the move is not a choice for this colour here");
    case ProfileError::no_such_card:
        return translate("the card is not there");
    case ProfileError::busy:
        return translate("another program has the file open; nothing was changed");
    }
    return {};
}

namespace {

std::int64_t now() { return QDateTime::currentSecsSinceEpoch(); }

Color colour_of(bool white) { return white ? Color::white : Color::black; }

// "N. san" for white's move at ply (0 for white's first), "N... san" for black's.
QString numbered(int ply, const QString& san) {
    const int number = (ply / 2) + 1;
    return ply % 2 == 0 ? QStringLiteral("%1. %2").arg(number).arg(san)
                        : QStringLiteral("%1... %2").arg(number).arg(san);
}

// The line from the start as one string, "1. a3 e5 2. Nf3"; the UCI where a move does not play.
QString san_line(const std::vector<std::string>& line) {
    Position position = Position::start();
    QStringList parts;
    for (std::size_t ply = 0; ply < line.size(); ++ply) {
        const auto played = position.play_uci(line.at(ply));
        const QString san = QString::fromStdString(played ? played->san : line.at(ply));
        parts << (ply % 2 == 0 ? numbered(static_cast<int>(ply), san) : san);
    }
    return parts.join(QLatin1Char(' '));
}

// The repertoire as a tree of lines from the start: where the colour is to move, its choice;
// where the opponent is, each legal reply (by UCI) that reaches a position with a choice. The walk
// goes depth first, a position walked once, by the first line in that order that reaches it, so
// transpositions do not repeat; names holds that line for every position walked.
struct TreeWalk {
    QVector<RepertoireTreeNode> tree;
    QHash<quint64, QString> names;
};

TreeWalk walk_repertoire(const Repertoire& choices, Color colour) {
    struct Frame {
        Position position;
        int node = -1; // the tree node that reached it, -1 at the start
        QStringList line;
        QString name;
        int ply = 0;
    };
    TreeWalk walk;
    std::unordered_set<Key> claimed{Position::start().key()};
    std::vector<Frame> stack;
    stack.push_back(Frame{
        .position = Position::start(),
        .node = -1,
        .line = {},
        .name = {},
        .ply = 0,
    });
    while (!stack.empty()) {
        const Frame frame = std::move(stack.back());
        stack.pop_back();
        walk.names.insert(frame.position.key(), frame.name);
        std::vector<std::pair<std::string, bool>> moves; // UCI, and whether it is the choice
        if (frame.position.side_to_move() == colour) {
            if (const auto choice = choices.find(frame.position.key()); choice != choices.end()) {
                moves.emplace_back(choice->second, true);
            }
        } else {
            std::vector<std::string> replies = frame.position.legal_moves_uci();
            std::ranges::sort(replies);
            for (std::string& reply : replies) {
                moves.emplace_back(std::move(reply), false);
            }
        }
        std::vector<Frame> children;
        for (const auto& [uci, chosen] : moves) {
            Position child = frame.position;
            const auto played = child.play_uci(uci);
            // A stored choice that does not play here is left out, as is a reply to a position
            // without a choice, or one another line has reached.
            if (!played || (!chosen && !choices.contains(played->child)) ||
                !claimed.insert(played->child).second) {
                continue;
            }
            const QString san = QString::fromStdString(played->san);
            QString name =
                frame.ply % 2 == 0 || frame.name.isEmpty() ? numbered(frame.ply, san) : san;
            if (!frame.name.isEmpty()) {
                name = frame.name + QLatin1Char(' ') + name;
            }
            QStringList line = frame.line;
            line.append(QString::fromStdString(uci));
            walk.tree.append(RepertoireTreeNode{
                .parent = frame.node,
                .text = numbered(frame.ply, san),
                .line = line,
                .chosen = chosen,
            });
            children.push_back(Frame{
                .position = std::move(child),
                .node = static_cast<int>(walk.tree.size()) - 1,
                .line = std::move(line),
                .name = std::move(name),
                .ply = frame.ply + 1,
            });
        }
        // The first child is walked first.
        std::ranges::reverse(children);
        for (Frame& child : children) {
            stack.push_back(std::move(child));
        }
    }
    return walk;
}

} // namespace

void register_profile_types() {
    qRegisterMetaType<RepertoireData>();
    qRegisterMetaType<CoverageData>();
    qRegisterMetaType<ScoreData>();
    qRegisterMetaType<PgnImportInfo>();
    qRegisterMetaType<CardsData>();
}

ProfileWorker::ProfileWorker(const QString& data_dir, QString db, QObject* parent)
    : QObject(parent), data_dir_(data_dir.toStdString()), db_(std::move(db)) {}

ProfileWorker::~ProfileWorker() = default;

ProfileStore* ProfileWorker::open_profiles(bool create) {
    if (profiles_.has_value()) {
        return &*profiles_;
    }
    if (!create && !QFileInfo::exists(QDir(QString::fromStdString(data_dir_.string()))
                                          .filePath(QStringLiteral("profiles.db")))) {
        return nullptr;
    }
    auto profiles = ProfileStore::open(data_dir_);
    if (!profiles) {
        fail(tr("The profile could not be opened: %1.").arg(describe(profiles.error())));
        return nullptr;
    }
    return &profiles_.emplace(std::move(*profiles));
}

const OpeningStore* ProfileWorker::open_store() {
    if (store_.has_value()) {
        return &*store_;
    }
    if (db_.isEmpty()) {
        return nullptr;
    }
    auto store = OpeningStore::open(std::filesystem::path(db_.toStdString()));
    if (!store) {
        return nullptr;
    }
    return &store_.emplace(std::move(*store));
}

std::optional<ProfileWorker::Writable> ProfileWorker::writable_profile() {
    ProfileStore* profiles = open_profiles(true);
    if (profiles == nullptr) {
        return std::nullopt;
    }
    const auto profile = default_profile(*profiles);
    if (!profile) {
        fail(tr("The profile store holds no profile."));
        return std::nullopt;
    }
    return Writable{.store = profiles, .profile = *profile};
}

void ProfileWorker::fail(const QString& message) { emit profile_failed(message); }

void ProfileWorker::close_stores() { profiles_.reset(); }

void ProfileWorker::set_choice(bool white, quint64 key, const QString& fen, const QString& uci) {
    try {
        const auto writable = writable_profile();
        if (!writable) {
            return;
        }
        const auto changed = writable->store->set_choice(writable->profile, colour_of(white),
                                                         RepertoireChoice{
                                                             .key = key,
                                                             .fen = fen.toStdString(),
                                                             .move = uci.toStdString(),
                                                         });
        if (!changed) {
            fail(tr("The choice was not saved: %1.").arg(describe(changed.error())));
            return;
        }
        emit repertoire_changed(white, *changed == ChoiceChange::unchanged
                                           ? tr("The choice was already made.")
                                           : tr("Choice saved."));
    } catch (const std::exception&) {
        profiles_.reset();
        fail(tr("profiles.db could not be read."));
    }
}

void ProfileWorker::remove_choice(bool white, quint64 key) {
    try {
        const auto writable = writable_profile();
        if (!writable) {
            return;
        }
        const auto removed =
            writable->store->remove_choice(writable->profile, colour_of(white), key);
        if (!removed) {
            fail(tr("The choice was not removed: %1.").arg(describe(removed.error())));
            return;
        }
        emit repertoire_changed(white,
                                *removed ? tr("Choice removed.") : tr("There was no choice here."));
    } catch (const std::exception&) {
        profiles_.reset();
        fail(tr("profiles.db could not be read."));
    }
}

void ProfileWorker::load_repertoire(bool white) {
    RepertoireData data;
    data.white = white;
    try {
        const ProfileStore* profiles = open_profiles(false);
        const auto profile =
            profiles != nullptr ? default_profile(*profiles) : std::optional<std::int64_t>();
        if (profiles != nullptr && profile.has_value()) {
            const Repertoire repertoire = profiles->repertoire(*profile, colour_of(white));
            for (const auto& [key, move] : repertoire) {
                data.choices.insert(key, QString::fromStdString(move));
            }
            data.tree = walk_repertoire(repertoire, colour_of(white)).tree;
        }
    } catch (const std::exception&) {
        profiles_.reset();
        fail(tr("profiles.db could not be read."));
    }
    emit repertoire_ready(data);
}

void ProfileWorker::compute_coverage(bool white, int tier, int depth) {
    const auto at = tier_from_index(tier);
    try {
        const OpeningStore* store = open_store();
        if (store == nullptr || !at.has_value()) {
            fail(tr("Coverage needs an opening database: open one with --db."));
            return;
        }
        Repertoire repertoire;
        const ProfileStore* profiles = open_profiles(false);
        if (profiles != nullptr) {
            if (const auto profile = default_profile(*profiles)) {
                repertoire = profiles->repertoire(*profile, colour_of(white));
            }
        }
        const Coverage coverage = chesscoach::compute_coverage(
            *store, repertoire, colour_of(white), *at, CoverageConfig{.max_plies = depth});
        CoverageData data;
        data.white = white;
        data.tier = tier;
        data.depth = depth;
        data.covered = coverage.covered;
        for (const Gap& gap : coverage.gaps) {
            QStringList line;
            for (const std::string& move : gap.line) {
                line << QString::fromStdString(move);
            }
            data.gaps.append(CoverageGap{
                .mass = gap.mass,
                .line_san = san_line(gap.line),
                .line = line,
                .fen = QString::fromStdString(gap.fen),
            });
        }
        emit coverage_ready(data);
    } catch (const std::exception&) {
        store_.reset();
        profiles_.reset();
        fail(tr("The coverage could not be computed: a database could not be read."));
    }
}

void ProfileWorker::score_position(quint64 key, int tier) {
    ScoreData data;
    data.key = key;
    data.tier = tier;
    const auto at = tier_from_index(tier);
    try {
        const OpeningStore* store = open_store();
        const auto row = store != nullptr ? store->position(key) : std::optional<PositionRow>();
        if (at.has_value() && row.has_value()) {
            const auto position = Position::from_fen(row->fen);
            for (const ScoreComponents& move : score_moves(*store, key, *at)) {
                const auto san = position ? position->san_of(move.move)
                                          : tl::expected<std::string, MoveError>(move.move);
                data.rows.append(ScoreRow{
                    .san = QString::fromStdString(san.value_or(move.move)),
                    .uci = QString::fromStdString(move.move),
                    .sound = move.sound,
                    .loss = move.loss,
                    .expected = move.expected_score,
                    .replies = move.replies_needed,
                    .penalty = move.penalty,
                    .score = move.score,
                    .share = move.share,
                });
            }
        }
    } catch (const std::exception&) {
        store_.reset();
        fail(tr("The opening database could not be read."));
    }
    emit scores_ready(data);
}

void ProfileWorker::import_pgn(const QString& path, bool white) {
    std::ifstream in(std::filesystem::path(path.toStdString()), std::ios::binary);
    if (!in) {
        fail(tr("%1 could not be opened.").arg(path));
        return;
    }
    const auto read = import_repertoire_pgn(in, colour_of(white));
    if (!read) {
        fail(tr("%1 is not a PGN file this import reads.").arg(QFileInfo(path).fileName()));
        return;
    }
    try {
        const auto writable = writable_profile();
        if (!writable) {
            return;
        }
        const auto applied =
            writable->store->import_choices(writable->profile, colour_of(white), read->choices);
        if (!applied) {
            fail(tr("The choices were not saved: %1.").arg(describe(applied.error())));
            return;
        }
        emit pgn_imported(PgnImportInfo{
            .white = white,
            .games = read->games,
            .variations = read->variations,
            .choices = static_cast<int>(read->choices.size()),
            .added = applied->added,
            .unchanged = applied->unchanged,
            .conflicts = read->conflicts + applied->conflicts,
            .unparsable = read->unparsable,
        });
        emit repertoire_changed(white, QString()); // pgn_imported says what changed
    } catch (const std::exception&) {
        profiles_.reset();
        fail(tr("profiles.db could not be read."));
    }
}

void ProfileWorker::sync_cards(bool white) {
    try {
        if (open_profiles(false) == nullptr) {
            emit cards_synced(white, 0, 0, 0); // no repertoire yet, so no cards
            return;
        }
        const auto writable = writable_profile();
        if (!writable) {
            return;
        }
        const auto synced = writable->store->sync_cards(writable->profile, colour_of(white));
        if (!synced) {
            fail(tr("The cards were not synced: %1.").arg(describe(synced.error())));
            return;
        }
        emit cards_synced(white, synced->created, synced->reset, synced->deleted);
    } catch (const std::exception&) {
        profiles_.reset();
        fail(tr("profiles.db could not be read."));
    }
}

void ProfileWorker::due_cards(bool white) {
    CardsData data;
    data.white = white;
    try {
        const ProfileStore* profiles = open_profiles(false);
        const auto profile =
            profiles != nullptr ? default_profile(*profiles) : std::optional<std::int64_t>();
        if (profiles != nullptr && profile.has_value()) {
            const std::int64_t at = now();
            data.white_total = static_cast<int>(profiles->cards(*profile, Color::white).size());
            data.white_due =
                static_cast<int>(profiles->due_cards(*profile, Color::white, at).size());
            data.black_total = static_cast<int>(profiles->cards(*profile, Color::black).size());
            data.black_due =
                static_cast<int>(profiles->due_cards(*profile, Color::black, at).size());
            data.total = white ? data.white_total : data.black_total;
            const Color colour = colour_of(white);
            const Repertoire repertoire = profiles->repertoire(*profile, colour);
            const TreeWalk walk = walk_repertoire(repertoire, colour);
            QHash<quint64, QString> fens;
            for (const RepertoireNode& node : profiles->repertoire_nodes(*profile, colour)) {
                fens.insert(node.choice.key, QString::fromStdString(node.choice.fen));
            }
            for (const Card& card : profiles->due_cards(*profile, colour, at)) {
                DrillCard out;
                out.id = card.id;
                out.white = white;
                out.key = card.key;
                out.fen = fens.value(card.key);
                out.move = QString::fromStdString(card.move);
                out.move_san = out.move;
                if (const auto position = Position::from_fen(out.fen.toStdString())) {
                    if (const auto san = position->san_of(card.move)) {
                        out.move_san = QString::fromStdString(*san);
                    }
                }
                const auto name = walk.names.constFind(card.key);
                if (name == walk.names.constEnd()) {
                    out.name = out.fen;
                } else if (name->isEmpty()) {
                    out.name = tr("the start position");
                } else {
                    out.name = *name;
                }
                out.due_at = card.state.due_at;
                data.due.append(out);
            }
        }
    } catch (const std::exception&) {
        profiles_.reset();
        fail(tr("profiles.db could not be read."));
    }
    emit cards_ready(data);
}

void ProfileWorker::record_review(qint64 card_id, bool white, quint64 key, int grade) {
    Grade applied = Grade::again;
    switch (grade) {
    case static_cast<int>(Grade::again):
    case static_cast<int>(Grade::hard):
    case static_cast<int>(Grade::good):
    case static_cast<int>(Grade::easy):
        applied = static_cast<Grade>(grade);
        break;
    default:
        fail(tr("No grade %1.").arg(grade));
        return;
    }
    try {
        const auto writable = writable_profile();
        if (!writable) {
            return;
        }
        const auto card = writable->store->record_review(writable->profile, colour_of(white),
                                                         card_id, key, applied, now());
        if (!card) {
            fail(tr("The review was not recorded: %1.").arg(describe(card.error())));
            return;
        }
        emit review_recorded(card_id, card->state.due_at);
    } catch (const std::exception&) {
        profiles_.reset();
        fail(tr("profiles.db could not be read."));
    }
}

void ProfileWorker::export_profiles(const QString& path) {
    // An export never replaces a file (docs/database.md §12.5).
    if (QFileInfo::exists(path)) {
        emit export_failed(tr("The profile was not exported: %1 exists.").arg(path));
        return;
    }
    const auto exported =
        ProfileStore::export_profiles(data_dir_, std::filesystem::path(path.toStdString()));
    if (!exported) {
        emit export_failed(tr("The profile was not exported: %1.").arg(describe(exported.error())));
        return;
    }
    emit profiles_exported(path);
}

} // namespace chesscoach::app
