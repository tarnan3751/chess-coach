// What the spar worker tells the window about a session (docs/reference/app.md, **Spar**): its
// state after each move, an interruption with the better choices, the summary at the end, the
// saved sessions, and a saved session replayed ply by ply. Plain values with text ready to show,
// copied across the queued connections from the worker's thread, so all are registered metatypes
// (register_spar_types).
#pragma once

#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>
#include <optional>

namespace chesscoach::app {

// One of the moves the coach would rather have seen, with its stats at the tier.
struct SparChoice {
    QString san;
    QString uci;
    QString badges; // the names, "Book, Popular", or empty
    qint64 total = 0;
    qint64 white = 0;
    qint64 draws = 0;
    qint64 black = 0;
};

struct SparInterruption {
    bool dubious = false; // else the move left the book
    QString reason;       // why, as a sentence: "a3 is Dubious (…)."
    QString played_san;
    QString played_uci;
    QString badges;             // the played move's names, "Popular and Dubious", or empty
    std::optional<qint64> loss; // centipawns, when defined
    QVector<SparChoice> better;
};

struct SparState {
    QString phase;       // "You are in the book.", "You have left the book, so …" or "The … over."
    int move_number = 1; // 1 at the start, one more after each black move
    bool user_to_move = false;
    bool ended = false;
};

struct SparSummary {
    quint64 seed = 0;
    QStringList moves_san;
    int interruptions = 0;
    QString phase_reached; // "You stayed in the book." or "You left the book, and …"
    QString result;        // "1-0", "0-1", "1/2-1/2", or empty when it ended at its move limit
    bool ended = false;    // false when stopped by End the game
    qint64 saved_id = 0;   // the saved session's id, 0 when it could not be saved
};

// A saved session, as the sessions list shows it.
struct SparSessionInfo {
    qint64 id = 0;
    qint64 started_at = 0; // Unix seconds
    int tier = 0;
    bool user_white = true;
    int moves = 0;
    qint64 interruptions = 0;
    QString result; // as SparSummary's, or "Ended early"
};

// One ply of a replayed session, with the interruption the user's move met, if any.
struct SparReplayPly {
    QString uci;
    QString san;
    std::optional<SparInterruption> interruption;
};

struct SparReplay {
    qint64 id = 0;
    int tier = 0;
    bool user_white = true;
    QVector<SparReplayPly> plies;
};

// Registers the types above for queued connections; idempotent.
void register_spar_types();

} // namespace chesscoach::app

Q_DECLARE_METATYPE(chesscoach::app::SparChoice)
Q_DECLARE_METATYPE(chesscoach::app::SparInterruption)
Q_DECLARE_METATYPE(chesscoach::app::SparState)
Q_DECLARE_METATYPE(chesscoach::app::SparSummary)
Q_DECLARE_METATYPE(chesscoach::app::SparSessionInfo)
Q_DECLARE_METATYPE(chesscoach::app::SparReplay)
