"""Audio feature extraction.

This is the Python twin of the C++ `extractFeatures()` in
`src/modules/Siren/SirenTagClassifier.hpp`. They MUST produce the same
6-element feature vector in the same order, otherwise training and inference
are in different feature spaces.

The algorithm mirrors the BPM detector's STFT pipeline (FFT size 512, hop
128, decimation to ~4410 Hz, Hann window) so that the Python pipeline and
the C++ pipeline are in lockstep.
"""
from __future__ import annotations

import numpy as np
from numpy.fft import rfft

# STFT parameters — MUST match SirenTagClassifier.hpp / SirenBpmDetector.hpp
FFT_SIZE = 512
HOP = 128
TARGET_SR = 4410  # decimated analysis rate
NYQUIST = TARGET_SR / 2.0  # feature [0, 1] = freq / NYQUIST

# Onset density normalization: 30 onsets/sec == 1.0
ONSET_DENSITY_NORM = 30.0

# Low-band cutoff
LOW_BAND_HZ = 250.0


def _hann(n: int) -> np.ndarray:
    return 0.5 - 0.5 * np.cos(2.0 * np.pi * np.arange(n) / (n - 1))


def _decimate(mono: np.ndarray, sr_in: int) -> tuple[np.ndarray, int]:
    """Decimate to TARGET_SR with simple integer step (matches the BPM detector)."""
    if sr_in == TARGET_SR:
        return mono, sr_in
    factor = max(1, int(round(sr_in / TARGET_SR)))
    return mono[::factor], sr_in // factor


def _stft_magnitudes(mono: np.ndarray) -> np.ndarray:
    """Return magnitude spectrogram. Shape: (num_frames, FFT_SIZE//2 + 1)."""
    window = _hann(FFT_SIZE)
    n = mono.shape[0]
    if n < FFT_SIZE:
        pad = np.zeros(FFT_SIZE - n, dtype=mono.dtype)
        mono = np.concatenate([mono, pad])
        n = FFT_SIZE
    num_frames = 1 + (n - FFT_SIZE) // HOP
    cols = []
    for i in range(num_frames):
        start = i * HOP
        frame = mono[start:start + FFT_SIZE] * window
        spec = rfft(frame, n=FFT_SIZE)
        cols.append(np.abs(spec))
    return np.stack(cols, axis=0)  # (T, F)


def extract_features(mono: np.ndarray, sr: int) -> np.ndarray:
    """Extract the 6 normalized features. Input is mono float in [-1, 1].

    Returns a 1-D numpy array of length 6 with values in [0, 1].
    """
    if mono.ndim != 1:
        raise ValueError("extract_features expects mono audio (1-D)")
    mono = mono.astype(np.float32, copy=False)
    if mono.size == 0:
        return np.zeros(6, dtype=np.float32)

    mono_dec, sr_dec = _decimate(mono, sr)
    mag = _stft_magnitudes(mono_dec)  # (T, F)
    if mag.shape[0] < 2:
        return np.full(6, 0.5, dtype=np.float32)

    # ── 1. spectral centroid ──────────────────────────────────────────────
    bin_freqs = np.linspace(0, NYQUIST, mag.shape[1])
    mag_sum_per_frame = mag.sum(axis=1) + 1e-12
    centroid_per_frame = (mag * bin_freqs[None, :]).sum(axis=1) / mag_sum_per_frame
    centroid = float(centroid_per_frame.mean()) / NYQUIST

    # ── 2. spectral rolloff 85% ──────────────────────────────────────────
    cum_energy = np.cumsum(mag, axis=1)
    total = cum_energy[:, -1:] + 1e-12
    rolloff_idx = (cum_energy >= 0.85 * total).argmax(axis=1)
    rolloff = float(bin_freqs[rolloff_idx].mean()) / NYQUIST

    # ── 3. zero-crossing rate ────────────────────────────────────────────
    signs = np.sign(mono_dec)
    signs[signs == 0] = 1
    zc = float(np.mean(signs[:-1] != signs[1:]))

    # ── 4. rms ───────────────────────────────────────────────────────────
    rms = float(np.sqrt(np.mean(mono_dec ** 2)))
    # Normalize so a peak-of-1 sine reads 0.707 → scale so 0.707 → 0.5
    rms_norm = min(1.0, rms / 0.5)

    # ── 5. onset density (spectral flux peaks per second) ────────────────
    flux = np.maximum(0.0, np.diff(mag, axis=0))
    flux_mean = flux.mean(axis=1)
    # Count peaks: local maxima above a small floor
    if flux_mean.size >= 3:
        padded = np.concatenate([[0.0], flux_mean, [0.0]])
        is_peak = (padded[1:-1] > padded[:-2]) & (padded[1:-1] > padded[2:])
        # Floor: median * 1.5 to ignore noise
        floor = max(1e-6, float(np.median(flux_mean)) * 1.5)
        is_peak = is_peak & (flux_mean > floor)
        n_peaks = int(is_peak.sum())
        seconds = mono_dec.size / float(sr_dec)
        density = n_peaks / max(seconds, 1e-3)
    else:
        density = 0.0
    onset_density = min(1.0, density / ONSET_DENSITY_NORM)

    # ── 6. low-band ratio (energy below 250 Hz / total) ──────────────────
    low_idx = int(np.searchsorted(bin_freqs, LOW_BAND_HZ, side="right"))
    low_idx = max(1, min(low_idx, mag.shape[1]))
    low_energy = (mag[:, :low_idx] ** 2).sum()
    total_energy = (mag ** 2).sum() + 1e-12
    low_band_ratio = float(low_energy / total_energy)

    out = np.array(
        [centroid, rolloff, zc, rms_norm, onset_density, low_band_ratio],
        dtype=np.float32,
    )
    return np.clip(out, 0.0, 1.0)
