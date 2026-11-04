#!/usr/bin/env python3
# Writes the app's five sounds (docs/reference/app.md, **Sounds**) as WAV files, 16-bit
# 44.1 kHz mono, each under 300 ms, its peak at -12 dBFS:
#   move.wav          a wooden thunk, 130 ms: a damped low noise burst, its energy between about
#                     100 and 250 Hz and about half the sound's, under a millisecond's broadband
#                     click at the onset (the piece's contact) and a short wooden ring above them
#   capture.wav       the same, heavier: a lower, longer thud with three fifths of the energy
#   check.wav         a short two-tone, a rising pair of soft bells
#   wrong.wav         a soft low tone gliding down
#   interruption.wav  a soft, low, muffled knock, twice: no click, a ringing pitch near 230 Hz
# Synthesised here rather than taken from a sound pack, so they are our own work under the
# project's licence and need no notice. Standard library only; a fixed seed for the noise, so a
# run writes the same bytes on the same machine.
#   tools/make_sounds.py [<out-dir>]     (default app/resources/sounds)
import math
import random
import struct
import sys
import wave
from pathlib import Path

RATE = 44100
PEAK = 10 ** (-12 / 20)  # -12 dBFS


def silence(seconds):
    return [0.0] * round(seconds * RATE)


def burst(seconds, tau, rng):
    """White noise under an exponential decay of time constant tau."""
    return [rng.uniform(-1.0, 1.0) * math.exp(-n / (tau * RATE))
            for n in range(round(seconds * RATE))]


def low_pass(signal, cutoff):
    """One-pole low pass."""
    a = 1.0 - math.exp(-2.0 * math.pi * cutoff / RATE)
    y = 0.0
    out = []
    for x in signal:
        y += a * (x - y)
        out.append(y)
    return out


def high_pass(signal, cutoff):
    """One-pole high pass: the signal less its low pass."""
    return [x - y for x, y in zip(signal, low_pass(signal, cutoff))]


def normalised(signal):
    """The signal scaled to a peak of 1."""
    top = max(abs(s) for s in signal) or 1.0
    return [s / top for s in signal]


def energy(signal):
    return sum(s * s for s in signal)


def resonate(signal, freq, tau):
    """A two-pole resonator at freq ringing with time constant tau, scaled to a peak of 1."""
    r = math.exp(-1.0 / (tau * RATE))
    c = 2.0 * r * math.cos(2.0 * math.pi * freq / RATE)
    y1 = y2 = 0.0
    out = []
    for x in signal:
        y = x + c * y1 - r * r * y2
        out.append(y)
        y2, y1 = y1, y
    return normalised(out)


def mix(length, *parts):
    """The weighted sum of (weight, offset in seconds, signal) parts over length seconds."""
    out = [0.0] * round(length * RATE)
    for weight, offset, signal in parts:
        start = round(offset * RATE)
        for n, s in enumerate(signal[: len(out) - start]):
            out[start + n] += weight * s
    return out


def tone(seconds, start_hz, end_hz, attack, tau, harmonics=((1, 1.0),)):
    """A sine gliding from start_hz to end_hz, with a raised-cosine attack and an exponential
    decay, and the harmonics given as (multiple, weight)."""
    out = []
    phase = 0.0
    count = round(seconds * RATE)
    for n in range(count):
        t = n / RATE
        freq = start_hz + (end_hz - start_hz) * n / count
        phase += 2.0 * math.pi * freq / RATE
        envelope = math.exp(-t / tau)
        if t < attack:
            envelope *= 0.5 - 0.5 * math.cos(math.pi * t / attack)
        out.append(envelope * sum(w * math.sin(k * phase) for k, w in harmonics))
    return out


def knock(rng, modes, length, excite=0.004, soften=None):
    """An impact: a short noise burst, softened by a low pass when given, ringing the modes
    (frequency, time constant, weight)."""
    hit = burst(excite, excite / 4, rng) + silence(length - excite)
    if soften:
        hit = low_pass(hit, soften)
    return mix(length, *((w, 0.0, resonate(hit, f, tau)) for f, tau, w in modes))


