// The app's settings: the theme and the last tier, in <data-dir>/settings.ini through QSettings
// in INI format, never the registry or the platform's own settings store (docs/reference/app.md,
// **Settings**). Reading never creates anything; the first write creates the data directory and
// the file, the first thing the app writes there. GUI thread only.
#pragma once

#include <QString>
#include <optional>

namespace chesscoach::app {

class Settings {
  public:
    explicit Settings(const QString& data_dir);

    // "light" or "dark"; light when unset or unknown.
    [[nodiscard]] QString theme() const;
    // The last tier selected, 0 to 6, or nullopt when unset or out of range.
    [[nodiscard]] std::optional<int> last_tier() const;

    void set_theme(const QString& theme);
    void set_last_tier(int tier);

    [[nodiscard]] QString file() const { return file_; }

  private:
    [[nodiscard]] QString read(const QString& key) const;
    void write(const QString& key, const QString& value);

    QString data_dir_;
    QString file_;
};

} // namespace chesscoach::app
