"""Bridge to the C++ feature extractor binary (siren_extract_features).

Feature extraction is implemented in C++ (SirenTagClassifierAPI.hpp) and
exposed here via a subprocess call so the training pipeline uses the exact
same code as the plugin runtime.
"""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path
from typing import Dict, List, Optional

import numpy as np

# ── C++ extractor bridge ──────────────────────────────────────────────────────

_SCRIPT_DIR = Path(__file__).parent
_BINARY_NAME = "siren_extract_features" + (".exe" if sys.platform == "win32" else "")
_BATCH_SIZE = 500  # max files per subprocess call (avoids ARG_MAX limits)


def find_cpp_extractor() -> str:
    """Return path to the compiled C++ extractor binary. Raises if not built."""
    binary = _SCRIPT_DIR / "build" / _BINARY_NAME
    if not binary.is_file():
        raise FileNotFoundError(
            f"C++ extractor not found at {binary}. "
            f"Run 'make' in {_SCRIPT_DIR} to build it."
        )
    return str(binary)


def extract_features_batch(
    paths: List[str],
    binary: Optional[str] = None,
) -> Dict[str, Optional[np.ndarray]]:
    """Extract features for a list of audio file paths using the C++ extractor.

    Returns {path: feature_array} for each successfully processed path;
    value is None for files the extractor could not decode.
    Raises FileNotFoundError if the binary is not built.
    """
    from feature_config import NUM_FEATURES

    if binary is None:
        binary = find_cpp_extractor()

    result: Dict[str, Optional[np.ndarray]] = {p: None for p in paths}
    if not paths:
        return result

    for batch_start in range(0, len(paths), _BATCH_SIZE):
        batch = paths[batch_start:batch_start + _BATCH_SIZE]
        proc = subprocess.run(
            [binary, *batch],
            capture_output=True, text=True, check=False,
        )
        for line in proc.stdout.splitlines()[1:]:  # skip header
            parts = line.split(",")
            if len(parts) < NUM_FEATURES + 1:
                continue
            path, vals = parts[0], parts[1:NUM_FEATURES + 1]
            try:
                result[path] = np.array([float(v) for v in vals], dtype=np.float32)
            except ValueError:
                pass
        if proc.returncode != 0 and proc.stderr:
            print(f"  extractor stderr: {proc.stderr[:300]}", file=sys.stderr)

    return result
