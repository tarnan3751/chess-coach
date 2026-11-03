// The Settings dialog (settings_dialog.hpp).
#include "settings_dialog.hpp"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QThread>
#include <QVBoxLayout>
#include <algorithm>

namespace chesscoach::app {

SettingsDialog::SettingsDialog(const EngineSettings& engine, QWidget* parent)
    : QDialog(parent), binary_(new QLineEdit(engine.binary, this)), threads_(new QSpinBox(this)),
      hash_mb_(new QSpinBox(this)), seconds_(new QDoubleSpinBox(this)), lines_(new QSpinBox(this)) {
    setObjectName(QStringLiteral("settings_dialog"));
    setWindowTitle(tr("Settings"));
    binary_->setObjectName(QStringLiteral("engine_binary"));
    binary_->setMinimumWidth(binary_->fontMetrics().averageCharWidth() * 48);
    binary_->setPlaceholderText(
        tr("CHESS_COACH_STOCKFISH, stockfish on the PATH, or the bundled one"));
    auto* browse = new QPushButton(tr("Browse…"), this);
    browse->setObjectName(QStringLiteral("engine_browse"));
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString chosen =
            QFileDialog::getOpenFileName(this, tr("The engine"), binary_->text());
        if (!chosen.isEmpty()) {
            binary_->setText(chosen);
        }
    });
    auto* binary_row = new QHBoxLayout();
    binary_row->addWidget(binary_, 1);
    binary_row->addWidget(browse);

    threads_->setObjectName(QStringLiteral("engine_threads"));
    threads_->setRange(1, std::max(1, QThread::idealThreadCount()));
    threads_->setValue(engine.threads);
    hash_mb_->setObjectName(QStringLiteral("engine_hash"));
    hash_mb_->setRange(1, 65536);
    hash_mb_->setSuffix(tr(" MB"));
    hash_mb_->setValue(engine.hash_mb);
    seconds_->setObjectName(QStringLiteral("engine_seconds"));
    seconds_->setRange(0.1, 60.0);
    seconds_->setDecimals(1);
    seconds_->setSingleStep(0.1);
    seconds_->setSuffix(tr(" s"));
    seconds_->setValue(engine.seconds);
    lines_->setObjectName(QStringLiteral("engine_lines_setting"));
    lines_->setRange(1, 10);
    lines_->setValue(engine.lines);

    auto* form = new QFormLayout();
    form->addRow(tr("Engine"), binary_row);
    form->addRow(tr("Threads"), threads_);
    form->addRow(tr("Hash"), hash_mb_);
    form->addRow(tr("Seconds per search"), seconds_);
    form->addRow(tr("Lines"), lines_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* column = new QVBoxLayout(this);
    column->addLayout(form);
    column->addWidget(buttons);
}

EngineSettings SettingsDialog::engine() const {
    return EngineSettings{
        .binary = binary_->text().trimmed(),
        .threads = threads_->value(),
        .hash_mb = hash_mb_->value(),
        .seconds = seconds_->value(),
        .lines = lines_->value(),
    };
}

} // namespace chesscoach::app
