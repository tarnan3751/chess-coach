// The app's settings: the theme, the last tier and the engine's, in <data-dir>/settings.ini through
// QSettings in INI format, never the registry or the platform's own settings store
// (docs/reference/app.md,
// **Settings**). Reading never creates anything; the first write creates the data directory and
// the file, the first thing the app writes there. GUI thread only.
#pragma once

#include "engine_worker.hpp"

#include <QList>
#include <QString>
#include <QtGlobal>
#include <optional>
#include <utility>

namespace chesscoach::app {

// Spar's settings: the move number a session ends at and each off-book reply's seconds.
struct SparSettings {
    int end_move = 25;    // 1 to 200
    double seconds = 0.5; // 0.1 to 60
};

class Settings {
  public:
    explicit Settings(const QString& data_dir);

    // "light" or "dark"; light when unset or unknown.
    [[nodiscard]] QString theme() const;
    // The last tier selected, 0 to 6, or nullopt when unset or out of range.
    [[nodiscard]] std::optional<int> last_tier() const;

    // The engine settings, each at its default when unset or out of range: the default binary,
    // half the cores (at least one), 64 MB of hash, 2 s per search (0.1 to 60), 3 lines (1 to
    // 10).
    [[nodiscard]] EngineSettings engine() const;
    [[nodiscard]] static EngineSettings default_engine();

    // Spar's, each at its default when unset or out of range; and a fixed seed, which only a
    // harness sets (nullopt: the clock's).
    [[nodiscard]] SparSettings spar() const;
    [[nodiscard]] std::optional<quint64> spar_seed() const;

    void set_theme(const QString& theme);
    void set_last_tier(int tier);
    void set_engine(const EngineSettings& engine);
    void set_spar(const SparSettings& spar);

    [[nodiscard]] QString file() const { return file_; }

  private:
    [[nodiscard]] QString read(const QString& key) const;
    void write(const QString& key, const QString& value);
    void write(const QList<std::pair<QString, QString>>& values);

    QString data_dir_;
    QString file_;
};

} // namespace chesscoach::app
