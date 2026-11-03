// Where the app finds the opening database (locations.hpp).
#include "locations.hpp"

#include <chesscoach/opening_locator.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QtGlobal>

namespace chesscoach::app {

namespace {

QString translate(const char* text) {
    return QCoreApplication::translate("chesscoach::app::locations", text);
}

} // namespace

std::optional<std::filesystem::path> bundle_resources() {
    if (const QString hook = qEnvironmentVariable("CHESS_COACH_BUNDLE_RESOURCES");
        !hook.isEmpty()) {
        return std::filesystem::path(hook.toStdString());
    }
    const QString executable_dir = QCoreApplication::applicationDirPath();
    if (!executable_dir.endsWith(QStringLiteral(".app/Contents/MacOS"))) {
        return std::nullopt;
    }
    return std::filesystem::path(
        QDir::cleanPath(QDir(executable_dir).filePath(QStringLiteral("../Resources")))
            .toStdString());
}

std::optional<DatabaseChoice>
find_opening_database(const QString& db, const QString& data_dir,
                      const std::optional<std::filesystem::path>& resources) {
    LocatorInput input;
    if (!db.isEmpty()) {
        input.explicit_db = std::filesystem::path(db.toStdString());
    }
    input.data_dir = std::filesystem::path(data_dir.toStdString());
    input.bundle_resources = resources;
    const auto found = locate_opening_database(input);
    if (!found) {
        return std::nullopt;
    }
    QString source = translate("app bundle");
    if (input.explicit_db && *found == *input.explicit_db) {
        source = translate("--db");
    } else if (*found == input.data_dir / "openings.db") {
        source = translate("data directory");
    }
    return DatabaseChoice{.path = *found, .source = source};
}

QString no_database_text(const QString& data_dir) {
    return translate("No opening database. The app looks for the file --db names, then for "
                     "openings.db in the data directory (%1), then in the app bundle. Import one "
                     "with File > Import database…")
        .arg(QDir::toNativeSeparators(data_dir));
}

} // namespace chesscoach::app
