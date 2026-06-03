"""Contracts that the C++ runtime and the Python training pipeline share.

This file is the **Python-side** contract. The C++-side contract is the
`TagClassifier::extractFeatures()` function in
`src/modules/Siren/SirenTagClassifier.hpp`. They MUST stay in sync.

The class names are NOT defined here any more — they come from
`res/data/SirenTags.json` via `tag_manifest.py`, which is the
single source of truth for both the C++ plugin and the Python pipeline.
"""
from __future__ import annotations

from tag_manifest import CLASS_NAMES, NUM_CLASSES, TAGS  # noqa: F401  (re-exported)

# Versioning for diagnostics + future cache invalidation. Bump by hand
# whenever `run.sh` produces a new model body.
MODEL_VERSION = 1

# Order matters: this is the array layout the C++ runtime passes to score().
FEATURE_NAMES: list[str] = [
    "spectral_centroid",    # brightness — mean freq weighted by power, in [0, 1] of Nyquist
    "spectral_rolloff85",   # freq below which 85 % of energy lies, in [0, 1] of Nyquist
    "zero_crossing_rate",   # fraction of sign changes per frame, averaged, in [0, 1]
    "rms",                  # root-mean-square, normalised to peak ~= 1
    "onset_density",        # spectral-flux peaks per second, normalised to 30/sec == 1
    "low_mid_ratio",        # energy in [80, 250) Hz / total energy, in [0, 1]
    "spectral_flatness",    # geometric / arithmetic mean of magnitude per frame, in [0, 1]
    "spectral_bandwidth",   # power-weighted std dev of freq around centroid, / Nyquist
    "high_band_ratio",      # energy above 2000 Hz / total energy, in [0, 1]
    "mean_spectral_flux",   # mean log-domain half-rectified flux per hop, normalised
    "crest_factor",         # peak / RMS (log-normalised): transients → high, sustained → low
    "harmonic_ratio",       # normalised autocorrelation peak: pitched → high, noise → low
    "spectral_crest",       # max(mag)/mean(mag)/N: single-tone → 1, flat noise → ~0
    "spectral_entropy",     # Shannon entropy of PSD normalised by log(N): noise → 1, tone → 0
    "spectral_slope",       # Pearson r(bin, mag) mapped [−1,1]→[0,1]: falling=0, rising=1
    "spectral_decrease",    # spectral decrease (low-freq bias) mapped to [0,1]
    "spectral_skewness",    # 3rd standardised moment of spectrum mapped [−3,3]→[0,1]
    "spectral_kurtosis",    # excess 4th moment (peakedness) mapped to [0,1]
    "mfcc_0",               # mel-frequency cepstral coefficient 0 (log energy)
    "mfcc_1",
    "mfcc_2",
    "mfcc_3",
    "mfcc_4",
    "mfcc_5",
    "mfcc_6",
    "mfcc_7",
    "mfcc_8",
    "mfcc_9",
    "mfcc_10",
    "mfcc_11",
    "mfcc_12",
    "temporal_centroid",  # time-weighted centre of mass of RMS envelope [0=front, 1=back]
    "tail_head_ratio",    # rms(last 20%) / (rms(first 20%) + rms(last 20%)): one-shot→0, loop→0.5
    "env_ac_peak",        # peak normalised autocorrelation of RMS envelope at 0.25–4 s lags: loop/drone→1, one-shot→0
    "attack_time",        # block of peak RMS / (N_BLOCKS-1): percussive→0, pad/drone→high
    "env_rms_variance",   # normalised std dev of RMS envelope blocks: sustained→0, rhythmic/one-shot→high
    "temporal_entropy",   # Shannon entropy of normalised RMS envelope: one-shot→0, drone/noise→1
    "sub_bass_ratio",     # energy in [0, 80) Hz / total: kick→high, most else→low
    "mid_band_ratio",     # energy in [250, 2000) Hz / total: snare/clap/vocal→high, kick/bass→low
    "flux_variance",      # std dev of per-hop spectral flux / MEAN_FLUX_NORM: glitch/drums→high, drone→low
    # MFCC deltas (40–52): mean absolute frame-to-frame difference per coefficient.
    # Static sounds (drone, pad) → near 0; melodic/rhythmic material → higher.
    "mfcc_delta_0",
    "mfcc_delta_1",
    "mfcc_delta_2",
    "mfcc_delta_3",
    "mfcc_delta_4",
    "mfcc_delta_5",
    "mfcc_delta_6",
    "mfcc_delta_7",
    "mfcc_delta_8",
    "mfcc_delta_9",
    "mfcc_delta_10",
    "mfcc_delta_11",
    "mfcc_delta_12",
]

assert len(FEATURE_NAMES) == 53, "The Siren runtime expects exactly 53 features"

NUM_FEATURES: int = len(FEATURE_NAMES)  # 40
