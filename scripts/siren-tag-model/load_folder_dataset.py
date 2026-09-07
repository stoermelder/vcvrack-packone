"""Load a folder-based labeled dataset and write a CSV the trainer can read.

The convention is: one subfolder per tag, named exactly after a tag in
`res/data/SirenTags.json` (e.g. `drone/`, `percussion/`, `pad/`).
Inside each subfolder, any `.wav` / `.flac` / `.mp3` / `.ogg` / `.aif*` file
is treated as a training example for that tag. Features are extracted with
the same `features.extract_features()` used during inference, so the CSV
is in lockstep with the C++ runtime.

This script is the **bridge** between "I have audio files in folders"
and the trainer. It does NOT change the trainer; it just produces a
`build/my_dataset.csv` you point `train_model.py` at.

Folder-name → tag mapping
-------------------------

The folder name MUST match a tag's `name` field in
`res/data/SirenTags.json`, exactly (case-insensitive
matching is applied to be friendly, but case is preserved in the CSV).
Anything that doesn't match a known tag is skipped with a warning.

    my_dataset/
        bass/         <- one folder per tag
            sub_001.wav
            sub_002.wav
            ...
        bright/
            hat_001.wav
            ...
        lead/         <- a folder can be empty (just produces zero rows)
            ...

Hard-negative folders (`Non-<Tag>`)
-----------------------------------

A folder whose name starts with `Non-` (case-insensitive) is treated as
a **hard-negative** bucket for the named tag. Every audio file in such
a folder is written as a CSV row with empty `label` and a populated
`negatives` column listing the target tag. The trainer applies a
configurable per-row weight (default 3.0) so the model is pushed
strongly away from the named tag for these feature vectors.

The point is to collect real-world mis-classified samples — drop a
noisy recording that keeps getting mis-tagged as "Kick" into
`Non-Kick/` and the Kick binary head will be penalised for predicting
"Kick" on its feature vector.

A `Non-<Tag>` row makes a claim about *one* head only: "this is not
<Tag>". It is deliberately **not** treated as a negative for the other
heads — the trainer masks (weight 0.0) every non-target head for these
rows, so a file that is genuinely some other tag (e.g. a sub-bass note
filed under `Non-Kick/` because it resembles a kick) is never injected
as a false negative into the Bass head. If you instead want a sample
that is "none of the 18 tags at all" and should act as a negative for
every head, give it a positive label of its true tag (if it has one),
or omit it — do not file true positives of tag B under `Non-A/`.

    my_dataset/
        Kick/
            kick_001.wav
        Non-Kick/                    <- hard negatives for the Kick head
            snare_loop_001.wav
            bass_sub_001.wav
        Pad/
            pad_001.wav
        Non-Pad/
            kick_002.wav
            clap_001.wav

Multiple `Non-<Tag>` folders may be supplied (one per tag you want to
push back on). Folders named exactly `Non-` (no tag suffix) or
`Non-<unknown>` are skipped with a warning.

Single-label vs multi-label
---------------------------

The default is **single-label per file** (a file inherits its parent
folder's tag). This matches how the synthetic dataset and the trainer
work today. If you have files that should wear multiple tags, you have
two options:

1. **Duplicate the file** under each tag's folder and let the trainer
   see it multiple times. Simple; works with the current trainer.

2. **Hand-write a CSV** with multiple labels per row. See
   `--multi-label-csv` below for the format.

Usage
-----

    # 1. Folder convention (single-label):
    python load_folder_dataset.py path/to/my_dataset --out build/my_dataset.csv

    # 2. Use that CSV with the trainer:
    python train_model.py --csv build/my_dataset.csv

    # 3. Or in one go via run.sh:
    bash run.sh --csv build/my_dataset.csv

The CSV is appended to (not overwritten) if `--append` is passed, so you
can merge multiple folder datasets before training.
"""
from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path

import numpy as np

