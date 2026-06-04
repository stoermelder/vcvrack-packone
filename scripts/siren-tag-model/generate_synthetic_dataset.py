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


# ── Generators — one per CLASS_NAME in SirenTags.json ─────────────────────

def gen_acoustic(rng):
    # Acoustic instrument: natural harmonics with exponential decay (guitar/piano-like)
    n = int(SR * DURATION)
    t = np.arange(n) / SR
    freq = rng.uniform(200, 600)
    sig = np.zeros(n, dtype=np.float64)
    for k, amp in enumerate([0.5, 0.3, 0.15, 0.08, 0.04], 1):
        sig += amp * np.exp(-3.0 * k * t) * np.sin(2 * np.pi * freq * k * t)
    attack = int(0.01 * SR)
    sig[:attack] += 0.2 * _noise(attack, rng, "white")
    return np.clip(sig * 0.8, -1, 1).astype(np.float32)


def gen_atmospheric(rng):
    # Slowly evolving pad: detuned sines with dual LFOs and slight noise
    t = np.arange(int(SR * DURATION)) / SR
    root = rng.uniform(100, 300)
    sig = np.zeros_like(t)
    for ratio, phase_offset in zip([1.0, 1.5, 2.0, 3.0], [0.0, 1.1, 2.3, 0.7]):
        sig += (0.2 / ratio) * np.sin(2 * np.pi * root * ratio * t + phase_offset)
    sig *= (0.5 + 0.5 * np.sin(2 * np.pi * 0.2 * t)) * (0.5 + 0.5 * np.sin(2 * np.pi * 0.07 * t))
    sig += 0.03 * _noise(len(t), rng, "pink")
    return (sig * 0.7).astype(np.float32)


def gen_bass(rng):
    return _sine(rng.uniform(30, 80), DURATION, SR, amp=0.8)


def gen_clap(rng):
    # Layered noise bursts with mid-frequency character
    from numpy.fft import rfft, irfft
    n = int(SR * DURATION)
    t = np.arange(n) / SR
    raw = _noise(n, rng, "white")
    spec = rfft(raw)
    freqs = np.linspace(0, SR / 2, spec.size)
    spec[freqs < 800] *= 0.05
    sig = irfft(spec, n=n).astype(np.float32)
    env = np.zeros(n, dtype=np.float32)
    for delay in [0.0, 0.006, 0.012]:
        mask = t >= delay
        env[mask] += np.exp(-60.0 * (t[mask] - delay))
    return np.clip(sig * env * 0.8, -1, 1).astype(np.float32)


def gen_cymbal(rng):
    # High-frequency metallic noise with longer decay
    from numpy.fft import rfft, irfft
    n = int(SR * DURATION)
    raw = _noise(n, rng, "white")
    spec = rfft(raw)
    freqs = np.linspace(0, SR / 2, spec.size)
    spec[freqs < 5000] *= 0.02
    sig = irfft(spec, n=n).astype(np.float32)
    t = np.arange(n) / SR
    return np.clip(sig * np.exp(-4.0 * t) * 0.8, -1, 1).astype(np.float32)


def gen_drone(rng):
    return _sine(rng.uniform(60, 220), DURATION, SR)


def gen_drums(rng):
    # Full drum pattern: kick on 1&3, snare on 2&4, hihat every 8th note
    n = int(SR * DURATION)
    out = np.zeros(n, dtype=np.float32)
    bpm = rng.uniform(100, 140)
    step = int(60.0 / bpm / 2 * SR)  # 8th note
    for i in range(int(n / step) + 1):
        t0 = i * step
        if t0 >= n:
            break
        # Kick on beats 1 and 3 (steps 0 and 4)
        if i % 8 in (0, 4):
            k = gen_kick(rng)
            end = min(t0 + len(k), n)
            out[t0:end] += k[:end - t0] * 0.8
        # Snare on beats 2 and 4 (steps 2 and 6)
        if i % 8 in (2, 6):
            s = gen_snare(rng)
            end = min(t0 + len(s), n)
            out[t0:end] += s[:end - t0] * 0.7
        # HiHat on every 8th note
        h = gen_hihat(rng)
        end = min(t0 + len(h), n)
        out[t0:end] += h[:end - t0] * 0.4
    return np.clip(out, -1, 1).astype(np.float32)


def gen_fx(rng):
    # Sweep, riser or impact with LFO amplitude shaping
    n = int(SR * DURATION)
    t = np.arange(n) / SR
    sweep_type = int(rng.integers(0, 3))
    if sweep_type == 0:
        freq = np.exp(np.linspace(np.log(80), np.log(4000), n))
    elif sweep_type == 1:
        freq = np.exp(np.linspace(np.log(4000), np.log(80), n))
    else:
        freq = 500 + 400 * np.sin(2 * np.pi * 0.5 * t)
    phase = np.cumsum(2 * np.pi * freq / SR)
    sig = 0.5 * np.sin(phase) + 0.2 * _noise(n, rng, "white")
    sig *= 0.5 + 0.5 * np.sin(2 * np.pi * 0.3 * t)
    return np.clip(sig * 0.8, -1, 1).astype(np.float32)


def gen_glitch(rng):
    # Stuttering random noise bursts with silence between them
    n = int(SR * DURATION)
    out = np.zeros(n, dtype=np.float32)
    for _ in range(int(rng.integers(6, 20))):
        pos = int(rng.integers(0, n - 1000))
        length = int(rng.integers(50, 3000))
        burst = _noise(length, rng, "white") * float(rng.uniform(0.3, 0.9))
        end = min(pos + length, n)
        out[pos:end] += burst[:end - pos].astype(np.float32)
    return np.clip(out, -1, 1).astype(np.float32)


