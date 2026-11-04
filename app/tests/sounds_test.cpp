// Unit tests of the app's sounds (app; unit): the SAN rule; the null backend, which
// CHESS_COACH_SILENT chooses, playing only while enabled; the five WAVs in the resource, 16-bit
// 44.1 kHz mono, each under 300 ms with its peak at -12 dBFS; and on macOS the NSSound backend
// decoding all five, which is not to say they were heard, and draining a run of silent plays when
// it is destroyed at once.
#include "sounds.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QString>
#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

using chesscoach::app::move_sound;
using chesscoach::app::NullSoundBackend;
using chesscoach::app::sound_data;
using chesscoach::app::sound_event_count;
using chesscoach::app::SoundEvent;
using chesscoach::app::Sounds;

namespace {

SoundEvent san_sound(const char* san) { return move_sound(QString::fromLatin1(san)); }

std::uint32_t byte_at(const QByteArray& bytes, qsizetype at) {
    return static_cast<std::uint8_t>(bytes.at(at));
}

std::uint32_t le16(const QByteArray& bytes, qsizetype at) {
    return byte_at(bytes, at) | (byte_at(bytes, at + 1) << 8U);
}

std::uint32_t le32(const QByteArray& bytes, qsizetype at) {
    return le16(bytes, at) | (le16(bytes, at + 2) << 16U);
}

} // namespace

TEST_CASE("A move sounds as a check, else a capture, else a move", "[sounds]") {
    CHECK(san_sound("e4") == SoundEvent::move);
    CHECK(san_sound("Nf3") == SoundEvent::move);
    CHECK(san_sound("O-O") == SoundEvent::move);
    CHECK(san_sound("e8=Q") == SoundEvent::move);
    CHECK(san_sound("exd5") == SoundEvent::capture);
    CHECK(san_sound("Nbxd7") == SoundEvent::capture);
    CHECK(san_sound("exd8=N") == SoundEvent::capture);
    CHECK(san_sound("Bb5+") == SoundEvent::check);
    CHECK(san_sound("O-O-O+") == SoundEvent::check);
    CHECK(san_sound("Qxf7#") == SoundEvent::check);
    CHECK(san_sound("exd8=Q+") == SoundEvent::check);
}

TEST_CASE("CHESS_COACH_SILENT chooses the null backend, which plays only while enabled",
          "[sounds]") {
    qputenv("CHESS_COACH_SILENT", "1");
    Sounds sounds;
    const NullSoundBackend* null = sounds.null_backend();
    REQUIRE(null != nullptr);
    CHECK(sounds.enabled());
    CHECK(!null->last().has_value());

    sounds.play(SoundEvent::capture);
    CHECK(null->last() == SoundEvent::capture);
    CHECK(null->count(SoundEvent::capture) == 1);

    sounds.set_enabled(false);
    CHECK(!sounds.enabled());
    sounds.play(SoundEvent::move);
    CHECK(null->last() == SoundEvent::capture);
    CHECK(null->count(SoundEvent::move) == 0);

    sounds.set_enabled(true);
    sounds.play(SoundEvent::wrong);
    sounds.play(SoundEvent::wrong);
    CHECK(null->last() == SoundEvent::wrong);
    CHECK(null->count(SoundEvent::wrong) == 2);
    CHECK(null->count(SoundEvent::capture) == 1);
    CHECK(null->count(SoundEvent::interruption) == 0);
    qunsetenv("CHESS_COACH_SILENT");
}

TEST_CASE("Each sound is a short 16-bit 44.1 kHz mono WAV peaking at -12 dBFS", "[sounds]") {
    qsizetype total = 0;
    for (std::size_t index = 0; index < sound_event_count; ++index) {
        const QByteArray wav = sound_data(static_cast<SoundEvent>(index));
        INFO("sound " << index);
        // The canonical 44-byte header tools/make_sounds.py writes, then the samples.
        REQUIRE(wav.size() > 44);
        CHECK(wav.first(4) == QByteArray("RIFF"));
        CHECK(wav.sliced(8, 8) == QByteArray("WAVEfmt "));
        CHECK(le16(wav, 20) == 1U); // PCM
        CHECK(le16(wav, 22) == 1U); // mono
        CHECK(le32(wav, 24) == 44100U);
        CHECK(le16(wav, 34) == 16U);
        CHECK(wav.sliced(36, 4) == QByteArray("data"));
        const std::uint32_t bytes = le32(wav, 40);
        REQUIRE(bytes == static_cast<std::uint32_t>(wav.size() - 44));
        CHECK(bytes / 2 < 44100U * 3 / 10); // under 300 ms

        int peak = 0;
        for (qsizetype at = 44; at + 1 < wav.size(); at += 2) {
            const auto sample = static_cast<std::int16_t>(le16(wav, at));
            peak = std::max(peak, std::abs(static_cast<int>(sample)));
        }
        const double dbfs = 20.0 * std::log10(peak / 32768.0);
        CHECK(std::abs(dbfs + 12.0) < 0.1);
        total += wav.size();
    }
    CHECK(total < 400 * 1024);
}

#ifdef __APPLE__
TEST_CASE("On macOS NSSound decodes the five sounds", "[sounds]") {
    const chesscoach::app::MacSoundBackend backend;
    CHECK(backend.decoded() == static_cast<int>(sound_event_count));
}

TEST_CASE("On macOS a run of plays is drained when the backend goes at once", "[sounds]") {
    // Five plays posted in a row and the backend destroyed straight after, at volume 0, so that
    // nothing is heard: the queue runs the blocks, which hold the backend's sounds, before the
    // destructor returns, and ASan watches the gate's run of it.
    {
        chesscoach::app::MacSoundBackend backend(0.0F);
        for (std::size_t index = 0; index < sound_event_count; ++index) {
            backend.play(static_cast<SoundEvent>(index));
        }
    }
    SUCCEED("the backend drained its queue and went");
}

TEST_CASE("On macOS, without CHESS_COACH_SILENT, the sounds go to NSSound", "[sounds]") {
    qunsetenv("CHESS_COACH_SILENT");
    const Sounds sounds;
    CHECK(sounds.null_backend() == nullptr);
}
#else
TEST_CASE("Off macOS the sounds go to the null backend", "[sounds]") {
    qunsetenv("CHESS_COACH_SILENT");
    const Sounds sounds;
    CHECK(sounds.null_backend() != nullptr);
}
#endif
