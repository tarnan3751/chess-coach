// The Settings dialog (Edit > Settings...): the engine's binary, threads, hash, seconds per
// search and line count, Spar's move limit and seconds per reply, Repertoire's coverage depth and
// whether the app plays its sounds, as Settings stores them
// (docs/reference/app.md, **Settings**). It only edits the values; the window saves and applies
// them when the dialog is accepted. GUI thread only.
#pragma once

#include "engine_worker.hpp"
#include "settings.hpp"

#include <QDialog>
#include <filesystem>
#include <optional>

class QCheckBox;
class QDoubleSpinBox;
class QLineEdit;
class QSpinBox;

namespace chesscoach::app {

class SettingsDialog : public QDialog {
    Q_OBJECT

  public:
    // files is where Browse's chooser starts and keeps its last folder; resources the app
    // bundle's, for the default engine the binary field names.
    SettingsDialog(const EngineSettings& engine, const SparSettings& spar, int repertoire_depth,
                   bool sounds, Settings& files,
                   const std::optional<std::filesystem::path>& resources,
                   QWidget* parent = nullptr);

    [[nodiscard]] EngineSettings engine() const;
    [[nodiscard]] SparSettings spar() const;
    // The coverage depth in plies: twice the moves the field shows.
    [[nodiscard]] int repertoire_depth() const;
    [[nodiscard]] bool sounds_enabled() const;

  private:
    QLineEdit* binary_;
    QSpinBox* threads_;
    QSpinBox* hash_mb_;
    QDoubleSpinBox* seconds_;
    QSpinBox* lines_;
    QSpinBox* spar_end_move_;
    QDoubleSpinBox* spar_seconds_;
    QSpinBox* repertoire_depth_;
    QCheckBox* sounds_;
};

} // namespace chesscoach::app