def gen_hihat(rng):
    # Short metallic high-frequency noise burst
    from numpy.fft import rfft, irfft
    n = int(SR * DURATION)
    raw = _noise(n, rng, "white")
    spec = rfft(raw)
    freqs = np.linspace(0, SR / 2, spec.size)
    spec[freqs < 3000] *= 0.03
    sig = irfft(spec, n=n).astype(np.float32)
    t = np.arange(n) / SR
    return np.clip(sig * np.exp(-80.0 * t) * 0.8, -1, 1).astype(np.float32)


def gen_kick(rng):
    # Low-frequency transient: exponential sine sweep from ~150 Hz to ~40 Hz
    n = int(SR * DURATION)
    t = np.arange(n) / SR
    freq = 150.0 * np.exp(-25.0 * t) + 40.0
    phase = np.cumsum(2 * np.pi * freq / SR)
    env = np.exp(-18.0 * t)
    sig = env * np.sin(phase)
    click = int(0.004 * SR)
    sig[:click] += np.linspace(0.4, 0.0, click)
    return np.clip(sig * 0.9, -1, 1).astype(np.float32)


def gen_lead(rng):
    # Bright mid/high-register melody: sequence of square-like tones
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


def gen_loop(rng):
    return _click_train(SR, rng.uniform(100, 140), DURATION, rng)


def gen_nature(rng):
    # Environmental recording: pink noise, low-passed, slow amplitude variation
    from numpy.fft import rfft, irfft
    n = int(SR * DURATION)
    raw = _noise(n, rng, "pink")
    spec = rfft(raw)
    freqs = np.linspace(0, SR / 2, spec.size)
    spec[freqs > 2000] *= 0.15
    sig = irfft(spec, n=n).astype(np.float32)
    t = np.arange(n) / SR
    sig *= 0.7 + 0.3 * np.sin(2 * np.pi * 0.4 * t)
    return (sig * 0.7).astype(np.float32)


def gen_noise(rng):
    n = int(SR * DURATION)
    color = rng.choice(["white", "pink"])
    return _noise(n, rng, color) * 0.5


def gen_pad(rng):
    # Sustained harmonic bed: stacked chord tones (root + third + fifth) with
    # slight detune, slow attack, and a gentle LFO for warmth.
    # Distinguishing profile: high harmonic_ratio, low crest_factor,
    # temporal_centroid ~0.5+, low env_rms_variance, high temporal_entropy.
    t = np.arange(int(SR * DURATION)) / SR
    root = rng.uniform(80, 300)
    chord = [root, root * 1.26, root * 1.5, root * 2.0]  # root, M3, P5, octave
    sig = np.zeros(len(t), dtype=np.float64)
    for f in chord:
        detune = rng.uniform(-0.8, 0.8)
        amp = rng.uniform(0.18, 0.32)
        sig += amp       * np.sin(2 * np.pi * (f + detune) * t)
        sig += amp * 0.4 * np.sin(2 * np.pi * 2 * (f + detune) * t)
        sig += amp * 0.15 * np.sin(2 * np.pi * 3 * (f + detune) * t)
    # Slow attack — 0 → 1 over the first ~40 % of the clip.
    attack_n = int(0.4 * len(t))
    env = np.ones(len(t))
    env[:attack_n] = np.linspace(0.0, 1.0, attack_n)
    sig *= env
    # Gentle chorus LFO.
    lfo = rng.uniform(0.2, 1.2)
    sig *= 0.88 + 0.12 * np.sin(2 * np.pi * lfo * t)
    return np.clip(sig * 0.38, -1.0, 1.0).astype(np.float32)


def gen_snare(rng):
    # Mid-frequency transient with noise component
    from numpy.fft import rfft, irfft
    n = int(SR * DURATION)
    t = np.arange(n) / SR
    body = 0.5 * np.exp(-30.0 * t) * np.sin(2 * np.pi * 200 * t)
    raw = _noise(n, rng, "white")
    spec = rfft(raw)
    freqs = np.linspace(0, SR / 2, spec.size)
    spec[freqs < 500] *= 0.1
    noise_hp = irfft(spec, n=n).astype(np.float32)
    sig = body + 0.6 * noise_hp * np.exp(-25.0 * t)
    return np.clip(sig * 0.8, -1, 1).astype(np.float32)


def gen_vocal(rng):
    # Formant-ish: fundamental + two formants, slow vibrato
    t = np.arange(int(SR * DURATION)) / SR
    f0 = rng.uniform(150, 300)
    sig = (
        0.5 * np.sin(2 * np.pi * f0 * t)
        + 0.3 * np.sin(2 * np.pi * 800 * t)
        + 0.2 * np.sin(2 * np.pi * 1200 * t)
    )
    sig *= 0.5 + 0.5 * np.sin(2 * np.pi * 5 * t)
    return (sig * 0.5).astype(np.float32)


GENERATORS: dict[str, Callable[[np.random.Generator], np.ndarray]] = {
    "Acoustic":    gen_acoustic,
    "Atmospheric": gen_atmospheric,
    "Bass":        gen_bass,
    "Clap":        gen_clap,
    "Cymbal":      gen_cymbal,
    "Drone":       gen_drone,
    "Drums":       gen_drums,
    "FX":          gen_fx,
    "Glitch":      gen_glitch,
    "HiHat":       gen_hihat,
    "Kick":        gen_kick,
    "Lead":        gen_lead,
    "Loop":        gen_loop,
    "Nature":      gen_nature,
    "Noise":       gen_noise,
    "Pad":         gen_pad,
    "Snare":       gen_snare,
    "Vocal":       gen_vocal,
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
