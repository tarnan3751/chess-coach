// The app's own file chooser (file_chooser.hpp).
#include "file_chooser.hpp"

#include "settings.hpp"

#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeView>
#include <QVBoxLayout>

namespace chesscoach::app {

namespace {

// The path as typed, with a leading ~ for the home folder and the platform's separators read.
QString expanded(const QString& typed) {
    QString path = QDir::fromNativeSeparators(typed.trimmed());
    if (path == QStringLiteral("~") || path.startsWith(QStringLiteral("~/"))) {
        path = QDir::homePath() + path.mid(1);
    }
    return path;
}

} // namespace

FileChooser::FileChooser(Mode mode, const QString& title, const QString& directory,
                         const QStringList& name_filters, const QString& name, QWidget* parent)
    : QDialog(parent), mode_(mode), model_(new QFileSystemModel(this)), tree_(new QTreeView(this)),
      path_(new QLineEdit(this)), name_(new QLineEdit(name, this)), message_(new QLabel(this)),
      accept_(new QPushButton(mode == Mode::open ? tr("Open") : tr("Save"), this)) {
    setObjectName(QStringLiteral("file_chooser"));
    setWindowTitle(title);
    tree_->setObjectName(QStringLiteral("file_chooser_tree"));
    path_->setObjectName(QStringLiteral("file_chooser_path"));
    name_->setObjectName(QStringLiteral("file_chooser_name"));
    message_->setObjectName(QStringLiteral("file_chooser_message"));
    accept_->setObjectName(QStringLiteral("file_chooser_accept"));
    accept_->setDefault(true);
    message_->setWordWrap(true);
    message_->hide();

    model_->setReadOnly(true);
    model_->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Drives);
    model_->setNameFilters(name_filters);
    model_->setNameFilterDisables(false); // the files the filters do not match are not shown
    model_->setRootPath(QString());
    tree_->setModel(model_);
    for (int column = 1; column < model_->columnCount(); ++column) {
        tree_->hideColumn(column);
    }
    tree_->header()->hide();
    tree_->setMinimumSize(520, 360);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    buttons->addButton(accept_, QDialogButtonBox::AcceptRole);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(accept_, &QPushButton::clicked, this, &FileChooser::accept);
    // Return in the path line goes where it says and never reaches the default button.
    path_->installEventFilter(this);
    // Which of the two lines the user edited last decides the name a typed file path gives.
    connect(path_, &QLineEdit::textEdited, this, [this] { name_edited_last_ = false; });
    connect(name_, &QLineEdit::textEdited, this, [this] { name_edited_last_ = true; });
    connect(tree_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current) { select(model_->filePath(current)); });
    connect(tree_, &QTreeView::doubleClicked, this, [this](const QModelIndex& index) {
        if (!model_->isDir(index)) {
            accept();
        }
    });

    auto* column = new QVBoxLayout(this);
    column->addWidget(path_);
    column->addWidget(tree_, 1);
    if (mode_ == Mode::save) {
        auto* naming = new QHBoxLayout();
        naming->addWidget(new QLabel(tr("File name"), this));
        naming->addWidget(name_, 1);
        column->addLayout(naming);
    } else {
        name_->hide();
    }
    column->addWidget(message_);
    column->addWidget(buttons);
    go_to(QFileInfo(directory).isDir() ? directory : QDir::homePath());
}

bool FileChooser::eventFilter(QObject* watched, QEvent* event) {
    const auto* key_event = dynamic_cast<const QKeyEvent*>(event);
    if (watched == path_ && event->type() == QEvent::KeyPress && key_event != nullptr) {
        const int key = key_event->key();
        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            go_to(path_->text());
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void FileChooser::go_to(const QString& typed) {
    QString path = expanded(typed);
    // In save mode a file that does not exist yet names its folder and the file's name.
    QString new_name;
    if (mode_ == Mode::save && !QFileInfo::exists(path) && QFileInfo(path).absoluteDir().exists()) {
        new_name = QFileInfo(path).fileName();
        path = QFileInfo(path).absolutePath();
    }
    const QModelIndex index = model_->index(QFileInfo(path).absoluteFilePath());
    if (!index.isValid()) {
        message_->setText(tr("There is no file or folder at %1. Check the path.").arg(path));
        message_->show();
        return;
    }
    tree_->setCurrentIndex(index);
    tree_->scrollTo(index);
    if (model_->isDir(index)) {
        tree_->expand(index);
    }
    select(model_->filePath(index));
    if (!new_name.isEmpty()) {
        name_->setText(new_name);
    }
}

void FileChooser::select(const QString& path) {
    const QFileInfo info(path);
    message_->hide();
    directory_ = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    if (info.isDir() || mode_ == Mode::open) {
        path_->setText(QDir::toNativeSeparators(info.absoluteFilePath()));
        return;
    }
    // In save mode the path line holds the folder, and the file's name goes to its own field.
    path_->setText(QDir::toNativeSeparators(directory_));
    name_->setText(info.fileName());
}

void FileChooser::accept() {
    if (mode_ == Mode::open) {
        const QFileInfo chosen(expanded(path_->text()));
        if (!chosen.isFile()) {
            message_->setText(tr("Choose a file in the list, then press Open."));
            message_->show();
            return;
        }
        file_ = chosen.absoluteFilePath();
        QDialog::accept();
        return;
    }
    // The path line holds the folder, or a file path typed there without Return, which then
    // gives the folder and, unless the name field was edited after it, the name: the user's
    // last edit wins.
    QString directory = directory_;
    QString name = name_->text().trimmed();
    if (const QFileInfo typed(expanded(path_->text())); !path_->text().trimmed().isEmpty()) {
        if (typed.isDir()) {
            directory = typed.absoluteFilePath();
        } else if (typed.absoluteDir().exists()) {
            directory = typed.absolutePath();
            if (!name_edited_last_) {
                name = typed.fileName();
            }
        } else {
            message_->setText(tr("There is no folder at %1. Check the path.").arg(path_->text()));
            message_->show();
            return;
        }
    }
    if (name.isEmpty()) {
        message_->setText(tr("Type a name for the file."));
        message_->show();
        return;
    }
    if (name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))) {
        message_->setText(
            tr("A file name cannot contain a slash. Type the folder in the line at the top."));
        message_->show();
        return;
    }
    const QString target = QDir(directory).filePath(name);
    if (QFileInfo::exists(target)) {
        // An export never replaces a file (docs/database.md §12.5).
        message_->setText(tr("%1 exists already. Type another name.").arg(name));
        message_->show();
        return;
    }
    file_ = target;
    QDialog::accept();
}

namespace {

QString chosen(FileChooser& chooser, Settings& settings) {
    if (chooser.exec() != QDialog::Accepted) {
        return {};
    }
    settings.set_last_dir(QFileInfo(chooser.file()).absolutePath());
    return chooser.file();
}

} // namespace

QString choose_file_to_open(QWidget* parent, Settings& settings, const QString& title,
                            const QStringList& name_filters) {
    FileChooser chooser(FileChooser::Mode::open, title, settings.last_dir(), name_filters,
                        QString(), parent);
    return chosen(chooser, settings);
}

QString choose_file_to_save(QWidget* parent, Settings& settings, const QString& title,
                            const QStringList& name_filters, const QString& name) {
    FileChooser chooser(FileChooser::Mode::save, title, settings.last_dir(), name_filters, name,
                        parent);
    return chosen(chooser, settings);
}

} // namespace chesscoach::app
