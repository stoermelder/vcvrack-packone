"""Reads the Siren tag manifest JSON and provides both Python and C++ access.

The C++ plugin treats `res/data/SirenTags.json` as the single
source of truth at runtime — it reads it once at module startup with
`rack::asset::plugin(...)` and rebuilds `STARTER_TAGS` from it. The Python
training pipeline reads the same JSON here and exposes the same list as
`CLASS_NAMES` so `features.py`, `train_model.py`, `classify_wav.py`, and
`emit_cpp.py` all agree.

When you edit `SirenTags.json`, both worlds pick it up automatically —
C++ on next module load, Python on next import of this file (or on the
next `bash run.sh`).
"""
from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

# Path to the canonical manifest. Resolved relative to the script folder
# so the script can be run from anywhere.
SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent.parent
MANIFEST_PATH = REPO_ROOT / "res" / "data" / "SirenTags.json"


@dataclass(frozen=True)
class TagInfo:
    name: str
    category: str  # one of: "source", "role", "time", "timbre"
    description: str
    classifier_can_suggest: bool


def load_manifest(path: Path = MANIFEST_PATH) -> tuple[int, list[TagInfo]]:
    """Return (version, [TagInfo]) from the manifest JSON.

    The file is read fresh every call so editing the JSON and re-running
    the trainer Just Works without re-importing Python.
    """
    with open(path) as f:
        data = json.load(f)
    version = int(data.get("version", 1))
    tags: list[TagInfo] = []
    for entry in data["tags"]:
        tags.append(
            TagInfo(
                name=entry["name"],
                category=entry.get("category", "source"),
                description=entry.get("description", ""),
                classifier_can_suggest=bool(entry.get("classifier_can_suggest", True)),
            )
        )
    return version, tags


# ── Module-level convenience: CLASS_NAMES and FEATURE_NAMES ──────────────
#
# The rest of the Python pipeline (features.py, train_model.py, classify_wav.py,
# emit_cpp.py) imports these names without knowing about the JSON.

def _reload() -> None:
    """Re-read the manifest and refresh module-level globals."""
    global MANIFEST_VERSION, TAGS, CLASS_NAMES, NUM_CLASSES
    MANIFEST_VERSION, TAGS = load_manifest()
    # CLASS_NAMES is the order the model emits: alphabetical by name, which
    # is also the order the JSON lists them. The C++ side reads the JSON
    # in the same order, so the indices match end-to-end.
    CLASS_NAMES = [t.name for t in TAGS]
    NUM_CLASSES = len(CLASS_NAMES)


# Populate on import.
MANIFEST_VERSION, TAGS = load_manifest()
CLASS_NAMES: list[str] = [t.name for t in TAGS]
NUM_CLASSES: int = len(CLASS_NAMES)


def refresh() -> None:
    """Public reload hook. Call this from `run.sh` after editing the JSON."""
    _reload()


if __name__ == "__main__":
    # Quick CLI: print the current state of the manifest.
    refresh()
    print(f"── SirenTags.json (version {MANIFEST_VERSION}, {NUM_CLASSES} tags) ──")
    for i, t in enumerate(TAGS):
        flag = "✓" if t.classifier_can_suggest else "·"
        print(f"  [{i:2d}] {flag} {t.name:12s}  ({t.category:7s})  {t.description}")
    print()
    print(f"CLASS_NAMES = {CLASS_NAMES}")
    print(f"NUM_CLASSES = {NUM_CLASSES}")
