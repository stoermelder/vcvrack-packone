"""Train a small multilabel classifier on the synthetic (or real) dataset.

Uses a Random Forest from scikit-learn — handles non-linear feature
interactions out of the box, doesn't need feature scaling, and emits to
clean C code via `m2cgen`. Per-class binary heads are managed by
`PerClassRFBag`, which exposes the same `.estimators_` interface expected
by `emit_cpp`.

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
from sklearn.model_selection import GroupShuffleSplit

from emit_cpp import emit_cpp
from feature_config import CLASS_NAMES, MODEL_VERSION, NUM_CLASSES, NUM_FEATURES


# Per-row weight for hard-negative samples (rows where the `negatives`
# column is non-empty). Hard-negative rows are mis-classified real-world
# recordings collected under `Non-<Tag>/` folders. The 3x weight is
# tuned so the RandomForest's per-split Gini criterion gives them
# disproportionate influence on the specific binary head for the named
# tag, without spilling over to the other 17 heads (where the sample
# is just ordinary background data).
DEFAULT_NEGATIVE_WEIGHT = 3.0


def load_csv(
    csv_path: Path,
    negative_weight: float = DEFAULT_NEGATIVE_WEIGHT,
) -> tuple[np.ndarray, np.ndarray, list[str], np.ndarray, tuple[int, int, int]]:
    """Read a CSV produced by `load_folder_dataset.py`.

    Schema is forward-compatible:
      - Newer CSVs (written since the `Non-<Tag>` convention landed)
        carry a `negatives` column (column index 2). Rows with a
        non-empty value in that column are *hard negatives* for the
        named tag(s): their `Y[i, neg_tag]` is forced to 0 (it usually
        would be already, but we make it explicit), the row is given a
        `negative_weight` x higher `sample_weight` *for that specific
        class's binary head only*, and is MASKED (weight 0.0) on every
        other head so it is dropped from those fits rather than asserted
        as a confirmed negative of tags it was never labelled against.
      - Older CSVs (no `negatives` column) still parse correctly:
        every row is a unit-weight positive for every class.

    Returns:
        X              (N, NUM_FEATURES)        float32 — feature matrix
        Y              (N, NUM_CLASSES)         int32   — one-vs-rest label matrix
        paths          list[str]                          — source path per row
        sample_weights (N, NUM_CLASSES)         float32 — per-(row, class)
                                                       weight for fit().
                                                       A pure hard-negative
                                                       row carries
                                                       `negative_weight` for
                                                       the specific class it is
                                                       a negative of and 0.0
                                                       (masked — dropped from
                                                       the fit) for every other
                                                       class, so it makes no
                                                       claim about heads it was
                                                       never labelled against.
                                                       All other rows are 1.0.
        neg_stats      (n_neg_rows, n_neg_cells,
                        n_neg_classes)               int     — hard-negative
                                                       counts derived directly
                                                       from the parsed
                                                       `negatives` column
                                                       (independent of
                                                       `negative_weight`, so
                                                       they stay correct even
                                                       when boosting is
                                                       disabled via 1.0 or 0.0).
    """
    X, Y, paths, neg_tags_per_row = [], [], [], []
    with open(csv_path) as f:
        reader = csv.reader(f)
        header = next(reader)
        # Detect the optional `negatives` column. Older CSVs have header
        # [path, label, f0, f1, ...]; newer ones have
        # [path, label, negatives, f0, f1, ...].
        try:
            neg_col = header.index("negatives")
        except ValueError:
            neg_col = -1
        # Feature columns always start at index 2 (after path, label,
        # and -- in newer CSVs -- the negatives column).
        feature_offset = 3 if neg_col >= 0 else 2
        for row in reader:
            if not row:
                continue
            paths.append(row[0])
            label = row[1] if len(row) > 1 else ""
            negatives = row[neg_col] if neg_col >= 0 and len(row) > neg_col else ""
            # Features are `NUM_FEATURES` floats starting at feature_offset.
            feat_end = feature_offset + NUM_FEATURES
            if len(row) < feat_end:
                raise ValueError(
                    f"row {paths[-1]!r}: expected {feat_end} columns, got {len(row)} "
                    f"(schema is path,label[,negatives],f0..f{NUM_FEATURES - 1})"
                )
            X.append([float(v) for v in row[feature_offset:feat_end]])
            Y.append((label, negatives))
            # Collect the per-row list of class indices this row is a
            # hard negative of. We resolve names → indices here so the
            # matrix construction below doesn't redo the work.
            row_neg_idxs: list[int] = []
            if negatives:
                for tag in (t.strip() for t in negatives.split("~")):
                    if not tag:
                        continue
                    if tag in CLASS_NAMES:
                        row_neg_idxs.append(CLASS_NAMES.index(tag))
                    else:
                        print(f"  warn: unknown hard-negative tag {tag!r} in row {paths[-1]!r}", file=sys.stderr)
            neg_tags_per_row.append(row_neg_idxs)
    X = np.asarray(X, dtype=np.float32)
    Y_idx = np.zeros((len(Y), NUM_CLASSES), dtype=np.int32)

    for i, (label, _) in enumerate(Y):
        # Positive labels. Empty label is allowed (pure-negative row).
        if label:
            # `label` is a single tag for backwards-compat; for the
            # initial cut we don't add a multi-label splitter here --
            # `load_folder_dataset.py` writes a single positive per row.
            if label in CLASS_NAMES:
                Y_idx[i, CLASS_NAMES.index(label)] = 1
            else:
                print(f"  warn: unknown label {label!r} in row {i}", file=sys.stderr)
        # Hard-negative tags: force the named head to 0 (usually already,
        # but kept explicit for clarity).
        for c in neg_tags_per_row[i]:
            Y_idx[i, c] = 0

    # Per-(row, class) weight matrix. Start at 1.0 everywhere. For a
    # hard-negative row (collected under `Non-<Tag>/`) we know exactly one
    # thing: it is NOT the target tag(s). We make no claim about the other
    # 16-17 heads — the file may well be a true positive of some other tag
    # that simply resembles the target. So such a row is:
    #   - weighted `negative_weight` on its target column(s) (a strong
    #     "definitely not this" signal for that head), and
    #   - MASKED (weight 0.0) on every other head, so it is dropped from
    #     those heads' fits rather than asserted as a confirmed negative.
    # Masking only the non-target cells (vs. weight 1.0) avoids injecting
    # false negatives into unrelated heads. The other heads don't go short
    # on negatives: every positive row of every other tag already serves as
    # a one-vs-rest negative here. A row carrying a positive label is left
    # fully weighted (it is a real, fully-known example).
    sample_weights = np.ones((len(Y), NUM_CLASSES), dtype=np.float32)
    neg_class_seen = np.zeros(NUM_CLASSES, dtype=bool)
    n_neg_cells = 0
    n_neg_rows = 0
    for i, neg_idxs in enumerate(neg_tags_per_row):
        if not neg_idxs:
            continue
        # Pure hard-negative row (label and negatives are mutually exclusive
        # in load_folder_dataset.py): silence every head except the targets.
        if not Y[i][0]:
            sample_weights[i, :] = 0.0
        for c in neg_idxs:
            sample_weights[i, c] = negative_weight
            neg_class_seen[c] = True
            n_neg_cells += 1
        n_neg_rows += 1
    # Derived directly from the parsed `negatives` column rather than from
    # `sample_weights`, so the counts stay correct even when `negative_weight`
    # is 1.0 (disables boosting) or 0.0 (skips hard-negatives) -- values the
    # weight matrix alone can no longer distinguish from "no hard negative".
    n_neg_classes = int(np.sum(neg_class_seen))

    print(f"  load_csv: {len(Y)} rows  ({n_neg_rows} hard-negative rows, "
          f"{n_neg_cells} (row,class) hard-negative cells across "
          f"{n_neg_classes}/{NUM_CLASSES} classes, weight={negative_weight}x)")
    return X, Y_idx, paths, sample_weights, (n_neg_rows, n_neg_cells, n_neg_classes)


def augment_dataset(
    X: np.ndarray,
    Y: np.ndarray,
    n_aug: int = 3,
    seed: int = 123,
    sample_weights: np.ndarray | None = None,
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Light augmentation: add small Gaussian noise to features. Keeps the
    feature-space alignment with the C++ runtime, since the C++ code also
    produces noisy estimates of the same features.

    `sample_weights` may be either:
      - 1-D, shape (N,) — a per-row scalar weight (uniform across all
        classes, e.g. for backwards compat with old callers).
      - 2-D, shape (N, NUM_CLASSES) — a per-(row, class) weight
        (produced by `load_csv` for the `Non-<Tag>` hard-negative
        pipeline). Each augmented copy inherits the source row's full
        weight vector so per-class pressure is preserved.

    When omitted, returns a uniform `np.ones((N_total, NUM_CLASSES))`
    matrix for the caller's convenience.
    """
    rng = np.random.default_rng(seed)
    Xs = [X]
    Ys = [Y]
    if sample_weights is None:
        # Default: uniform weight = 1.0 for every (row, class).
        if NUM_CLASSES > 0:
            Ws = [np.ones((len(X), NUM_CLASSES), dtype=np.float32)]
        else:
            Ws = [np.ones((len(X), 0), dtype=np.float32)]
    else:
        Ws = [sample_weights]
    for _ in range(n_aug):
        noise = rng.normal(0.0, 0.03, size=X.shape).astype(np.float32)
        Xs.append(np.clip(X + noise, 0.0, 1.0))
        Ys.append(Y)
        if sample_weights is not None:
            Ws.append(sample_weights)
    return (
        np.concatenate(Xs, axis=0),
        np.concatenate(Ys, axis=0),
        np.concatenate(Ws, axis=0),
    )


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


