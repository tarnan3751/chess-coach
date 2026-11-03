// The app's own file chooser (file_chooser.hpp).
#include "file_chooser.hpp"

#include "settings.hpp"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeView>
#include <QVBoxLayout>

namespace chesscoach::app {

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
    connect(path_, &QLineEdit::returnPressed, this, [this] { go_to(path_->text()); });
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

void FileChooser::go_to(const QString& path) {
    const QModelIndex index = model_->index(QFileInfo(path).absoluteFilePath());
    if (!index.isValid()) {
        message_->setText(tr("%1 is not there.").arg(path));
        message_->show();
        return;
    }
    tree_->setCurrentIndex(index);
    tree_->scrollTo(index);
    if (model_->isDir(index)) {
        tree_->expand(index);
    }
    select(model_->filePath(index));
}

void FileChooser::select(const QString& path) {
    const QFileInfo info(path);
    message_->hide();
    path_->setText(QDir::toNativeSeparators(info.absoluteFilePath()));
    if (info.isDir()) {
        directory_ = info.absoluteFilePath();
        return;
    }
    directory_ = info.absolutePath();
    if (mode_ == Mode::save) {
        name_->setText(info.fileName());
    }
}

void FileChooser::accept() {
    if (mode_ == Mode::open) {
        const QFileInfo chosen(QDir::fromNativeSeparators(path_->text()));
        if (!chosen.isFile()) {
            message_->setText(tr("Choose a file."));
            message_->show();
            return;
        }
        file_ = chosen.absoluteFilePath();
        QDialog::accept();
        return;
    }
    const QString name = name_->text().trimmed();
    if (name.isEmpty()) {
        message_->setText(tr("Give the file a name."));
        message_->show();
        return;
    }
    const QString target = QDir(directory_).filePath(name);
    if (QFileInfo::exists(target)) {
        // An export never replaces a file (docs/database.md §12.5).
        message_->setText(tr("%1 exists: choose another name.").arg(name));
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
