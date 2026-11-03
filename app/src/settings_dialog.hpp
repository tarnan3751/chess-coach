// The Settings dialog (Edit > Settings...): the engine's binary, threads, hash, seconds per
// search and line count, and Spar's move limit and seconds per reply, as Settings stores them
// (docs/reference/app.md, **Settings**). It only edits the values; the window saves and applies
// them when the dialog is accepted. GUI thread only.
#pragma once

#include "engine_worker.hpp"
#include "settings.hpp"

#include <QDialog>

class QDoubleSpinBox;
class QLineEdit;
class QSpinBox;

namespace chesscoach::app {

class SettingsDialog : public QDialog {
    Q_OBJECT

  public:
    SettingsDialog(const EngineSettings& engine, const SparSettings& spar,
                   QWidget* parent = nullptr);

    [[nodiscard]] EngineSettings engine() const;
    [[nodiscard]] SparSettings spar() const;

  private:
    QLineEdit* binary_;
    QSpinBox* threads_;
    QSpinBox* hash_mb_;
    QDoubleSpinBox* seconds_;
    QSpinBox* lines_;
    QSpinBox* spar_end_move_;
    QDoubleSpinBox* spar_seconds_;
};

} // namespace chesscoach::app
