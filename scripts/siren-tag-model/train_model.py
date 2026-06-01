"""Train a small multilabel classifier on the synthetic (or real) dataset.

Uses a Random Forest from scikit-learn — handles non-linear feature
interactions out of the box, doesn't need feature scaling, and emits to
clean C code via `m2cgen`. We use `MultiOutputClassifier` to support
multi-label tagging (a "click train at 120 BPM" should ideally be tagged
both "percussion" and "one-shot" and "loop" simultaneously).

Usage:
    python train_model.py --csv build/synthetic_dataset.csv
"""
from __future__ import annotations

import csv
import sys
from pathlib import Path

import numpy as np
from sklearn.ensemble import RandomForestClassifier
from sklearn.metrics import classification_report, hamming_loss
from sklearn.model_selection import train_test_split
from sklearn.multioutput import MultiOutputClassifier

from emit_cpp import emit_cpp
from feature_config import CLASS_NAMES, NUM_CLASSES, NUM_FEATURES


def load_csv(csv_path: Path) -> tuple[np.ndarray, np.ndarray, list[str]]:
    """Returns (X, Y, label_names). X is (N, NUM_FEATURES) float32. Y is (N, NUM_CLASSES) int in {0, 1}."""
    X, Y, paths = [], [], []
    with open(csv_path) as f:
        reader = csv.reader(f)
        header = next(reader)
        for row in reader:
            if not row:
                continue
            paths.append(row[0])
            X.append([float(v) for v in row[2:2 + NUM_FEATURES]])
            Y.append(row[1])
    X = np.asarray(X, dtype=np.float32)
    # One-vs-rest multilabel
    Y_idx = np.zeros((len(Y), NUM_CLASSES), dtype=np.int32)
    for i, label in enumerate(Y):
        if label in CLASS_NAMES:
            Y_idx[i, CLASS_NAMES.index(label)] = 1
        else:
            print(f"  warn: unknown label {label!r} in row {i}", file=sys.stderr)
    return X, Y_idx, paths


def augment_dataset(X: np.ndarray, Y: np.ndarray, n_aug: int = 3, seed: int = 123) -> tuple[np.ndarray, np.ndarray]:
    """Light augmentation: add small Gaussian noise to features. Keeps the
    feature-space alignment with the C++ runtime, since the C++ code also
    produces noisy estimates of the same features."""
    rng = np.random.default_rng(seed)
    Xs = [X]
    Ys = [Y]
    for _ in range(n_aug):
        noise = rng.normal(0.0, 0.03, size=X.shape).astype(np.float32)
        Xs.append(np.clip(X + noise, 0.0, 1.0))
        Ys.append(Y)
    return np.concatenate(Xs, axis=0), np.concatenate(Ys, axis=0)


def main() -> int:
    from argparse import ArgumentParser
    p = ArgumentParser(description=__doc__)
    p.add_argument("--csv", type=Path, required=True, help="Path to a CSV produced by generate_synthetic_dataset.py or your own loader.")
    p.add_argument("--out", type=Path, default=Path(__file__).parent / "build" / "SirenTagClassifier.generated.hpp",
                   help="Where to write the generated C++ header fragment.")
    p.add_argument("--n-estimators", type=int, default=24)
    p.add_argument("--max-depth", type=int, default=8)
    p.add_argument("--augment", type=int, default=3, help="Number of augmented copies per sample (0 = no augmentation).")
    p.add_argument("--seed", type=int, default=42)
    args = p.parse_args()

    print(f"Loading {args.csv} ...")
    X, Y, _ = load_csv(args.csv)
    print(f"  X: {X.shape}  Y: {Y.shape}  positive-rate: {Y.mean():.3f}")

    if args.augment > 0:
        X, Y = augment_dataset(X, Y, n_aug=args.augment, seed=args.seed)
        print(f"  after augmentation: X: {X.shape}  Y: {Y.shape}")

    X_tr, X_te, Y_tr, Y_te = train_test_split(X, Y, test_size=0.25, random_state=args.seed)

    print(f"\nTraining RandomForest (n_estimators={args.n_estimators}, max_depth={args.max_depth}) ...")
    base = RandomForestClassifier(
        n_estimators=args.n_estimators,
        max_depth=args.max_depth,
        min_samples_leaf=2,
        random_state=args.seed,
        n_jobs=-1,
    )
    model = MultiOutputClassifier(base, n_jobs=-1)
    model.fit(X_tr, Y_tr)
    print("  done.")

    Y_pred = model.predict(X_te)
    print("\n── Test-set metrics ───────────────────────────────────────────")
    print(f"  hamming loss: {hamming_loss(Y_te, Y_pred):.3f}  (lower is better)")
    print("  per-class report:")
    print(classification_report(Y_te, Y_pred, target_names=CLASS_NAMES, zero_division=0))

    # Smoke-test: run on a few training samples and print top-3
    print("── Smoke test on 5 training samples ────────────────────────────")
    sample_X = X_tr[:5]
    sample_scores = model.predict_proba(sample_X)
    for i in range(len(sample_X)):
        # Each per-label classifier returns (n_samples, 2) probas for [class 0, class 1].
        # If a class was missing from the training set entirely (e.g. you supplied
        # only 6 of the 15 classes), MultiOutputClassifier returns a 1-column
        # array for that class — guard against the IndexError that would otherwise
        # kill the smoke test.
        probs = np.array([
            sample_scores[c][i, 1] if sample_scores[c].shape[1] == 2 else 0.0
            for c in range(NUM_CLASSES)
        ])
        top3 = probs.argsort()[::-1][:3]
        print(f"  sample {i}: features={sample_X[i].round(3).tolist()}")
        for j in top3:
            print(f"    {CLASS_NAMES[j]:12s}  score={probs[j]:.3f}")

    # Emit C++
    args.out.parent.mkdir(parents=True, exist_ok=True)
    emit_cpp(model, args.out)
    print(f"\nWrote generated C++ to {args.out}")
    print("Paste its contents into the marked region of src/modules/Siren/SirenTagClassifier.hpp, then rebuild.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