from audio_augment import augment_waveform, load_mono
from feature_config import NUM_FEATURES
from features import (  # raises if not built
    extract_features_batch,
    extract_features_for_arrays,
    find_cpp_extractor,
)
from tag_manifest import CLASS_NAMES, TAGS

# Audio file extensions we recognize. soundfile / libsndfile handles these.
AUDIO_EXTENSIONS = {".wav", ".flac", ".mp3", ".ogg", ".aif", ".aiff", ".aifc"}

# Hard-negative folder prefix. Folders whose name is `Non-<Tag>` (or any
# case variant thereof) are hard-negative buckets for the named tag.
NEGATIVE_FOLDER_PREFIX = "Non-"


def _normalize_tag(name: str) -> str:
    """Lowercase for matching; preserve original spelling when writing CSV."""
    return name.strip().lower()


def _find_matching_tag(folder_name: str) -> str | None:
    """Return the canonical tag name (preserving its case) for a folder name.

    Returns None if the folder doesn't match any tag in the manifest.
    """
    wanted = folder_name.strip().lower()
    for tag in TAGS:
        if tag.name.lower() == wanted:
            return tag.name
    return None


def _parse_negative_folder(folder_name: str) -> str | None:
    """Return the canonical tag name targeted by a `Non-<Tag>` folder.

    Returns the canonical tag (preserving its case from the manifest) if
    `folder_name` starts with `Non-` and the suffix matches a known tag.
    Returns None otherwise — including the bare prefix `Non-` (no tag
    suffix) and `Non-<unknown>`.
    """
    if not folder_name.lower().startswith(NEGATIVE_FOLDER_PREFIX.lower()):
        return None
    suffix = folder_name[len(NEGATIVE_FOLDER_PREFIX):].strip()
    if not suffix:
        return None
    return _find_matching_tag(suffix)


def _list_audio_files(folder: Path) -> list[Path]:
    return sorted(
        p for p in folder.iterdir()
        if p.is_file() and p.suffix.lower() in AUDIO_EXTENSIONS
    )




def _write_csv(rows: list[list], out_path: Path, append: bool) -> int:
    out_path.parent.mkdir(parents=True, exist_ok=True)
    mode = "a" if append else "w"
    new_file = not append or not out_path.exists()
    with open(out_path, mode, newline="") as f:
        w = csv.writer(f)
        if new_file:
            w.writerow(["path", "label", "negatives", *[f"f{i}" for i in range(NUM_FEATURES)]])
        w.writerows(rows)
    return len(rows)


