// The app's sounds (sounds.hpp): the SAN rule, the resource, the null backend and the choice of
// backend. macOS's backend is in sounds_mac.mm.
#include "sounds.hpp"

#include <QFile>
#include <QIODevice>
#include <QtGlobal>

namespace chesscoach::app {

namespace {

std::size_t index_of(SoundEvent event) { return static_cast<std::size_t>(event); }

std::unique_ptr<SoundBackend> make_backend() {
#ifdef __APPLE__
    if (!qEnvironmentVariableIsSet("CHESS_COACH_SILENT")) {
        return std::make_unique<MacSoundBackend>();
    }
#endif
    return std::make_unique<NullSoundBackend>();
}

} // namespace

SoundEvent move_sound(const QString& san) {
    if (san.contains(QLatin1Char('+')) || san.contains(QLatin1Char('#'))) {
        return SoundEvent::check;
    }
    if (san.contains(QLatin1Char('x'))) {
        return SoundEvent::capture;
    }
    return SoundEvent::move;
}

QByteArray sound_data(SoundEvent event) {
    static constexpr std::array<const char*, sound_event_count> names{
        ":/sounds/move.wav",  ":/sounds/capture.wav",      ":/sounds/check.wav",
        ":/sounds/wrong.wav", ":/sounds/interruption.wav",
    };
    QFile file(QString::fromLatin1(names.at(index_of(event))));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

void NullSoundBackend::play(SoundEvent event) {
    last_ = event;
    ++counts_.at(index_of(event));
}

int NullSoundBackend::count(SoundEvent event) const { return counts_.at(index_of(event)); }

Sounds::Sounds() : backend_(make_backend()) {}

void Sounds::play(SoundEvent event) {
    if (enabled_) {
        backend_->play(event);
    }
}

const NullSoundBackend* Sounds::null_backend() const {
    return dynamic_cast<const NullSoundBackend*>(backend_.get());
}

} // namespace chesscoach::app
