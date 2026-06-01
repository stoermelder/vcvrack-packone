"""Generate a synthetic labeled dataset of audio clips + per-class feature vectors.

The goal is to have *something* to train on before real labeled data exists.
Each "class" is a parameterized synthetic generator whose output strongly
exhibits the audio features we care about (e.g. a sustained 200 Hz sine wave
should be tagged "drone + bass + tonal"; a 120 BPM click train should be
tagged "percussion + one-shot").

The output is a CSV at `build/synthetic_dataset.csv` with columns:
    path, label, feature_0, feature_1, ..., feature_5
plus the audio clips themselves in `build/synthetic_audio/`.

You can swap this out for a real dataset (Freesound, your own sample library)
by writing `load_real_dataset()` in this file and pointing the trainer at it.
"""
from __future__ import annotations

import csv
import os
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable

import numpy as np

from features import extract_features_batch, find_cpp_extractor  # raises if not built

SR = 22050  # input sample rate for generated audio
DURATION = 2.0  # seconds per clip


@dataclass
class Generator:
    name: str
    fn: Callable[[int, np.random.Generator], np.ndarray]
    labels: list[str]


def _sine(freq: float, dur: float, sr: int, amp: float = 0.6) -> np.ndarray:
    t = np.arange(int(sr * dur)) / sr
    return amp * np.sin(2 * np.pi * freq * t).astype(np.float32)


def _noise(n: int, rng: np.random.Generator, color: str = "white") -> np.ndarray:
    if color == "white":
        return rng.standard_normal(n).astype(np.float32) * 0.4
    if color == "pink":
        # Voss-McCartney approximation
        rows = 16
        buf = rng.standard_normal((rows, n)).astype(np.float32)
        weights = 2.0 ** -np.arange(rows, dtype=np.float32)
        s = (weights[:, None] * buf).sum(axis=0)
        return (s / np.max(np.abs(s)) * 0.4).astype(np.float32)
    raise ValueError(color)


