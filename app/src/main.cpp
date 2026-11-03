// chess-coach: the entry point. Parses --data-dir and --db and opens the main window. The data
// directory is the option's, or the platform's application data location when it is absent
// (QStandardPaths::AppDataLocation, the owner's real data, which is why tests and agents always
// pass --data-dir). The window reads and writes there and nowhere else: settings.ini and
// profiles.db, and profiles.db.previous after an import (docs/reference/app.md). Wiring only: the
// window and the board are app_lib's.
#include <chesscoach/version.hpp>

#include "main_window.hpp"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QString>
#include <QStringList>

int main(int argc, char* argv[]) {
    const QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("chess-coach"));
    QApplication::setApplicationVersion(
        QString::fromStdString(chesscoach::to_string(chesscoach::current_version())));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QCoreApplication::translate("main", "An offline chess opening coach."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption data_dir_option(
        QStringList{QStringLiteral("data-dir")},
        QCoreApplication::translate("main", "The directory for profiles, repertoires and the "
                                            "engine cache (default: the application data "
                                            "location)."),
        QCoreApplication::translate("main", "dir"));
    const QCommandLineOption db_option(
        QStringList{QStringLiteral("db")},
        QCoreApplication::translate("main", "The opening database file."),
        QCoreApplication::translate("main", "file"));
    parser.addOption(data_dir_option);
    parser.addOption(db_option);
    parser.process(application);

    const QString data_dir =
        parser.isSet(data_dir_option)
            ? parser.value(data_dir_option)
            : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    chesscoach::app::MainWindow window(data_dir, parser.value(db_option));
    window.show();
    return QApplication::exec();
}
