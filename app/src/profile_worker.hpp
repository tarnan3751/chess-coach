// The profile worker: a QObject on its own QThread for the window's life, owning the ProfileStore
// connection through which the Repertoire and Drill screens read and change the Default profile's
// repertoires and cards, and its own read-only OpeningStore connection to --db for coverage and
// the recommendation score (docs/reference/app.md, **Profile worker**). The window calls its
// slots by queued connection, close_stores by a blocking one, and gets every answer back queued;
// the GUI thread never touches either store. profiles.db is opened when it exists or when a slot
// writes, so a run that changes nothing creates nothing.
#pragma once

#include <chesscoach/opening_store.hpp>
#include <chesscoach/profile_store.hpp>

#include "profile_data.hpp"

#include <QObject>
#include <QString>
#include <QtGlobal>
#include <cstdint>
#include <filesystem>
#include <optional>

namespace chesscoach::app {

class ProfileWorker : public QObject {
    Q_OBJECT

  public:
    // data_dir holds profiles.db; db is the opening database, opened read-only when first needed.
    ProfileWorker(const QString& data_dir, QString db, QObject* parent = nullptr);
    ~ProfileWorker() override;
    Q_DISABLE_COPY_MOVE(ProfileWorker)

    // What a profile store error says, for a message.
    [[nodiscard]] static QString describe(ProfileError error);

    // Closes the profile store, so that another may replace profiles.db; the next slot reopens it.
    Q_SLOT void close_stores();
    // The opening database to read coverage and scores from now on.
    Q_SLOT void set_database(const QString& path);
    // The colour's choice at the position, replacing any: repertoire_changed.
    Q_SLOT void set_choice(bool white, quint64 key, const QString& fen, const QString& uci);
    Q_SLOT void remove_choice(bool white, quint64 key);
    // repertoire_ready: the colour's repertoire as a tree of lines from the start.
    Q_SLOT void load_repertoire(bool white);
    // coverage_ready: the colour's coverage at the tier to depth plies, with the gaps.
    Q_SLOT void compute_coverage(bool white, int tier, int depth);
    // scores_ready: the recommendation score of the position's moves at the tier.
    Q_SLOT void score_position(quint64 key, int tier);
    // pgn_imported, then repertoire_changed: the file's choices for the colour added where the
    // repertoire has none.
    Q_SLOT void import_pgn(const QString& path, bool white);
    // cards_synced: the colour's cards brought in step with its repertoire.
    Q_SLOT void sync_cards(bool white);
    // cards_ready: the colour's cards due now, with both repertoires' progress.
    Q_SLOT void due_cards(bool white);
    // review_recorded: the grade (core's Grade value) applied to the card now; the card is the
    // colour's card with the id at the key, as cards_ready listed it.
    Q_SLOT void record_review(qint64 card_id, bool white, quint64 key, int grade);
    // profiles_imported: profiles.db replaced by the file at path (ProfileStore::import_profiles),
    // this worker's store closed first; the other workers must have closed theirs.
    Q_SLOT void import_profiles(const QString& path);
    // profiles_exported: a copy of profiles.db at path; export_failed when a file is there.
    Q_SLOT void export_profiles(const QString& path);

  signals:
    void repertoire_changed(bool white, const QString& message);
    void repertoire_ready(const chesscoach::app::RepertoireData& repertoire);
    void coverage_ready(const chesscoach::app::CoverageData& coverage);
    void scores_ready(const chesscoach::app::ScoreData& scores);
    void pgn_imported(const chesscoach::app::PgnImportInfo& import);
    void cards_synced(bool white, int created, int reset, int deleted);
    void cards_ready(const chesscoach::app::CardsData& cards);
    void review_recorded(qint64 card_id, qint64 due_at);
    void profiles_exported(const QString& path);
    void export_failed(const QString& message);
    // error is empty when the import succeeded.
    void profiles_imported(const QString& path, const QString& error);
    void profile_failed(const QString& message);

  private:
    // The store, or nullptr: without create, when profiles.db does not exist yet.
    [[nodiscard]] ProfileStore* open_profiles(bool create);
    [[nodiscard]] const OpeningStore* open_store();
    // The store opened with create and its Default profile, or nullopt after reporting why not.
    struct Writable {
        ProfileStore* store = nullptr;
        std::int64_t profile = 0;
    };
    [[nodiscard]] std::optional<Writable> writable_profile();
    void fail(const QString& message);

    std::filesystem::path data_dir_;
    QString db_;
    std::optional<OpeningStore> store_;
    std::optional<ProfileStore> profiles_;
};

} // namespace chesscoach::app
