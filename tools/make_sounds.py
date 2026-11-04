#!/usr/bin/env python3
# Writes the app's five sounds (docs/reference/app.md, **Sounds**) as WAV files, 16-bit
# 44.1 kHz mono, each under 300 ms, its peak at -12 dBFS:
#   move.wav          a wooden thunk: a short noise burst ringing a few resonances, over a damped
#                     low noise thud
#   capture.wav       the same, heavier: lower, longer and with more thud
#   check.wav         a short two-tone, a rising pair of soft bells
#   wrong.wav         a soft low tone gliding down
#   interruption.wav  a gentle knock, twice
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
    top = max(abs(s) for s in out) or 1.0
    return [s / top for s in out]


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


def thud(rng, length, tau, cutoff):
    """A damped low noise burst."""
    return low_pass(burst(length, tau, rng), cutoff)


def finish(signal, fade=0.012):
    """Fades the last fade seconds out and scales the peak to -12 dBFS."""
    count = round(fade * RATE)
    out = list(signal)
    for n in range(count):
        out[len(out) - count + n] *= 0.5 + 0.5 * math.cos(math.pi * (n + 1) / count)
    top = max(abs(s) for s in out)
    return [s * PEAK / top for s in out]


def move(rng):
    length = 0.12
    body = knock(rng, [(450, 0.012, 1.0), (1100, 0.006, 0.55), (2400, 0.003, 0.3),
                       (4200, 0.0015, 0.12)], length)
    return mix(length, (1.0, 0.0, body), (0.8, 0.0, thud(rng, length, 0.006, 350)))


def capture(rng):
    length = 0.16
    body = knock(rng, [(300, 0.024, 1.0), (760, 0.011, 0.6), (1700, 0.005, 0.35),
                       (3400, 0.002, 0.2)], length, excite=0.006)
    return mix(length, (1.0, 0.0, body), (1.2, 0.0, thud(rng, length, 0.014, 300)))


def check(_rng):
    bell = ((1, 1.0), (2, 0.15))
    return mix(0.19, (1.0, 0.0, tone(0.085, 740, 740, 0.004, 0.040, bell)),
               (1.0, 0.09, tone(0.10, 988, 988, 0.004, 0.045, bell)))


def wrong(_rng):
    return tone(0.28, 294, 247, 0.015, 0.11, ((1, 1.0), (2, 0.3), (3, 0.08)))


def interruption(rng):
    length = 0.26
    modes = [(420, 0.020, 1.0), (1100, 0.008, 0.4), (2400, 0.003, 0.15)]
    one = knock(rng, modes, 0.14, excite=0.003, soften=2500)
    return mix(length, (1.0, 0.0, one), (0.8, 0.11, one))


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
