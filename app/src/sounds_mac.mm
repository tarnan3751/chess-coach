// macOS's sound backend (sounds.hpp): one NSSound per event, decoded once from the WAV in the
// resource; a play stops the event's sound and starts it again, since NSSound's play refuses a
// sound that is still playing. Objective-C++ under ARC; AppKit is a system framework. GUI thread
// only.
#include "sounds.hpp"

#import <AppKit/AppKit.h>

#include <algorithm>

namespace chesscoach::app {

struct MacSoundBackend::Players {
    std::array<NSSound*, sound_event_count> sounds{}; // nil where the WAV did not decode
};

MacSoundBackend::MacSoundBackend() : players_(std::make_unique<Players>()) {
    @autoreleasepool {
        for (std::size_t index = 0; index < sound_event_count; ++index) {
            const QByteArray wav = sound_data(static_cast<SoundEvent>(index));
            // alloc and init, not a class message such as dataWithBytes:: Homebrew's clang, which
            // compiles the gate build, sends one through a class stub the system linker lacks.
            NSData* const data = [[NSData alloc] initWithBytes:wav.constData()
                                                        length:static_cast<NSUInteger>(wav.size())];
            players_->sounds.at(index) = [[NSSound alloc] initWithData:data];
        }
    }
}

MacSoundBackend::~MacSoundBackend() = default;

void MacSoundBackend::play(SoundEvent event) {
    NSSound* const sound = players_->sounds.at(static_cast<std::size_t>(event));
    @autoreleasepool { // a message to nil, a sound that did not decode, does nothing
        [sound stop];
        [sound play];
    }
}

int MacSoundBackend::decoded() const {
    return static_cast<int>(
        std::ranges::count_if(players_->sounds, [](NSSound* sound) { return sound != nil; }));
}

} // namespace chesscoach::app
