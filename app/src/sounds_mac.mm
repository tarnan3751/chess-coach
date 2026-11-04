// macOS's sound backend (sounds.hpp): one NSSound per event, decoded once from the WAV in the
// resource, all of them living on a serial dispatch queue of the backend's own. NSSound's play
// blocks its caller (tens of milliseconds the first time in a process, about a dozen after), so
// the GUI thread only posts to the queue: the sounds are made there at construction, and one is
// played once silently, which opens the output, so that the first move's sound pays no more than
// the others; each play there stops the event's sound and starts it again, since NSSound's play
// refuses a sound that is still playing. The GUI thread never touches an NSSound, and waits on the
// queue only in decoded(), for the tests, and in the destructor, which lets the queue finish first.
// Objective-C++ under ARC; AppKit is a system framework.
#include "sounds.hpp"

#import <AppKit/AppKit.h>
#include <dispatch/dispatch.h>

#include <algorithm>

namespace chesscoach::app {

struct MacSoundBackend::Players {
    dispatch_queue_t queue = dispatch_queue_create("chess-coach.sounds", DISPATCH_QUEUE_SERIAL);
    std::array<NSSound*, sound_event_count> sounds{}; // on the queue only; nil where no decode
};

MacSoundBackend::MacSoundBackend() : players_(std::make_unique<Players>()) {
    // The WAVs are read from the Qt resource here; NSSound decodes them on the queue.
    std::array<QByteArray, sound_event_count> wavs;
    for (std::size_t index = 0; index < sound_event_count; ++index) {
        wavs.at(index) = sound_data(static_cast<SoundEvent>(index));
    }
    Players* const players = players_.get();
    dispatch_async(players->queue, ^{
      @autoreleasepool {
          for (std::size_t index = 0; index < sound_event_count; ++index) {
              const QByteArray& wav = wavs.at(index);
              // alloc and init, not a class message such as dataWithBytes:: Homebrew's clang,
              // which compiles the gate build, sends one through a class stub the system linker
              // lacks.
              NSData* const data =
                  [[NSData alloc] initWithBytes:wav.constData()
                                         length:static_cast<NSUInteger>(wav.size())];
              players->sounds.at(index) = [[NSSound alloc] initWithData:data];
          }
          // The first play in a process opens the output and takes several times as long as the
          // others: it is spent here, at volume 0, before any move.
          NSSound* const first = players->sounds.at(0);
          const float volume = first.volume;
          first.volume = 0.0F;
          [first play];
          [first stop];
          first.volume = volume;
      }
    });
}

MacSoundBackend::~MacSoundBackend() {
    // Every block posted holds a pointer to the players: the queue finishes them, and the sounds
    // are let go there, before the players go.
    Players* const players = players_.get();
    dispatch_sync(players->queue, ^{
      players->sounds = {};
    });
}

void MacSoundBackend::play(SoundEvent event) {
    Players* const players = players_.get();
    const auto index = static_cast<std::size_t>(event);
    dispatch_async(players->queue, ^{
      @autoreleasepool { // a message to nil, a sound that did not decode, does nothing
          NSSound* const sound = players->sounds.at(index);
          [sound stop];
          [sound play];
      }
    });
}

int MacSoundBackend::decoded() const {
    Players* const players = players_.get();
    __block int count = 0;
    dispatch_sync(players->queue, ^{
      count = static_cast<int>(
          std::ranges::count_if(players->sounds, [](NSSound* sound) { return sound != nil; }));
    });
    return count;
}

} // namespace chesscoach::app
