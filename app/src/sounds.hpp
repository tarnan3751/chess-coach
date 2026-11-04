// The app's sounds (docs/reference/app.md, **Sounds**): a move, a capture, a check, a wrong answer
// in Drill and the coach's interruption in Practice, each a WAV compiled in under :/sounds/. Not
// Qt Multimedia, which links QtNetwork (PLAN.md §11, 2026-11-04): Sounds plays them through a
// backend of its own, on macOS NSSound (sounds_mac.mm), elsewhere, and whenever the environment
// variable CHESS_COACH_SILENT is set, the null backend, which plays nothing and records what it
// was given for the tests. GUI thread only.
#pragma once

#include <QByteArray>
#include <QString>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace chesscoach::app {

enum class SoundEvent : std::uint8_t { move, capture, check, wrong, interruption };
inline constexpr std::size_t sound_event_count = 5;

// The sound of a move from its SAN: check when it carries + or #, else capture when it carries
// x, else move.
[[nodiscard]] SoundEvent move_sound(const QString& san);

// The event's WAV file, read from the resource (:/sounds/<event>.wav).
[[nodiscard]] QByteArray sound_data(SoundEvent event);

// What plays the sounds.
class SoundBackend {
  public:
    SoundBackend() = default;
    virtual ~SoundBackend() = default;
    SoundBackend(const SoundBackend&) = delete;
    SoundBackend& operator=(const SoundBackend&) = delete;
    SoundBackend(SoundBackend&&) = delete;
    SoundBackend& operator=(SoundBackend&&) = delete;

    virtual void play(SoundEvent event) = 0;
};

// Plays nothing; records the last event it was given and how often it was given each.
class NullSoundBackend final : public SoundBackend {
  public:
    void play(SoundEvent event) override;

    [[nodiscard]] std::optional<SoundEvent> last() const { return last_; }
    [[nodiscard]] int count(SoundEvent event) const;

  private:
    std::optional<SoundEvent> last_;
    std::array<int, sound_event_count> counts_{};
};

#ifdef __APPLE__
// macOS's (sounds_mac.mm): one NSSound per event, decoded from its WAV when it is made, all living
// on a serial dispatch queue of the backend's own, since NSSound's play blocks its caller; play
// posts to the queue and returns, and there the event's sound is stopped and started again, so a
// fast sequence drops none. decoded() and the destructor wait for the queue.
class MacSoundBackend final : public SoundBackend {
  public:
    // volume, 0 to 1, is every sound's: the app's 1, the tests' 0, which plays them silently.
    explicit MacSoundBackend(float volume = 1.0F);
    ~MacSoundBackend() override;
    MacSoundBackend(const MacSoundBackend&) = delete;
    MacSoundBackend& operator=(const MacSoundBackend&) = delete;
    MacSoundBackend(MacSoundBackend&&) = delete;
    MacSoundBackend& operator=(MacSoundBackend&&) = delete;

    void play(SoundEvent event) override;
    // How many of the five sounds decoded (NSSound took the WAV).
    [[nodiscard]] int decoded() const;

  private:
    struct Players; // the NSSound objects, in sounds_mac.mm
    std::unique_ptr<Players> players_;
};
#endif

class Sounds {
  public:
    // macOS's backend, or the null one when CHESS_COACH_SILENT is set or on another platform.
    // Enabled.
    Sounds();

    // Plays the event's sound when enabled; nothing otherwise.
    void play(SoundEvent event);
    void set_enabled(bool enabled) { enabled_ = enabled; }
    [[nodiscard]] bool enabled() const { return enabled_; }

    // The null backend when it is the one sounding, for the tests; null otherwise.
    [[nodiscard]] const NullSoundBackend* null_backend() const;

  private:
    std::unique_ptr<SoundBackend> backend_;
    bool enabled_ = true;
};

} // namespace chesscoach::app
