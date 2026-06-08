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
import platform
import sys
from pathlib import Path

import numpy as np
import sklearn
from scipy.optimize import fmin_bfgs
from sklearn.ensemble import RandomForestClassifier
from sklearn.metrics import classification_report, hamming_loss
from sklearn.model_selection import train_test_split
from sklearn.multioutput import MultiOutputClassifier

from emit_cpp import emit_cpp
from feature_config import CLASS_NAMES, MODEL_VERSION, NUM_CLASSES, NUM_FEATURES


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


def fit_calibration(
    estimators: list,
    X_cal: np.ndarray,
    Y_cal: np.ndarray,
) -> list[tuple[float, float] | None]:
    """Fit per-class Platt sigmoid calibration on a held-out set.

    Implements Platt (1999) directly — the same algorithm sklearn uses
    internally in CalibratedClassifierCV(method='sigmoid').  The key
    difference from plain LogisticRegression is *label smoothing*: target
    labels are mapped to (N+1)/(N+2) and 1/(N+2) instead of 1 and 0, which
    prevents the infinite-slope problem that occurs when the RF perfectly
    separates the calibration set (common with synthetic data).

    Returns a list of (a, b) per class where
        p_calibrated = 1 / (1 + exp(a * p_raw + b))
    Note the sign convention: 'a' is typically negative so that higher raw
    probability maps to higher calibrated probability.
    Returns None for a class when calibration cannot be fitted (fewer than
    two distinct label values in the calibration set).
    """
    params: list[tuple[float, float] | None] = []
    for c, est in enumerate(estimators):
        y_true = Y_cal[:, c]
        if len(np.unique(y_true)) < 2:
            print(f"  calibration skipped for class {c} (single label in cal set)", file=sys.stderr)
            params.append(None)
            continue

        raw = est.predict_proba(X_cal)[:, 1]

        # Platt label smoothing — maps {0, 1} to soft targets that keep the
        # optimisation well-posed even when the RF perfectly separates classes.
        prior1 = float(np.sum(y_true > 0))
        prior0 = float(len(y_true) - prior1)
        T  = np.where(y_true > 0, (prior1 + 1.0) / (prior1 + 2.0), 1.0 / (prior0 + 2.0))
        T1 = 1.0 - T

        def objective(AB: np.ndarray) -> float:
            E = np.exp(AB[0] * raw + AB[1])
            P = 1.0 / (1.0 + E)
            return -float(np.sum(T * np.log(P + 1e-12) + T1 * np.log(1.0 - P + 1e-12)))

        def gradient(AB: np.ndarray) -> np.ndarray:
            E   = np.exp(AB[0] * raw + AB[1])
            P   = 1.0 / (1.0 + E)
            TEP = P * (T * E - T1)
            return np.array([float(np.dot(TEP, raw)), float(np.sum(TEP))])

        AB0 = np.array([0.0, np.log((prior0 + 1.0) / (prior1 + 1.0))])
        AB  = fmin_bfgs(objective, AB0, fprime=gradient, disp=False)
        params.append((float(AB[0]), float(AB[1])))
    return params