def _calibrated_proba(
    est, calibration: tuple[float, float] | None, X: np.ndarray
) -> np.ndarray | None:
    """Positive-class probability for one head, after Platt scaling.

    Returns None when the head has no positive-class column (degenerate
    single-label training split). Mirrors the C++ runtime: raw RF score
    then `1/(1+exp(a*raw+b))` when a calibration pair exists, else raw.
    """
    proba = est.predict_proba(X)
    if proba.ndim != 2 or proba.shape[1] < 2:
        return None
    raw = proba[:, 1]
    if calibration is not None:
        a, b = calibration
        return 1.0 / (1.0 + np.exp(a * raw + b))
    return raw


def fit_thresholds(
    estimators: list,
    calibration_params: list[tuple[float, float] | None],
    X_cal: np.ndarray,
    Y_cal: np.ndarray,
    default: float = 0.5,
    lo: float = 0.05,
    hi: float = 0.9,
    min_pos: int = 3,
    bias: float = 0.0,
) -> list[float]:
    """Per-class decision threshold that maximises F1 on the calibration slice.

    The one-vs-rest RF heads on imbalanced tags emit *calibrated* probabilities
    that seldom exceed 0.5 (a well-calibrated P(tag) for a tag present in <50%
    of clips can't), so a global 0.5 cut starves recall — most tags are never
    applied. Instead we pick, per class, the threshold on the calibrated
    probability that maximises F1 over the held-out calibration slice. That is
    the exact same probability the deployed C++ thresholds against, so the
    tuned cut transfers directly.

    A class with fewer than `min_pos` positives in the cal slice (too few to
    estimate a stable cut) or with no calibratable head falls back to
    `default`. The chosen threshold is clamped to `[lo, hi]` so a single noisy
    cal positive can't drive it to a degenerate 0 or 1.

    `bias` is a global nudge applied to every class's threshold (including the
    fallback default) as a shift in **log-odds** space:
    `t' = sigmoid(logit(t) + bias)`. Positive values make tagging more
    conservative (higher precision, lower recall); negative values make it more
    eager. Working in log-odds — the natural scale of a calibrated probability —
    keeps the nudge proportional: a low-threshold (weak/rare) class moves by a
    small absolute amount and can't be slammed to the floor, while mid-range
    classes move more. This avoids the precision cliff an additive nudge causes
    once the many already-low thresholds (0.05–0.30) hit a hard floor at once.
    The transform is self-clamping (logit maps (0,1) → ±∞), so no min/max is
    needed beyond guarding the input away from exactly 0 or 1.
    """
    def _finish(t: float) -> float:
        if bias == 0.0:
            return float(t)
        t = float(min(max(t, 1e-4), 1.0 - 1e-4))
        z = np.log(t / (1.0 - t)) + bias
        return float(1.0 / (1.0 + np.exp(-z)))

    thresholds: list[float] = []
    for c, est in enumerate(estimators):
        y = Y_cal[:, c]
        n_pos = int(np.sum(y > 0))
        p = _calibrated_proba(est, calibration_params[c], X_cal)
        if p is None or n_pos < min_pos:
            thresholds.append(_finish(default))
            continue
        # Candidate cuts: midpoints between consecutive distinct scores, so
        # every achievable (TP, FP) split is considered exactly once.
        cand = np.unique(p)
        cand = (cand[:-1] + cand[1:]) / 2.0 if cand.size > 1 else cand
        best_t, best_f1 = default, -1.0
        for t in cand:
            pred = p >= t
            tp = int(np.sum(pred & (y > 0)))
            if tp == 0:
                continue
            fp = int(np.sum(pred & (y == 0)))
            fn = int(np.sum(~pred & (y > 0)))
            prec = tp / (tp + fp)
            rec = tp / (tp + fn)
            f1 = 2.0 * prec * rec / (prec + rec)
            if f1 > best_f1:
                best_f1, best_t = f1, float(t)
        thresholds.append(_finish(min(max(best_t, lo), hi)))
    return thresholds


