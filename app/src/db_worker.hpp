// The database worker: a QObject that lives on its own QThread for the app's life and owns the
// opening database (core's OpeningStore, opened read-only from the file the window found), and
// puts an imported one in place, a copy too long for the GUI thread. The GUI thread never
// touches the store: it calls the slots by queued connection and gets the answers back the same
// way (CLAUDE.md, **app/ is presentation**; docs/reference/app.md, **Database worker**).
#pragma once

#include <chesscoach/opening_store.hpp>

#include "explore_data.hpp"

#include <QObject>
#include <QString>
#include <QVector>
#include <QtGlobal>
#include <optional>

namespace chesscoach::app {

class DbWorker : public QObject {
    Q_OBJECT

  public:
    explicit DbWorker(QObject* parent = nullptr);

    // Opens the database at path, replacing any open one; answers database_opened with the
    // file's data_version and months from meta, or database_failed with a message.
    Q_SLOT void open_database(const QString& path);
    // Puts a copy of source in place as <data_dir>/openings.db (core's import_opening_database),
    // its own store closed meanwhile: answers database_imported and opens the new file, or
    // database_import_failed and opens current, the file it had, again when it is there.
    Q_SLOT void import_database(const QString& data_dir, const QString& source,
                                const QString& current);
    // The position at the end of the line (keys from the start position to it) at the tier;
    // answers position_ready. Without an open database it answers nothing.
    Q_SLOT void query(const QVector<quint64>& line_keys, int tier);

  signals:
    void database_imported(const QString& path, qint64 data_version, const QString& months);
    void database_import_failed(const QString& message);
    void database_opened(const QString& data_version, const QString& months);
    void database_failed(const QString& message);
    void position_ready(const chesscoach::app::ExploreData& data);

  private:
    std::optional<OpeningStore> store_;
};

} // namespace chesscoach::app
