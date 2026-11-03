// What the profile worker tells the window about the repertoires and their cards
// (docs/reference/app.md, **Profile worker**): a colour's repertoire as a tree of lines from the
// start, its coverage at a tier with the gaps, the recommendation score of a position's moves, a
// PGN import's counts, and the cards to drill with the progress of both repertoires. Plain values
// with text ready to show, copied across the queued connections from the worker's thread, so all
// are registered metatypes (register_profile_types).
#pragma once

#include <QHash>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>
#include <optional>

namespace chesscoach::app {

// One move of the repertoire tree: a chosen move, or an opponent's reply that leads to a chosen
// one. parent is the index of the node it follows, -1 at the start; line is the UCI from the start
// up to and including the move.
struct RepertoireTreeNode {
    int parent = -1;
    QString text; // "1. e4", "1... c5"
    QStringList line;
    bool chosen = false;
};

struct RepertoireData {
    bool white = true;
    QVector<RepertoireTreeNode> tree; // a node after its parent
    QHash<quint64, QString> choices;  // key -> the chosen move in UCI
};

struct CoverageGap {
    double mass = 0.0;
    QString line_san; // "1. a3 e5"
    QStringList line; // UCI from the start
    QString fen;
};

struct CoverageData {
    bool white = true;
    int tier = 0;
    int depth = 0;
    double covered = 0.0; // of a total of 1
    QVector<CoverageGap> gaps;
};

// One move's recommendation score and its components (core's ScoreComponents).
struct ScoreRow {
    QString san;
    QString uci;
    bool sound = true;
    std::optional<qint64> loss;
    double expected = 0.5;
    int replies = 0;
    double penalty = 0.0;
    double score = 0.0;
    double share = 0.0;
};

struct ScoreData {
    quint64 key = 0;
    int tier = 0;
    QVector<ScoreRow> rows; // by score, highest first
};

// What a PGN import read and what the repertoire took of it.
struct PgnImportInfo {
    bool white = true;
    int games = 0;
    int variations = 0;
    int choices = 0;
    int added = 0;
    int unchanged = 0;
    int conflicts = 0; // within the file, and with the choices already made
    int unparsable = 0;
};

struct DrillCard {
    qint64 id = 0;
    bool white = true;
    quint64 key = 0;
    QString fen;
    QString move;     // UCI
    QString move_san; // the answer as SAN
    QString name;     // the SAN line the repertoire tree reaches it by, else the FEN
    qint64 due_at = 0;
};

struct CardsData {
    bool white = true;
    QVector<DrillCard> due; // by due time, then key
    int total = 0;
    int white_due = 0;
    int white_total = 0;
    int black_due = 0;
    int black_total = 0;
};

void register_profile_types();

} // namespace chesscoach::app

Q_DECLARE_METATYPE(chesscoach::app::RepertoireData)
Q_DECLARE_METATYPE(chesscoach::app::CoverageData)
Q_DECLARE_METATYPE(chesscoach::app::ScoreData)
Q_DECLARE_METATYPE(chesscoach::app::PgnImportInfo)
Q_DECLARE_METATYPE(chesscoach::app::CardsData)