class PerClassRFBag:
    """18 (NUM_CLASSES) independent RandomForestClassifiers, one per binary head.

    This replaces `sklearn.multioutput.MultiOutputClassifier` so we can
    pass a *per-class* `sample_weight` to each fit() call. MultiOutputClassifier
    only supports a single (per-row) sample_weight that's forwarded
    identically to every sub-classifier — which is exactly the leak we
    want to avoid: a `Non-Kick/` sample should only carry extra weight
    for the Kick head, not for all 18.

    API surface kept minimal to match what the existing trainer code
    expects:
      - `.estimators_`   list[RandomForestClassifier], length NUM_CLASSES
      - `.predict_proba(X)` returns a list of (N, 2) arrays, one per
        class, in `estimators_` order. Empty (1, 1) arrays for classes
        where the training set had no positives (matches the behaviour
        of MultiOutputClassifier in that degenerate case).
      - `.predict(X)`    hard 0/1 predictions at threshold 0.5, shape
        (N, NUM_CLASSES).
    """
    def __init__(self, n_estimators: int, max_depth: int, min_samples_leaf: int,
                 random_state: int, n_jobs: int = -1):
        self.estimators_: list[RandomForestClassifier] = [
            RandomForestClassifier(
                n_estimators=n_estimators,
                max_depth=max_depth,
                min_samples_leaf=min_samples_leaf,
                random_state=random_state,
                n_jobs=n_jobs,
            )
            for _ in range(NUM_CLASSES)
        ]

    def fit(self, X: np.ndarray, Y: np.ndarray,
            sample_weights: np.ndarray | None = None) -> "PerClassRFBag":
        """Fit one RF per class with its own per-row sample_weight slice.

        `sample_weights` is a (N, NUM_CLASSES) matrix. If `None` or 1-D,
        every class gets a uniform 1.0 weight.
        """
        for c, est in enumerate(self.estimators_):
            y_c = Y[:, c]
            # Per-class weight slice for column c. If the matrix is 1-D
            # we fall back to the scalar (back-compat) behaviour.
            if sample_weights is None:
                w_c = None
            elif sample_weights.ndim == 1:
                w_c = sample_weights
            else:
                w_c = sample_weights[:, c]
            est.fit(X, y_c, sample_weight=w_c)
        return self

    def predict_proba(self, X: np.ndarray) -> list[np.ndarray]:
        """Return one (N, 2) probability array per class, mirroring
        MultiOutputClassifier's behaviour (including the degenerate
        1-column return for classes with no training-time positives)."""
        out: list[np.ndarray] = []
        for est in self.estimators_:
            try:
                proba = est.predict_proba(X)
            except Exception:
                proba = np.zeros((len(X), 1), dtype=np.float32)
            if proba.ndim == 1:
                proba = proba.reshape(-1, 1)
            out.append(proba)
        return out

    def predict(self, X: np.ndarray, threshold: float = 0.5) -> np.ndarray:
        probas = self.predict_proba(X)
        Y_pred = np.zeros((len(X), NUM_CLASSES), dtype=np.int32)
        for c, proba in enumerate(probas):
            if proba.shape[1] < 2:
                # No positive class in the test split — predict 0.
                continue
            Y_pred[:, c] = (proba[:, 1] >= threshold).astype(np.int32)
        return Y_pred