def thud(rng, length, tau, low, high):
    """A damped low noise burst, its energy between low and high Hz (two one-pole high passes
    after three one-pole low passes), scaled to a peak of 1."""
    signal = burst(length, tau, rng)
    for _ in range(3):
        signal = low_pass(signal, high)
    return normalised(high_pass(high_pass(signal, low), low))


def click(rng, length):
    """A millisecond of broadband noise at the onset, the piece's contact, scaled to a peak of 1."""
    return normalised(burst(0.001, 0.0004, rng)) + silence(length - 0.001)


def thunk(rng, length, thud_shape, ring_modes, click_weight, low_share):
    """A piece set down: the damped low thud (tau, low, high) carrying low_share of the energy,
    under a click at the onset and a short wooden ring of the modes (frequency, tau, weight)."""
    above = mix(length, (1.0, 0.0, knock(rng, ring_modes, length)),
                (click_weight, 0.0, click(rng, length)))
    low = thud(rng, length, *thud_shape)
    weight = math.sqrt(low_share / (1.0 - low_share) * energy(above) / energy(low))
    return mix(length, (1.0, 0.0, above), (weight, 0.0, low))


def finish(signal, fade=0.012):
    """Fades the last fade seconds out and scales the peak to -12 dBFS."""
    count = round(fade * RATE)
    out = list(signal)
    for n in range(count):
        out[len(out) - count + n] *= 0.5 + 0.5 * math.cos(math.pi * (n + 1) / count)
    top = max(abs(s) for s in out)
    return [s * PEAK / top for s in out]


def move(rng):
    return thunk(rng, 0.13, (0.014, 100, 200),
                 [(620, 0.006, 1.0), (1400, 0.0035, 0.5), (2900, 0.0018, 0.25)],
                 click_weight=0.4, low_share=0.6)


def capture(rng):
    return thunk(rng, 0.19, (0.028, 70, 170),
                 [(480, 0.009, 1.0), (1100, 0.005, 0.55), (2300, 0.0025, 0.3)],
                 click_weight=0.55, low_share=0.62)


def check(_rng):
    bell = ((1, 1.0), (2, 0.15))
    return mix(0.19, (1.0, 0.0, tone(0.085, 740, 740, 0.004, 0.040, bell)),
               (1.0, 0.09, tone(0.10, 988, 988, 0.004, 0.045, bell)))


def wrong(_rng):
    return tone(0.28, 294, 247, 0.015, 0.11, ((1, 1.0), (2, 0.3), (3, 0.08)))


def interruption(rng):
    # Muffled: a soft hit without a click ringing low modes, twice, the second softer.
    modes = [(230, 0.032, 1.0), (520, 0.010, 0.5), (1150, 0.004, 0.1)]
    one = knock(rng, modes, 0.15, excite=0.004, soften=1200)
    return mix(0.27, (1.0, 0.0, one), (0.7, 0.13, one))


def write(path, samples):
    frames = b"".join(struct.pack("<h", round(s * 32767)) for s in samples)
    with wave.open(str(path), "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes(frames)


def main():
    if len(sys.argv) > 2:
        sys.exit("usage: tools/make_sounds.py [<out-dir>]")
    root = Path(__file__).resolve().parent.parent
    out_dir = Path(sys.argv[1]) if len(sys.argv) == 2 else root / "app" / "resources" / "sounds"
    out_dir.mkdir(parents=True, exist_ok=True)
    rng = random.Random(20261104)
    for name, make in (("move", move), ("capture", capture), ("check", check),
                       ("wrong", wrong), ("interruption", interruption)):
        samples = finish(make(rng))
        write(out_dir / f"{name}.wav", samples)
        print(f"{name}.wav: {len(samples) / RATE * 1000:.0f} ms")


if __name__ == "__main__":
    main()
