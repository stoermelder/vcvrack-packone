"""Contracts that the C++ runtime and the Python training pipeline share.

This file is the **Python-side** contract. The C++-side contract is the
`TagClassifier::extractFeatures()` function in
`src/modules/Siren/SirenTagClassifier.hpp`. They MUST stay in sync.

The class names are NOT defined here any more — they come from
`src/modules/Siren/TagManifest.json` via `tag_manifest.py`, which is the
single source of truth for both the C++ plugin and the Python pipeline.
"""
from __future__ import annotations

from tag_manifest import CLASS_NAMES, NUM_CLASSES, TAGS  # noqa: F401  (re-exported)

# Versioning for diagnostics + future cache invalidation. Bump by hand
# whenever `run.sh` produces a new model body.
MODEL_VERSION = 1

# Order matters: this is the array layout the C++ runtime passes to score().
FEATURE_NAMES: list[str] = [
    "spectral_centroid",  # brightness, mean freq weighted by magnitude, in [0, 1] of Nyquist
    "spectral_rolloff85", # freq below which 85% of energy lies, in [0, 1] of Nyquist
    "zero_crossing_rate", # fraction of sign changes per frame, averaged, in [0, 1]
    "rms",                # root-mean-square, normalized to peak ~= 1
    "onset_density",      # spectral-flux peaks per second, normalized to 30/sec == 1
    "low_band_ratio",     # energy below 250 Hz / total energy, in [0, 1]
]

assert len(FEATURE_NAMES) == 6, "The Siren runtime expects exactly 6 features"

NUM_FEATURES = len(FEATURE_NAMES)
