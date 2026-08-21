"""Audio-domain augmentation for the tag-classifier training pipeline.

Unlike the feature-space jitter in `train_model.py` (which adds Gaussian noise
to the already-extracted 53-d vector), these transforms operate on the *waveform*
and are run through the real C++ `extractFeatures()`. They produce on-manifold
variation that actually occurs in the wild — the kind a RandomForest can learn
genuine invariance from — instead of an isotropic blob around each feature point.

Deliberate omissions:
  - **Gain / level** changes are NOT applied: `prepareMono()` peak-normalises the
    buffer before extraction, so a gain change is a no-op on the features. Adding
    it would waste augmentation budget on a transform the pipeline cancels out.
  - **Polarity flip** is omitted: every feature is magnitude/energy/autocorrelation
    based, so a sign flip barely moves the vector.

Pure numpy only — no scipy/librosa, no new dependencies. Filtering is done with
windowed-FIR convolution (`np.convolve`), which is fast (C-level) and good enough
for augmentation realism; we are not building a mastering EQ.
"""
from __future__ import annotations

from pathlib import Path
from typing import Optional, Tuple

import numpy as np


def load_mono(path: str | Path) -> Optional[Tuple[np.ndarray, int]]:
    """Load `path` as a mono float32 array. Returns (data, samplerate) or None."""
    try:
        import soundfile as sf
        data, sr = sf.read(str(path), dtype="float32", always_2d=True)
    except Exception:
        return None
    if data.shape[1] > 1:
        data = data.mean(axis=1)          # mix to mono (same as the C++ extractor)
    else:
        data = data[:, 0]
    return np.ascontiguousarray(data, dtype=np.float32), int(sr)


# ── Individual transforms ─────────────────────────────────────────────────────
# Each takes (data, sr, rng) and returns a new float32 array.

def add_noise(data: np.ndarray, sr: int, rng: np.random.Generator) -> np.ndarray:
    """Additive Gaussian noise at a random, high SNR (25–45 dB) — subtle hiss
    only, never enough to blur the class."""
    snr_db = rng.uniform(25.0, 45.0)
    sig_power = float(np.mean(data ** 2)) + 1e-12
    noise_power = sig_power / (10.0 ** (snr_db / 10.0))
    noise = rng.normal(0.0, np.sqrt(noise_power), size=data.shape).astype(np.float32)
    return (data + noise).astype(np.float32)


def speed_change(data: np.ndarray, sr: int, rng: np.random.Generator) -> np.ndarray:
    """Resample to change speed by ±10% — shifts pitch and tempo together.

    Implemented as linear-interpolation resampling at the same samplerate, so
    the decimation factor in the extractor is unchanged; only the content moves.
    Kept subtle (±4 %) so pitch/tempo move just enough to vary the sample.
    """
    factor = rng.uniform(0.96, 1.04)
    n = len(data)
    if n < 2:
        return data
    new_n = max(2, int(round(n / factor)))
    idx = np.linspace(0.0, n - 1, new_n)
    return np.interp(idx, np.arange(n), data).astype(np.float32)


def _lowpass(data: np.ndarray, k: int) -> np.ndarray:
    """Hann-windowed moving-average low-pass; larger k → lower cutoff."""
    if k < 2:
        return data
    kernel = np.hanning(k).astype(np.float32)
    kernel /= kernel.sum()
    return np.convolve(data, kernel, mode="same").astype(np.float32)


def eq_filter(data: np.ndarray, sr: int, rng: np.random.Generator) -> np.ndarray:
    """Random low-pass (roll off highs) or high-pass (thin out lows).

    Applied as a light wet/dry blend (mix 0.1–0.3) so a band is gently
    *tilted*, never removed — a hard low-pass that strips a hi-hat's defining
    highs (or a high-pass that guts a kick's lows) would change the clip's true
    class, turning a training sample into a mislabelled one.
    """
    mix = float(rng.uniform(0.1, 0.3))
    if rng.random() < 0.5:
        filtered = _lowpass(data, int(rng.integers(3, 15)))            # roll off highs
    else:
        k = int(rng.integers(15, 80))
        filtered = (data - _lowpass(data, k)).astype(np.float32)       # thin out lows
    return ((1.0 - mix) * data + mix * filtered).astype(np.float32)


def reverb(data: np.ndarray, sr: int, rng: np.random.Generator) -> np.ndarray:
    """Convolve with a very short exponentially-decaying noise impulse response,
    blended in at a low wet level so it adds a hint of room, not a tail long
    enough to turn a one-shot into a sustained sound."""
    dur = rng.uniform(0.03, 0.10)
    length = max(2, int(dur * sr))
    decay = np.exp(-np.linspace(0.0, 6.0, length))
    ir = (rng.normal(0.0, 1.0, length) * decay).astype(np.float32)
    ir[0] = 1.0                                              # preserve the direct sound
    wet = np.convolve(data, ir)[: len(data)]
    mix = float(rng.uniform(0.1, 0.25))
    return ((1.0 - mix) * data + mix * wet).astype(np.float32)


def time_shift(data: np.ndarray, sr: int, rng: np.random.Generator) -> np.ndarray:
    """Delay the onset by up to 10% of the length, zero-padding the front.

    A non-wrapping shift (unlike a circular roll, which could wrap a one-shot's
    transient across the window boundary and split it in two). Exercises the
    temporal features (centroid, attack, tail/head) a little without materially
    moving a percussive one-shot's envelope shape.
    """
    n = len(data)
    if n < 2:
        return data
    s = int(rng.integers(1, max(2, int(0.1 * n) + 1)))
    out = np.zeros_like(data)
    out[s:] = data[: n - s]
    return out.astype(np.float32)


_TRANSFORMS = (add_noise, speed_change, eq_filter, reverb, time_shift)


def augment_waveform(
    data: np.ndarray,
    sr: int,
    rng: np.random.Generator,
    max_transforms: int = 1,
) -> np.ndarray:
    """Apply a random chain of 1..max_transforms transforms to `data`.

    Defaults to a single transform per variant: stacking several (e.g. reverb +
    time-shift + EQ) compounds the drift and can push a clip off its true class.
    One subtle transform keeps every variant clearly in-class.

    Falls back to the (unmodified) input if the result is empty or non-finite,
    so a pathological transform never produces a bad row. The output is left at
    natural level (only clipped if it exceeds ±1.0) — the extractor peak-
    normalises anyway, so we deliberately don't re-gain here.
    """
    src = np.asarray(data, dtype=np.float32)
    if src.size == 0:
        return src
    k = int(rng.integers(1, max_transforms + 1))
    chosen = rng.choice(len(_TRANSFORMS), size=k, replace=False)
    out = src.copy()
    for ci in chosen:
        out = _TRANSFORMS[int(ci)](out, sr, rng)
    if out.size == 0 or not np.all(np.isfinite(out)):
        return src
    peak = float(np.max(np.abs(out)))
    if peak > 1.0:
        out = (out / peak).astype(np.float32)
    return out