def main() -> int:
    from argparse import ArgumentParser
    p = ArgumentParser(description=__doc__)
    p.add_argument("--csv", type=Path, required=True, help="Path to a CSV produced by generate_synthetic_dataset.py or your own loader.")
    p.add_argument("--out", type=Path, default=Path(__file__).parent / "build" / "SirenTagClassifier.generated.cpp",
                   help="Where to write the generated C++ source file. The whole file is the model — copy it over src/modules/siren/SirenTagClassifier.cpp.")
    p.add_argument("--n-estimators", type=int, default=24)
    p.add_argument("--max-depth", type=int, default=8)
    p.add_argument("--augment", type=int, default=0,
                   help="Feature-space Gaussian jitter: extra noisy copies per sample (default 0 = off). "
                        "Measured to not help (and to hurt the weak classes) on real data — the uniform "
                        "0.03 noise swamps low-variance features. Audio-domain augmentation in "
                        "load_folder_dataset.py is the useful kind. Left as an opt-in knob.")
    p.add_argument("--seed", type=int, default=42)
    p.add_argument("--negative-weight", type=float, default=DEFAULT_NEGATIVE_WEIGHT,
                   help=f"Per-(row, class) sample weight applied to hard-negative cells (default {DEFAULT_NEGATIVE_WEIGHT}). "
                        "Set to 1.0 to disable weighting, or 0.0 to skip hard-negatives entirely (debug).")
    p.add_argument("--threshold-bias", type=float, default=0.0,
                   help="Global nudge to every per-class decision threshold, as a shift in "
                        "log-odds: t' = sigmoid(logit(t) + bias) (default 0.0). "
                        "Positive = more conservative (fewer, more-precise tags — rather miss a tag "
                        "than apply a wrong one); negative = more eager (more tags, more recall). "
                        "Proportional in probability space, so weak/low-threshold classes aren't "
                        "over-nudged. Applied after F1 tuning. Try +/-0.3 to +/-0.7.")
    args = p.parse_args()

    print(f"Loading {args.csv} ...")
    X, Y, paths, sample_weights, neg_stats = load_csv(args.csv, negative_weight=args.negative_weight)
    # Hard-negative counts come straight from `load_csv` (derived from the
    # parsed `negatives` column), not from thresholding `sample_weights` --
    # the matrix alone can't tell a hard-negative cell from an ordinary one
    # once `negative_weight` is 1.0 (disabled) or 0.0 (skipped).
    n_neg_rows, n_neg_cells, n_neg_classes = neg_stats
    print(f"  X: {X.shape}  Y: {Y.shape}  positive-rate: {Y.mean():.3f}  "
          f"hard-negatives: {n_neg_rows} rows, {n_neg_cells} (row,class) cells, "
          f"across {n_neg_classes}/{NUM_CLASSES} classes")

    # Split BEFORE augmenting, and split by GROUP (source clip), not by row.
    # Two reasons leakage would otherwise creep in:
    #   - feature-space augmentation (below) makes near-duplicate copies, and
    #   - audio-domain augmentation (load_folder_dataset --augment) emits extra
    #     rows that share the source clip's `path`.
    # Splitting on the clip key keeps every variant of a clip on the same side,
    # so the test set never contains a sibling of a training sample.
    # The raw clips are split three ways — train (≈60%), a held-out calibration
    # slice (≈20%), and test (≈20%).
    #
    # Audio-domain augmented rows (from `load_folder_dataset --augment`) carry an
    # `#aug<j>` marker on their path. We strip it to derive the clip group, but
    # we also keep those rows OUT of the calibration and test partitions:
    # augmentation is a training-time signal, and scoring/calibrating on noisy,
    # reverbed, filtered audio understates real performance (and can invalidate
    # the label outright). Train keeps originals + augmented; cal/test keep
    # originals only.
    paths_arr = np.asarray(paths)
    is_aug = np.array(["#aug" in str(p) for p in paths_arr])
    groups = np.array([str(p).split("#aug", 1)[0] for p in paths_arr])

    def _group_split(idx: np.ndarray, test_size: float):
        """Split the row indices in `idx` by clip group; returns (train_idx, test_idx)."""
        gss = GroupShuffleSplit(n_splits=1, test_size=test_size, random_state=args.seed)
        a, b = next(gss.split(idx, groups=groups[idx]))
        return idx[a], idx[b]

    # Split over ORIGINAL rows only, so the cal/test partitions are pure
    # originals and the test_size fractions describe real clips. Augmented rows
    # are folded back into whichever clip-group lands in train afterwards.
    orig_idx = np.where(~is_aug)[0]
    tmp_idx, te_idx = _group_split(orig_idx, 0.20)
    # Carve the calibration slice out of the remaining 80% (0.25 × 0.8 = 0.20 of
    # the whole). Gate on the number of distinct CLIPS, not rows — tiny clip
    # counts can't spare a third slice, so fall back to calibrating on the test
    # set (clearly flagged) when there aren't enough.
    cal_separate = len(np.unique(groups[tmp_idx])) >= 8
    if cal_separate:
        tr_orig_idx, cal_idx = _group_split(tmp_idx, 0.25)
    else:
        tr_orig_idx, cal_idx = tmp_idx, te_idx
        print("  warn: too few distinct clips for a separate calibration slice — "
              "calibrating on the test set (calibrated metrics will be "
              "optimistic)", file=sys.stderr)

    # Train = original train clips + all augmented rows belonging to those clips.
    # Augmented rows of cal/test clips are simply dropped — never used anywhere.
    train_groups = set(groups[tr_orig_idx].tolist())
    tr_idx = np.array([i for i in range(len(X)) if groups[i] in train_groups], dtype=int)

    X_tr, Y_tr, w_tr = X[tr_idx], Y[tr_idx], sample_weights[tr_idx]
    X_cal, Y_cal = X[cal_idx], Y[cal_idx]
    X_te, Y_te = X[te_idx], Y[te_idx]
    n_train_rows = len(X_tr)
    n_train_aug = int(np.sum(is_aug[tr_idx]))
    n_train_clips = len(train_groups)
    if args.augment > 0:
        # Feature-space jitter on the TRAIN partition only. Each noisy copy
        # inherits its source row's full per-class weight vector so per-class
        # pressure is preserved.
        X_tr, Y_tr, w_tr = augment_dataset(
            X_tr, Y_tr, n_aug=args.augment, seed=args.seed, sample_weights=w_tr
        )
    print(f"  split: train={n_train_clips} clips → {n_train_rows} rows "
          f"({n_train_aug} audio-augmented)  "
          f"cal={len(X_cal)}{'' if cal_separate else ' [=test, fallback]'}  "
          f"test={len(X_te)}  (cal/test are clean originals only)")
    if args.augment > 0:
        print(f"  train after {args.augment}× feature jitter: {len(X_tr)} rows")

    print(f"\nTraining {NUM_CLASSES} independent RandomForest heads "
          f"(n_estimators={args.n_estimators}, max_depth={args.max_depth}) ...")
    print(f"  hard-negative weighting: per-(row, class) — only the specific class")
    print(f"  the row is a Non-<Tag> for gets the {args.negative_weight}x weight;")
    print(f"  all other classes see weight 1.0 for that row.")
    model = PerClassRFBag(
        n_estimators=args.n_estimators,
        max_depth=args.max_depth,
        min_samples_leaf=2,
        random_state=args.seed,
        n_jobs=-1,
    )
    # `sample_weight=w_tr` is the per-(row, class) weight matrix; each
    # head slices its own column out and uses it as the RF's sample_weight.
    # A `Non-Kick/snare.wav` row carries weight `negative_weight` ONLY in
    # the Kick column — the Pad, Snare, ..., Vocal heads see weight 1.0
    # for that row, exactly the same as any other unlabeled sample.
    model.fit(X_tr, Y_tr, sample_weights=w_tr)
    print("  done.")

    Y_pred = model.predict(X_te)
    print("\n── Test-set metrics (raw RF, before calibration) ──────────────")
    print(f"  hamming loss: {hamming_loss(Y_te, Y_pred):.3f}  (lower is better)")
    print("  per-class report:")
    print(classification_report(Y_te, Y_pred, target_names=CLASS_NAMES, zero_division=0))

    # Platt sigmoid calibration — fitted on the held-out calibration slice,
    # which is separate from both the training data and the test set, so the
    # calibration parameters never peek at the test set the metrics report on.
    # RF probability estimates cluster near 0 and 1 without meaning it; Platt
    # scaling learns a per-class logistic transform (a * p_raw + b) → sigmoid
    # that pushes them toward the true empirical distribution.
    cal_where = "calibration slice" if cal_separate else "test set (fallback)"
    print(f"\nFitting per-class Platt sigmoid calibration on {cal_where} "
          f"({len(X_cal)} rows) ...")
    calibration_params = fit_calibration(model.estimators_, X_cal, Y_cal)
    n_cal = sum(1 for p in calibration_params if p is not None)
    print(f"  calibrated {n_cal}/{len(calibration_params)} classes")

    # Apply calibration to test-set probabilities and report again.
    # Collects raw per-class probabilities, applies the Platt sigmoid, then
    # thresholds at 0.5 to produce hard predictions for the metrics.
    Y_cal_proba = np.zeros((len(X_te), len(CLASS_NAMES)), dtype=np.float32)
    for c, est in enumerate(model.estimators_):
        proba = est.predict_proba(X_te)
        # Guard: when a class has no positives in the test split (small or
        # unbalanced test sets), predict_proba returns shape (N, 1) and
        # there is no positive-class column to read. The matching calibration
        # entry will be None, so we just leave this class as 0.
        if proba.shape[1] < 2:
            continue
        raw = proba[:, 1]
        cal = calibration_params[c]
        if cal is not None:
            a, b = cal
            Y_cal_proba[:, c] = (1.0 / (1.0 + np.exp(a * raw + b))).astype(np.float32)
        else:
            Y_cal_proba[:, c] = raw
    Y_pred_cal = (Y_cal_proba >= 0.5).astype(np.int32)
    print("\n── Test-set metrics (after Platt calibration, global 0.5 cut) ──")
    print(f"  hamming loss: {hamming_loss(Y_te, Y_pred_cal):.3f}  (lower is better)")
    print("  per-class report:")
    print(classification_report(Y_te, Y_pred_cal, target_names=CLASS_NAMES, zero_division=0))

    # Per-class decision thresholds, tuned for F1 on the calibration slice.
    # The deployed C++ thresholds the calibrated probability per class; a global
    # 0.5 cut starves recall on the (many) tags whose calibrated P rarely tops
    # 0.5. Tuned thresholds are emitted with the model and used by the runtime.
    print(f"\nFitting per-class F1 thresholds on {cal_where} "
          f"({len(X_cal)} rows) ...")
    thresholds = fit_thresholds(model.estimators_, calibration_params, X_cal, Y_cal,
                                bias=args.threshold_bias)
    # Default-class fallback after the same log-odds nudge fit_thresholds applies
    # (logit(0.5) = 0, so it reduces to sigmoid(bias)).
    base_default = float(1.0 / (1.0 + np.exp(-args.threshold_bias)))
    n_tuned = sum(1 for t in thresholds if abs(t - base_default) > 1e-6)
    if abs(args.threshold_bias) > 1e-6:
        print(f"  threshold bias: {args.threshold_bias:+.3f} "
              f"({'more conservative — fewer tags' if args.threshold_bias > 0 else 'more eager — more tags'})")
    print(f"  tuned {n_tuned}/{len(thresholds)} classes (others fall back to {base_default:.3f})")
    for c, t in enumerate(thresholds):
        flag = "" if abs(t - base_default) > 1e-6 else "  (default — too few cal positives)"
        print(f"    {CLASS_NAMES[c]:<12} thr={t:.3f}{flag}")

    Y_pred_thr = (Y_cal_proba >= np.asarray(thresholds, dtype=np.float32)).astype(np.int32)
    print("\n── Test-set metrics (after Platt calibration, per-class thresholds) ──")
    print(f"  hamming loss: {hamming_loss(Y_te, Y_pred_thr):.3f}  (lower is better)")
    print("  per-class report:")
    print(classification_report(Y_te, Y_pred_thr, target_names=CLASS_NAMES, zero_division=0))

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
    # Audio-domain augmentation is applied by the folder loader, not here, so it
    # isn't a trainer argument — augmented rows are flagged by an `#aug` marker
    # on their path. Count them per clip and report the modal value, so the
    # provenance is recorded even for combined CSVs. 0 means none (e.g. the
    # synthetic dataset).
    _uniq, _inv = np.unique(groups, return_inverse=True)
    _aug_per_clip = np.bincount(_inv, weights=is_aug.astype(np.int64),
                                minlength=len(_uniq)).astype(int)
    audio_augment_variants = int(np.bincount(_aug_per_clip).argmax()) if _aug_per_clip.size else 0
    training_params: dict = {
        # Augmentation provenance. `augment_copies` is the trainer's feature-
        # space jitter (post-extraction); `audio_augment_variants` is the
        # folder loader's audio-domain augmentation (pre-extraction), derived
        # from per-clip row multiplicity.
        "augment_copies":   args.augment,
        "audio_augment_variants": audio_augment_variants,
        "distinct_clips":   int(_uniq.size),
        "split_grouped_by": "clip (path, #aug stripped); cal/test = clean originals only",
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
        # Hard-negative diagnostics. The pipeline stores one weight
        # cell per (row, class) pair, so we report both:
        #   - negative_cells: total count of (row, class) cells that
        #     carry the boosted weight (== number of `Non-<Tag>`
        #     audio files * 1, since each folder targets one tag).
        #   - negative_classes: number of distinct classes with at
        #     least one hard-negative cell (== number of `Non-<Tag>`
        #     folders supplied).
        "negative_cells":   int(n_neg_cells),
        "negative_classes": int(n_neg_classes),
        "negative_weight":  float(args.negative_weight),
        "platform":         platform.platform(),
        "python_version":   sys.version.split()[0],
        "random_seed":      args.seed,
        "sklearn_version":  sklearn.__version__,
        # Three-way split by clip. cal/test hold clean originals only; train
        # holds its clips' originals + audio-augmented rows. `train_rows` is
        # before feature jitter, `train_samples` after.
        "test_size":        0.20,
        "cal_size":         0.20 if cal_separate else 0.0,
        "cal_separate":     bool(cal_separate),
        "train_clips":      int(n_train_clips),
        "train_rows":       int(n_train_rows),
        "train_audio_augmented": int(n_train_aug),
        "train_samples":    int(X_tr.shape[0]),
        "cal_samples":      int(X_cal.shape[0]),
        "test_samples":     int(X_te.shape[0]),
        # Per-class decision thresholds (F1-tuned on the cal slice; 0.5 =
        # untuned fallback). The runtime applies these to the calibrated score.
        "decision_thresholds": {CLASS_NAMES[c]: round(t, 4) for c, t in enumerate(thresholds)},
        "thresholds_tuned":  int(n_tuned),
        "threshold_strategy": "per_class_f1_on_cal_slice",
        "threshold_bias":    round(float(args.threshold_bias), 4),
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
             thresholds=thresholds,
             training_params=training_params)
    print(f"\nWrote generated C++ to {args.out}")
    print("Paste its contents into the marked region of src/modules/Siren/SirenTagClassifier.cpp, then rebuild.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
