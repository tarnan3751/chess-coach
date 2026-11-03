// The app's own file chooser (docs/reference/app.md, **Files**): a QDialog over a
// QFileSystemModel in a QTreeView, showing folders and the files the name filters match, a path
// line, and in save mode a file name; Open or Save, and Cancel. It keeps no state of its own and
// uses neither QSettings nor the platform's file panel, both of which keep theirs outside the data
// directory; the folder it starts in is the last one used, kept in settings.ini. Save mode refuses
// a file that exists (docs/database.md §12.5). GUI thread only.
#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>
#include <cstdint>

class QFileSystemModel;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeView;

namespace chesscoach::app {

class Settings;

class FileChooser : public QDialog {
    Q_OBJECT

  public:
    enum class Mode : std::uint8_t { open, save };

    // directory is where it starts; name is save mode's suggested file name.
    FileChooser(Mode mode, const QString& title, const QString& directory,
                const QStringList& name_filters, const QString& name = QString(),
                QWidget* parent = nullptr);

    // The file chosen, once accepted.
    [[nodiscard]] QString file() const { return file_; }
    // The folder shown: the one selected, or the selected file's.
    [[nodiscard]] QString directory() const { return directory_; }
    // Selects a folder, or a file and its folder, as the path line does on Return.
    void go_to(const QString& path);

    void accept() override;

  private:
    void select(const QString& path);

    Mode mode_;
    QFileSystemModel* model_;
    QTreeView* tree_;
    QLineEdit* path_;
    QLineEdit* name_;
    QLabel* message_;
    QPushButton* accept_;
    QString directory_;
    QString file_;
};

// An existing file the user picks, starting at the folder last used, which the pick then
// becomes; empty when cancelled.
[[nodiscard]] QString choose_file_to_open(QWidget* parent, Settings& settings, const QString& title,
                                          const QStringList& name_filters);
// A file to write that does not exist yet, likewise.
[[nodiscard]] QString choose_file_to_save(QWidget* parent, Settings& settings, const QString& title,
                                          const QStringList& name_filters, const QString& name);

} // namespace chesscoach::app
