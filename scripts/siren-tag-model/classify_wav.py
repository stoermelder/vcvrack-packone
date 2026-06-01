"""Classify a single WAV (or any soundfile-supported) audio file.

Runs the same feature extractor used during training and prints the
top-K predicted classes with their raw model scores. Use this as a quick
sanity check after retraining: does the model still recognize obvious
"percussion" or "drone" or "vocal" inputs?

Usage:
    python classify_wav.py path/to/some.wav
    python classify_wav.py path/to/some.wav --top-k 5
    python classify_wav.py path/to/some.wav --csv build/synthetic_dataset.csv
    python classify_wav.py path/to/some.wav --no-model   # just dump the 6 features

If the trained model is not available (e.g. you ran this before
`train_model.py`), the script falls back to a uniform-random score per
class so you still see the top-3 — useful for verifying the feature
extractor in isolation.

Exit code 0 on success, non-zero on bad input.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np
import soundfile as sf

from feature_config import CLASS_NAMES, MODEL_VERSION, NUM_CLASSES, NUM_FEATURES
from features import extract_features


def _load_mono(path: Path) -> tuple[np.ndarray, int]:
    """Load an audio file as mono float32 in [-1, 1].

    soundfile handles wav / flac / ogg / mp3 etc. via libsndfile.
    Multi-channel input is averaged to mono.
    """
    data, sr = sf.read(str(path), always_2d=False, dtype="float32")
    if data.ndim == 2:
        data = data.mean(axis=1).astype(np.float32)
    if data.size == 0:
        raise ValueError(f"file is empty: {path}")
    return data, int(sr)


def _format_features(features: np.ndarray) -> str:
    parts = [f"{name}={value:.3f}" for name, value in zip(_feature_names(), features)]
    return "[" + ", ".join(parts) + "]"


def _feature_names() -> list[str]:
    # Imported lazily to keep this script's --help fast
    from feature_config import FEATURE_NAMES
    return FEATURE_NAMES


def _maybe_load_model(csv_path: Path | None):
    """Load the model that `train_model.py` would have written.

    Re-uses the same training pipeline so the scores here match what
    gets emitted to C++. Returns (model, source_label) — `model` is
    either a fitted sklearn estimator or None; `source_label` is a
    human-readable string for the output.
    """
    if csv_path is None:
        return None, "no model (uniform random scores)"
    try:
        from train_model import load_csv
        from sklearn.ensemble import RandomForestClassifier
        from sklearn.multioutput import MultiOutputClassifier
    except ImportError as e:
        print(f"  warn: cannot import training pipeline ({e}); using uniform scores", file=sys.stderr)
        return None, "no model (uniform random scores)"

    if not csv_path.exists():
        print(f"  warn: csv not found at {csv_path}; using uniform scores", file=sys.stderr)
        return None, "no model (uniform random scores)"

    X, Y, _ = load_csv(csv_path)
    base = RandomForestClassifier(
        n_estimators=24, max_depth=8, min_samples_leaf=2,
        random_state=42, n_jobs=-1,
    )
    model = MultiOutputClassifier(base, n_jobs=-1)
    model.fit(X, Y)
    return model, f"RandomForest from {csv_path.name}"


def _score_with_model(model, features: np.ndarray) -> np.ndarray:
    """Return (NUM_CLASSES,) array of per-class scores in [0, 1]."""
    if model is None:
        return np.full(NUM_CLASSES, 1.0 / NUM_CLASSES, dtype=np.float32)
    # model.predict_proba returns a list of (n_samples, 2) arrays, one per
    # output class — take column 1 (probability of positive class).
    probas = model.predict_proba(features.reshape(1, -1))
    return np.array([p[0, 1] for p in probas], dtype=np.float32)


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("wav", type=Path, help="Path to a wav/flac/mp3/ogg file.")
    p.add_argument("--top-k", type=int, default=3, help="How many top classes to print (default: 3).")
    p.add_argument("--csv", type=Path, default=Path(__file__).parent / "build" / "synthetic_dataset.csv",
                   help="Path to the labeled CSV. If present, the script also trains a RandomForest and scores with it. "
                        "Pass an empty string to skip training entirely.")
    p.add_argument("--no-model", action="store_true",
                   help="Skip training. Only print the 6 features. Equivalent to --csv ''.")
    p.add_argument("--max-seconds", type=float, default=30.0,
                   help="Trim input audio to at most this many seconds (default: 30). 0 = no trim.")
    args = p.parse_args()

    if not args.wav.exists():
        print(f"error: file not found: {args.wav}", file=sys.stderr)
        return 2

    csv_arg = None if args.no_model else (args.csv if str(args.csv) != "" else None)

    print(f"── classify_wav ────────────────────────────────────────────────")
    print(f"  file        : {args.wav}")
    print(f"  model ver   : {MODEL_VERSION}")
    print(f"  top-k       : {args.top_k}")

    try:
        audio, sr = _load_mono(args.wav)
    except Exception as e:
        print(f"error: failed to decode audio ({e})", file=sys.stderr)
        return 2

    if args.max_seconds and args.max_seconds > 0:
        max_samples = int(args.max_seconds * sr)
        if audio.size > max_samples:
            audio = audio[:max_samples]

    duration_s = audio.size / float(sr) if sr else 0.0
    print(f"  decoded     : {audio.size} samples @ {sr} Hz  ({duration_s:.2f} s, mono)")

    features = extract_features(audio, sr)
    print(f"  features    : {_format_features(features)}")

    if csv_arg is None:
        print(f"  model       : (skipped — --no-model or empty --csv)")
        print()
        print("  No scores to print (model skipped). Re-run without --no-model to see predictions.")
        return 0

    print(f"  training    : ", end="", flush=True)
    model, source_label = _maybe_load_model(csv_arg)
    print(source_label)

    if model is not None:
        scores = _score_with_model(model, features)
    else:
        scores = np.full(NUM_CLASSES, 1.0 / NUM_CLASSES, dtype=np.float32)

    order = scores.argsort()[::-1]
    print()
    print(f"  top-{args.top_k} predictions:")
    for rank, idx in enumerate(order[:args.top_k], start=1):
        bar_len = int(round(float(scores[idx]) * 20))
        bar = "█" * bar_len + "░" * (20 - bar_len)
        print(f"    {rank}. {CLASS_NAMES[idx]:12s}  {float(scores[idx]):.3f}  {bar}")

    if len(order) > args.top_k:
        print()
        print("  (rest of classes:)")
        for idx in order[args.top_k:]:
            print(f"      {CLASS_NAMES[idx]:12s}  {float(scores[idx]):.3f}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