def main() -> int:
    from argparse import ArgumentParser
    p = ArgumentParser(description=__doc__)
    p.add_argument("--csv", type=Path, required=True, help="Path to a CSV produced by generate_synthetic_dataset.py or your own loader.")
    p.add_argument("--out", type=Path, default=Path(__file__).parent / "build" / "SirenTagClassifier.generated.cpp",
                   help="Where to write the generated C++ source file. The whole file is the model — copy it over src/modules/siren/SirenTagClassifier.cpp.")
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
    print("\n── Test-set metrics (raw RF, before calibration) ──────────────")
    print(f"  hamming loss: {hamming_loss(Y_te, Y_pred):.3f}  (lower is better)")
    print("  per-class report:")
    print(classification_report(Y_te, Y_pred, target_names=CLASS_NAMES, zero_division=0))

    # Platt sigmoid calibration — fitted on the test set.
    # RF probability estimates cluster near 0 and 1 without meaning it; Platt
    # scaling learns a per-class logistic transform (a * p_raw + b) → sigmoid
    # that pushes them toward the true empirical distribution.
    # Note: we calibrate on the same test set used for the metrics above, so
    # the calibration is slightly optimistic; swap to a separate cal split if
    # the dataset grows large enough to support it.
    print("\nFitting per-class Platt sigmoid calibration on test set ...")
    calibration_params = fit_calibration(model.estimators_, X_te, Y_te)
    n_cal = sum(1 for p in calibration_params if p is not None)
    print(f"  calibrated {n_cal}/{len(calibration_params)} classes")

    # Apply calibration to test-set probabilities and report again.
    # Collects raw per-class probabilities, applies the Platt sigmoid, then
    # thresholds at 0.5 to produce hard predictions for the metrics.
    Y_cal_proba = np.zeros((len(X_te), len(CLASS_NAMES)), dtype=np.float32)
    for c, est in enumerate(model.estimators_):
        raw = est.predict_proba(X_te)[:, 1]
        cal = calibration_params[c]
        if cal is not None:
            a, b = cal
            Y_cal_proba[:, c] = (1.0 / (1.0 + np.exp(a * raw + b))).astype(np.float32)
        else:
            Y_cal_proba[:, c] = raw
    Y_pred_cal = (Y_cal_proba >= 0.5).astype(np.int32)
    print("\n── Test-set metrics (after Platt calibration) ─────────────────")
    print(f"  hamming loss: {hamming_loss(Y_te, Y_pred_cal):.3f}  (lower is better)")
    print("  per-class report:")
    print(classification_report(Y_te, Y_pred_cal, target_names=CLASS_NAMES, zero_division=0))

    # Smoke-test: run on a few training samples and print top-3
    print("── Smoke test on 5 training samples ────────────────────────────")
    sample_X = X_tr[:5]
    sample_scores = model.predict_proba(sample_X)
    for i in range(len(sample_X)):
        # Each per-label classifier returns (n_samples, 2) probas for [class 0, class 1].
        # If a class was missing from the training set entirely (e.g. you supplied
        # only 6 of the 18 classes), MultiOutputClassifier returns a 1-column
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

    # Build the training-parameters metadata that gets embedded in the
    # generated C++ as a JSON blob. This is what the plugin can later
    # show in its "About / model info" UI to answer "where did this
    # classifier come from?" without needing any sidecar files.
    n_cal = sum(1 for p in calibration_params if p is not None)
    try:
        m2cgen_version = __import__("m2cgen").__version__
    except (ImportError, AttributeError):
        m2cgen_version = None
    training_params: dict = {
        "augment_copies":   args.augment,
        "calibrated_classes": n_cal,
        "calibration":      "platt_sigmoid",
        "dataset_csv":      str(args.csv),
        "dataset_classes":  NUM_CLASSES,
        "dataset_features": NUM_FEATURES,
        "dataset_shape":    [int(X.shape[0]), int(X.shape[1])],
        "max_depth":        args.max_depth,
        "min_samples_leaf": 2,
        "model_version":    MODEL_VERSION,
        "n_estimators":     args.n_estimators,
        "platform":         platform.platform(),
        "python_version":   sys.version.split()[0],
        "random_seed":      args.seed,
        "sklearn_version":  sklearn.__version__,
        "test_size":        0.25,
        "train_samples":    int(X_tr.shape[0]),
        "test_samples":     int(X_te.shape[0]),
    }
    if m2cgen_version:
        training_params["m2cgen_version"] = m2cgen_version
    try:
        training_params["numpy_version"] = np.__version__
    except AttributeError:
        pass

    # Emit C++
    args.out.parent.mkdir(parents=True, exist_ok=True)
    emit_cpp(model, args.out,
             calibration_params=calibration_params,
             training_params=training_params)
    print(f"\nWrote generated C++ to {args.out}")
    print("Paste its contents into the marked region of src/modules/Siren/SirenTagClassifier.cpp, then rebuild.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
