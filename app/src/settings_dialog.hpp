// The Settings dialog (Edit > Settings...): the engine's binary, threads, hash, seconds per
// search and line count, as Settings stores them (docs/reference/app.md, **Settings**). It only
// edits the values; the window saves and applies them when the dialog is accepted. GUI thread
// only.
#pragma once

#include "engine_worker.hpp"

#include <QDialog>

class QDoubleSpinBox;
class QLineEdit;
class QSpinBox;

namespace chesscoach::app {

class SettingsDialog : public QDialog {
    Q_OBJECT

  public:
    explicit SettingsDialog(const EngineSettings& engine, QWidget* parent = nullptr);

    [[nodiscard]] EngineSettings engine() const;

  private:
    QLineEdit* binary_;
    QSpinBox* threads_;
    QSpinBox* hash_mb_;
    QDoubleSpinBox* seconds_;
    QSpinBox* lines_;
};

} // namespace chesscoach::app