def _click_train(sr: int, bpm: float, dur: float, rng: np.random.Generator) -> np.ndarray:
    out = np.zeros(int(sr * dur), dtype=np.float32)
    interval = 60.0 / bpm
    pulse_len = max(64, sr // 100)
    bass_freq = 60.0
    t = 0.0
    while t < dur:
        c0 = int(t * sr)
        for i in range(pulse_len):
            idx = c0 + i
            if idx >= out.size:
                break
            phase = i / pulse_len
            env = (1.0 - phase) * 0.5 * (1.0 - np.cos(2 * np.pi * phase))
            bass = np.cos(2 * np.pi * bass_freq * idx / sr)
            amp = 0.9 * env * bass
            out[idx] += amp
        t += interval
    out += 0.005 * _noise(out.size, rng, "white")
    return out


def _tonal(sr: int, dur: float, freq: float, rng: np.random.Generator) -> np.ndarray:
    """Sustained pitched tone with a slow LFO on amplitude."""
    t = np.arange(int(sr * dur)) / sr
    base = 0.5 * np.sin(2 * np.pi * freq * t)
    # Add a couple of harmonics for "tonal" character
    base += 0.2 * np.sin(2 * np.pi * freq * 2 * t)
    base += 0.1 * np.sin(2 * np.pi * freq * 3 * t)
    lfo = 0.5 + 0.5 * np.sin(2 * np.pi * 0.5 * t)
    return (base * lfo).astype(np.float32)


def _bright_texture(sr: int, dur: float, rng: np.random.Generator) -> np.ndarray:
    """High-pass-filtered white noise — bright / texture / fx."""
    n = int(sr * dur)
    raw = _noise(n, rng, "white")
    # crude high-pass: subtract a low-pass
    from numpy.fft import rfft, irfft
    spec = rfft(raw)
    freqs = np.linspace(0, sr / 2, spec.size)
    spec[freqs < 2000] *= 0.1
    spec[freqs > 8000] *= 1.5
    return irfft(spec, n=n).astype(np.float32) * 0.6


# ── The 13 generators, one per CLASS_NAME in feature_config.py ────────────

def gen_drone(rng):
    return _sine(rng.uniform(60, 220), DURATION, SR)


def gen_percussion(rng):
    return _click_train(SR, rng.uniform(80, 160), DURATION, rng)


def gen_loop(rng):
    # 4-bar 120 BPM click train = 8 seconds, truncate to 2
    return _click_train(SR, 120.0, DURATION, rng)


def gen_one_shot(rng):
    # A single transient + short noise tail
    out = np.zeros(int(SR * DURATION), dtype=np.float32)
    start = int(0.1 * SR)
    pulse_len = int(0.05 * SR)
    for i in range(pulse_len):
        env = (1.0 - i / pulse_len) * 0.5 * (1.0 - np.cos(2 * np.pi * i / pulse_len))
        out[start + i] = 0.9 * env * np.cos(2 * np.pi * 60.0 * (start + i) / SR)
    out[start + pulse_len:] += 0.02 * _noise(out.size - start - pulse_len, rng, "white")
    return out


def gen_vocal(rng):
    # Formant-ish: fundamental + two formants, slow vibrato
    t = np.arange(int(SR * DURATION)) / SR
    f0 = rng.uniform(150, 300)
    formant = (
        0.5 * np.sin(2 * np.pi * f0 * t)
        + 0.3 * np.sin(2 * np.pi * 800 * t)
        + 0.2 * np.sin(2 * np.pi * 1200 * t)
    )
    vibrato = 0.5 + 0.5 * np.sin(2 * np.pi * 5 * t)
    return (formant * vibrato * 0.5).astype(np.float32)


def gen_field(rng):
    # Pink noise, low-passed
    n = int(SR * DURATION)
    raw = _noise(n, rng, "pink")
    from numpy.fft import rfft, irfft
    spec = rfft(raw)
    freqs = np.linspace(0, SR / 2, spec.size)
    spec[freqs > 2000] *= 0.2
    return irfft(spec, n=n).astype(np.float32) * 0.7


def gen_texture(rng):
    # Slow modulating filtered noise — texture-y
    n = int(SR * DURATION)
    raw = _noise(n, rng, "pink")
    t = np.arange(n) / SR
    lfo = 0.5 + 0.5 * np.sin(2 * np.pi * 0.3 * t)
    return (raw * lfo * 0.6).astype(np.float32)


def gen_bass(rng):
    # Sub-bass sine, 30–80 Hz
    return _sine(rng.uniform(30, 80), DURATION, SR, amp=0.8)


def gen_noise(rng):
    # Broadband noise without a tonal component — wind, hiss, static.
    n = int(SR * DURATION)
    color = rng.choice(["white", "pink"])
    return _noise(n, rng, color) * 0.5


def gen_pad(rng):
    # Slow sustained chord: 3 detuned sines + soft noise. LFO on amplitude.
    t = np.arange(int(SR * DURATION)) / SR
    root = rng.uniform(110, 220)  # A2 to A3
    intervals = [1.0, 1.005, 1.498, 1.5, 2.0]  # octave pair + fifth
    sig = np.zeros_like(t)
    for ratio in intervals:
        sig += (1.0 / len(intervals)) * np.sin(2 * np.pi * root * ratio * t)
    sig += 0.05 * _noise(sig.size, rng, "pink")
    lfo = 0.7 + 0.3 * np.sin(2 * np.pi * 0.3 * t)
    return (sig * lfo * 0.6).astype(np.float32)


def gen_lead(rng):
    # Bright mid/high-register melody: a short sequence of square-like tones
    t = np.arange(int(SR * DURATION)) / SR
    notes = [rng.uniform(400, 1200) for _ in range(4)]
    note_dur = DURATION / len(notes)
    out = np.zeros_like(t)
    for i, freq in enumerate(notes):
        t0 = int(i * note_dur * SR)
        t1 = int((i + 1) * note_dur * SR)
        seg = t[t0:t1] - t[t0]
        env = np.exp(-3 * seg / note_dur)
        out[t0:t1] += 0.3 * env * np.sign(np.sin(2 * np.pi * freq * seg))
    return out.astype(np.float32)


def gen_stab(rng):
    # Short pitched chord hit, ~150 ms attack + decay
    n = int(SR * DURATION)
    out = np.zeros(n, dtype=np.float32)
    t = np.arange(n) / SR
    # 3–4 chord notes
    root = rng.uniform(220, 440)
    chord = [root * r for r in (1.0, 1.26, 1.5, 2.0)[:rng.integers(3, 5)]]
    for f in chord:
        out += 0.15 * np.sin(2 * np.pi * f * t)
    env = np.exp(-15 * t)
    out *= env
    return out.astype(np.float32)


def gen_bright(rng):
    # High-pitched bright sine + sparkly noise
    t = np.arange(int(SR * DURATION)) / SR
    base = 0.3 * np.sin(2 * np.pi * rng.uniform(2000, 5000) * t)
    base += 0.1 * _noise(base.size, rng, "white")
    return base.astype(np.float32)


def gen_dark(rng):
    # Low-passed noise, no high content
    n = int(SR * DURATION)
    raw = _noise(n, rng, "pink")
    from numpy.fft import rfft, irfft
    spec = rfft(raw)
    freqs = np.linspace(0, SR / 2, spec.size)
    spec[freqs > 800] *= 0.05
    return irfft(spec, n=n).astype(np.float32) * 0.6


def gen_tonal(rng):
    return _tonal(SR, DURATION, rng.uniform(200, 800), rng)


GENERATORS: dict[str, Callable[[np.random.Generator], np.ndarray]] = {
    "Bass":       gen_bass,
    "Bright":     gen_bright,
    "Dark":       gen_dark,
    "Drone":      gen_drone,
    "Field":      gen_field,
    "Lead":       gen_lead,
    "Loop":       gen_loop,
    "Noise":      gen_noise,
    "One-Shot":   gen_one_shot,
    "Pad":        gen_pad,
    "Percussion": gen_percussion,
    "Stab":       gen_stab,
    "Texture":    gen_texture,
    "Tonal":      gen_tonal,
    "Vocal":      gen_vocal,
}


def generate(out_dir: Path, n_per_class: int, seed: int = 42) -> Path:
    """Generate `n_per_class` clips per generator. Returns path to the CSV."""
    from feature_config import CLASS_NAMES, NUM_FEATURES
    import soundfile as sf

    audio_dir = out_dir / "synthetic_audio"
    audio_dir.mkdir(parents=True, exist_ok=True)
    csv_path = out_dir / "synthetic_dataset.csv"

    rng = np.random.default_rng(seed)

    # Phase 1: generate audio and write WAV files.
    # WAVs are written before feature extraction so the C++ extractor can read them.
    pending: list[tuple[Path, str]] = []  # (wav_path, label)
    for label in CLASS_NAMES:
        if label not in GENERATORS:
            print(f"  skip: no generator for class {label!r}", file=sys.stderr)
            continue
        gen_fn = GENERATORS[label]
        for i in range(n_per_class):
            audio = gen_fn(rng)
            wav_path = audio_dir / f"{label}_{i:04d}.wav"
            sf.write(wav_path, audio, SR)
            pending.append((wav_path, label))

    print(f"  Wrote {len(pending)} audio clips to {audio_dir}")

    # Phase 2: extract features via C++ binary.
    binary = find_cpp_extractor()
    print(f"  Using C++ extractor: {binary}")
    features_by_path = extract_features_batch([str(p) for p, _ in pending], binary)

    # Phase 3: assemble CSV rows.
    rows: list[list] = []
    for wav_path, label in pending:
        features = features_by_path.get(str(wav_path))
        if features is None:
            print(f"  skip: extraction failed for {wav_path.name}", file=sys.stderr)
            continue
        rows.append([str(wav_path.relative_to(out_dir)), label, *features.tolist()])

    with open(csv_path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["path", "label", *[f"f{i}" for i in range(NUM_FEATURES)]])
        w.writerows(rows)

    print(f"\nWrote {len(rows)} rows to {csv_path}")
    return csv_path


def main() -> int:
    from argparse import ArgumentParser
    p = ArgumentParser(description=__doc__)
    p.add_argument("--out", type=Path, default=Path(__file__).parent / "build")
    p.add_argument("--n-per-class", type=int, default=80)
    p.add_argument("--seed", type=int, default=42)
    args = p.parse_args()
    generate(args.out, args.n_per_class, args.seed)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
