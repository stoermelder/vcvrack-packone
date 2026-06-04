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

from feature_config import NUM_FEATURES
from features import extract_features_batch, find_cpp_extractor  # raises if not built
from tag_manifest import CLASS_NAMES, TAGS

# Audio file extensions we recognize. soundfile / libsndfile handles these.
AUDIO_EXTENSIONS = {".wav", ".flac", ".mp3", ".ogg", ".aif", ".aiff", ".aifc"}


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
            w.writerow(["path", "label", *[f"f{i}" for i in range(NUM_FEATURES)]])
        w.writerows(rows)
    return len(rows)


def load_folder_dataset(
    root: Path,
    out_csv: Path,
    max_per_class: int | None = None,
    append: bool = False,
    seed: int = 42,
) -> dict[str, int]:
    """Walk `root`, find one subfolder per known tag, extract features, write CSV.

    Returns a dict {tag_name: row_count_written} for the caller's diagnostics.
    """
    if not root.exists() or not root.is_dir():
        raise FileNotFoundError(f"dataset root not found: {root}")

    rng = np.random.default_rng(seed)
    counts: dict[str, int] = {}

    # Phase 1: collect (file_path, label) pairs across all subfolders.
    pending: list[tuple[Path, str]] = []
    subdirs = sorted(p for p in root.iterdir() if p.is_dir())
    if not subdirs:
        print(f"warning: no subdirectories found in {root}", file=sys.stderr)

    for sub in subdirs:
        canonical = _find_matching_tag(sub.name)
        if canonical is None:
            print(
                f"  skip folder {sub.name!r}: doesn't match any tag in SirenTags.json "
                f"(known tags: {', '.join(t.name for t in TAGS)})",
                file=sys.stderr,
            )
            continue

        files = _list_audio_files(sub)
        if not files:
            print(f"  warn: {canonical!r} folder is empty ({sub})", file=sys.stderr)
            counts[canonical] = 0
            continue

        if max_per_class is not None and len(files) > max_per_class:
            idxs = rng.choice(len(files), size=max_per_class, replace=False)
            files = [files[i] for i in sorted(idxs)]

        for f in files:
            pending.append((f, canonical))
        counts[canonical] = len(files)
        print(f"  {canonical:12s}  {len(files):4d} clips from {sub}")

    # Phase 2: batch-extract features via C++ binary.
    binary = find_cpp_extractor()
    print(f"  Using C++ extractor: {binary}")
    features_by_path = extract_features_batch([str(f) for f, _ in pending], binary)

    # Phase 3: build rows.
    rows: list[list] = []
    for f, canonical in pending:
        features = features_by_path.get(str(f))
        if features is None:
            print(f"  skip: extraction failed for {f.name}", file=sys.stderr)
            continue
        try:
            rel = f.relative_to(root.parent)
        except ValueError:
            rel = f
        rows.append([str(rel), canonical, *features.tolist()])

    if not rows:
        raise RuntimeError(
            f"No usable audio found under {root}. Check that subdirectory names "
            f"match a known tag (e.g. drone, percussion, lead) and contain audio files."
        )

    n_written = _write_csv(rows, out_csv, append=append)
    print()
    print(f"Wrote {n_written} rows to {out_csv}")
    print(f"Per-class counts: {counts}")
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
    p.add_argument("--list-known-tags", action="store_true",
                   help="Print the list of accepted tag names and exit.")
    args = p.parse_args()

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
        load_folder_dataset(args.root, args.out, args.max_per_class, args.append)
    except (FileNotFoundError, RuntimeError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