def load_folder_dataset(
    root: Path,
    out_csv: Path,
    max_per_class: int | None = None,
    append: bool = False,
    seed: int = 42,
    augment: int = 0,
) -> dict[str, int]:
    """Walk `root`, find one subfolder per known tag, extract features, write CSV.

    When `augment > 0`, each source clip additionally contributes `augment`
    audio-domain augmented variants (noise, speed/pitch, EQ, reverb, time
    shift; see `audio_augment.py`), extracted through the same C++ extractor.
    All variants of a clip share the original clip's `path` value so the
    trainer can keep them on the same side of the train/cal/test split
    (group-aware splitting) — otherwise the augmented copies would leak across
    the boundary and inflate the metrics.

    Returns a dict {tag_name: row_count_written} for the caller's diagnostics.
    """
    if not root.exists() or not root.is_dir():
        raise FileNotFoundError(f"dataset root not found: {root}")

    rng = np.random.default_rng(seed)
    counts: dict[str, int] = {}
    neg_counts: dict[str, int] = {}

    # Phase 1: collect (file_path, label, negatives) tuples across all subfolders.
    # `label` and `negatives` are mutually exclusive: a positive folder sets
    # label and leaves negatives empty; a `Non-<Tag>` folder does the opposite.
    pending: list[tuple[Path, str, str]] = []
    subdirs = sorted(p for p in root.iterdir() if p.is_dir())
    if not subdirs:
        print(f"warning: no subdirectories found in {root}", file=sys.stderr)

    for sub in subdirs:
        canonical = _find_matching_tag(sub.name)
        is_negative = canonical is None
        negative_target: str | None = None
        if is_negative:
            negative_target = _parse_negative_folder(sub.name)
            if negative_target is None:
                print(
                    f"  skip folder {sub.name!r}: doesn't match any tag in SirenTags.json "
                    f"and isn't a valid Non-<Tag> folder "
                    f"(known tags: {', '.join(t.name for t in TAGS)})",
                    file=sys.stderr,
                )
                continue

        files = _list_audio_files(sub)
        if not files:
            folder_kind = negative_target or canonical
            print(f"  warn: {folder_kind!r} folder is empty ({sub})", file=sys.stderr)
            (neg_counts if is_negative else counts)[folder_kind] = 0
            continue

        if max_per_class is not None and len(files) > max_per_class:
            idxs = rng.choice(len(files), size=max_per_class, replace=False)
            files = [files[i] for i in sorted(idxs)]

        for f in files:
            if is_negative:
                # Empty label, populated `negatives` column.
                pending.append((f, "", negative_target or ""))
            else:
                # Empty `negatives`, populated label.
                pending.append((f, canonical or "", ""))
        if is_negative:
            neg_counts[negative_target or ""] = len(files)
            print(f"  Non-{negative_target:<10s}  {len(files):4d} clips from {sub}")
        else:
            counts[canonical or ""] = len(files)
            print(f"  {canonical:12s}  {len(files):4d} clips from {sub}")

    # Phase 2: batch-extract features via C++ binary.
    binary = find_cpp_extractor()
    print(f"  Using C++ extractor: {binary}")
    features_by_path = extract_features_batch([str(f) for f, _, _ in pending], binary)

    # Phase 3: build rows (one per successfully-extracted source clip).
    # `kept` tracks the clips whose originals made it in, so Phase 4 only
    # augments real, usable audio.
    rows: list[list] = []
    kept: list[tuple[Path, str, str, str]] = []  # (file, relpath, label, negatives)
    for f, label, negatives in pending:
        features = features_by_path.get(str(f))
        if features is None:
            print(f"  skip: extraction failed for {f.name}", file=sys.stderr)
            continue
        try:
            rel = f.relative_to(root.parent)
        except ValueError:
            rel = f
        rows.append([str(rel), label, negatives, *features.tolist()])
        kept.append((f, str(rel), label, negatives))

    # Phase 4: audio-domain augmentation. Generate `augment` variants per kept
    # clip, extract their features in one batch, and append rows that REUSE the
    # source clip's relpath (the trainer groups on it to avoid split leakage).
    if augment > 0 and kept:
        aug_rng = np.random.default_rng(seed + 1)
        items: list[tuple] = []                     # (key, waveform, sr) for the extractor
        key_meta: dict[str, tuple[str, str, str]] = {}  # key -> (relpath, label, negatives)
        n_loaded = 0
        for i, (f, rel, label, negatives) in enumerate(kept):
            loaded = load_mono(f)
            if loaded is None:
                continue                            # un-loadable here; original row still stands
            data, sr = loaded
            n_loaded += 1
            for j in range(augment):
                key = f"{i}#aug{j}"
                # The row's path carries an `#aug<j>` marker: the trainer strips
                # it to group variants with their original clip, AND uses it to
                # keep augmented rows OUT of the calibration/test partitions
                # (augmentation is a training-time signal; metrics/calibration
                # must see clean originals only).
                aug_rel = f"{rel}#aug{j}"
                items.append((key, augment_waveform(data, sr, aug_rng), sr))
                key_meta[key] = (aug_rel, label, negatives)
        print(f"  augmenting {n_loaded}/{len(kept)} clips × {augment} "
              f"→ {len(items)} variants ...")
        aug_feats = extract_features_for_arrays(items, binary)
        n_aug_rows = 0
        for key, feats in aug_feats.items():
            if feats is None:
                continue
            rel, label, negatives = key_meta[key]
            rows.append([rel, label, negatives, *feats.tolist()])
            n_aug_rows += 1
        print(f"  added {n_aug_rows} augmented rows "
              f"({len(items) - n_aug_rows} dropped as empty/failed)")

    if not rows:
        raise RuntimeError(
            f"No usable audio found under {root}. Check that subdirectory names "
            f"match a known tag (e.g. drone, percussion, lead) and contain audio files."
        )

    n_written = _write_csv(rows, out_csv, append=append)
    print()
    print(f"Wrote {n_written} rows to {out_csv}")
    print(f"Per-class positive counts: {counts}")
    print(f"Per-tag  hard-negative counts: {neg_counts}")
    return counts


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("root", type=Path, nargs="?",
                   help="Path to a folder containing one subdirectory per tag. Not required when --list-known-tags is set.")
    p.add_argument("--out", type=Path, default=Path(__file__).parent / "build" / "my_dataset.csv",
                   help="Where to write the resulting CSV.")
    p.add_argument("--max-per-class", type=int, default=None,
                   help="Optional cap on the number of clips per tag (for class balancing).")
    p.add_argument("--append", action="store_true",
                   help="Append to the CSV instead of overwriting (preserves the header if the file is new).")
    p.add_argument("--augment", type=int, default=0,
                   help="Audio-domain augmented variants to add per source clip "
                        "(0 = off). Variants share the source clip's path so the "
                        "trainer keeps them on one side of the split.")
    p.add_argument("--seed", type=int, default=42,
                   help="RNG seed for class-balancing sampling and augmentation.")
    p.add_argument("--list-known-tags", action="store_true",
                   help="Print the list of accepted tag names and exit.")
    p.add_argument("--self-test", action="store_true",
                   help="Run the folder-name parser self-test and exit. Useful in CI.")
    args = p.parse_args()

    if args.self_test:
        return _self_test()

    if args.list_known_tags:
        print("Accepted folder names (must match one of these exactly, case-insensitive):")
        for t in TAGS:
            print(f"  {t.name:12s}  ({t.category:7s})  {t.description}")
        return 0

    if args.root is None:
        print("error: 'root' is required (or pass --list-known-tags to print the accepted tag names).",
              file=sys.stderr)
        return 2

    try:
        load_folder_dataset(args.root, args.out, args.max_per_class, args.append,
                            seed=args.seed, augment=args.augment)
    except (FileNotFoundError, RuntimeError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    return 0


# The following block runs the loader's own self-test in isolation. Use:
#     python load_folder_dataset.py --self-test
# to verify the positive + negative folder parsers agree with the manifest.
def _self_test() -> int:
    """Exercise the folder-name parsers on synthetic inputs."""
    cases = [
        ("Kick",            "Kick",     None),
        ("non-kick",        None,       "Kick"),
        ("Non-Pad",         None,       "Pad"),
        ("NON-VOCAL",       None,       "Vocal"),
        ("Non-",            None,       None),  # bare prefix -- invalid
        ("Non-Unknown",     None,       None),  # unknown tag -- invalid
        ("Unknown",         None,       None),  # unknown positive -- invalid
        ("drone",           "Drone",    None),
    ]
    ok = True
    for folder, want_pos, want_neg in cases:
        got_pos = _find_matching_tag(folder)
        got_neg = _parse_negative_folder(folder)
        # Disallow matching BOTH a positive tag and a negative target.
        if got_pos is not None and got_neg is not None:
            print(f"  FAIL  {folder!r}: ambiguous match (pos={got_pos!r}, neg={got_neg!r})")
            ok = False
            continue
        if got_pos != want_pos or got_neg != want_neg:
            print(f"  FAIL  {folder!r}: expected pos={want_pos!r} neg={want_neg!r}, "
                  f"got pos={got_pos!r} neg={got_neg!r}")
            ok = False
            continue
        print(f"  ok    {folder!r}  -> pos={got_pos!r}  neg={got_neg!r}")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
