// Where the app finds the opening database and what it ships beside itself
// (docs/reference/app.md, **The database**): the app bundle's resources, which the window works
// out once at start and hands to the workers, the opening database core's locator picks, and the
// message that names the places it looks. GUI thread only.
#pragma once

#include <QString>
#include <filesystem>
#include <optional>

namespace chesscoach::app {

// The app bundle's Contents/Resources: CHESS_COACH_BUNDLE_RESOURCES when it is set (the UI tests'
// hook), else <the executable's folder>/../Resources when the executable runs inside a .app
// bundle, else nullopt.
[[nodiscard]] std::optional<std::filesystem::path> bundle_resources();

// The opening database to open and where it was found.
struct DatabaseChoice {
    std::filesystem::path path;
    QString source; // "--db", "data directory" or "app bundle", translated
};

// core's locate_opening_database over db (the --db option, when given, which it returns whether
// the file is there or not, so the open reports it), else the data directory and resources, the
// bundle's.
[[nodiscard]] std::optional<DatabaseChoice>
find_opening_database(const QString& db, const QString& data_dir,
                      const std::optional<std::filesystem::path>& resources);

// What the app says when it finds no opening database: the three places, and the import.
[[nodiscard]] QString no_database_text(const QString& data_dir);

} // namespace chesscoach::app
