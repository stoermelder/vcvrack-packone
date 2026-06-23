"""Bridge to the C++ feature extractor binary (siren_extract_features).

Feature extraction is implemented in C++ (SirenTagClassifierAPI.hpp) and
exposed here via a subprocess call so the training pipeline uses the exact
same code as the plugin runtime.

Transcoding fallback
--------------------
drwav (used by the C++ extractor) can open a WAV header but fail to decode
compressed encodings (ADPCM, mu-law, IMA, etc.), returning all-zero features.
When that happens, this module re-reads the file with soundfile (which handles
a much wider range of encodings via libsndfile) and writes a temporary PCM WAV,
then retries extraction on that temp file. The caller sees a clean result; the
temp file is deleted immediately after.
"""
from __future__ import annotations

import subprocess
import sys
import tempfile
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


# Mirrors the tag-classifier STFT constants in SirenTagClassifierApi.hpp
# (the TAG_* params used by extractFeatures(), not the BPM detector's).
_TARGET_SR  = 22050
_FFT_SIZE   = 1024
_HOP        = 256
_MIN_HOPS   = 4

# Minimum original-sample frames needed to produce _MIN_HOPS STFT hops after
# decimation, plus a small safety margin.  For a 44100 Hz file this is ~0.11 s.
def _min_frames_for_sr(samplerate: int) -> int:
    decim_rate  = max(1, samplerate // _TARGET_SR)
    decim_needed = _FFT_SIZE + (_MIN_HOPS - 1) * _HOP  # 896 decimated samples
    return (decim_needed + 1) * decim_rate              # back to original SR


def _write_temp_pcm_wav(data: np.ndarray, samplerate: int) -> Optional[str]:
    """Write a mono float array to a temporary 16-bit PCM WAV.

    Tiles audio that is too short to produce enough STFT hops in the C++
    extractor (_MIN_HOPS hops, roughly 0.1 s). Short one-shot samples are
    looped rather than zero-padded so their spectral character is preserved.
    The tiled version matches what extractFeatures() would see if the plugin
    received the sample looped.

    Returns the temp file path on success, None on failure. The caller is
    responsible for deleting the temp file.
    """
    try:
        import os
        import soundfile as sf
        arr = np.asarray(data, dtype=np.float32)
        if arr.ndim == 1:
            arr = arr.reshape(-1, 1)
        # Tile only if too short for the STFT minimum — don't inflate longer files.
        min_frames = _min_frames_for_sr(samplerate)
        if 0 < len(arr) < min_frames:
            repeats = -(-min_frames // len(arr))  # ceiling division
            arr = np.tile(arr, (repeats, 1))[:min_frames]
        fd, tmp_path = tempfile.mkstemp(suffix=".wav")
        os.close(fd)
        sf.write(tmp_path, arr, samplerate, subtype="PCM_16")
        return tmp_path
    except Exception:
        return None


def _transcode_to_pcm_wav(src: str) -> Optional[str]:
    """Re-encode src to a temporary 16-bit PCM WAV using soundfile.

    Used as a fallback for encodings drwav can't decode (ADPCM, mu-law, ...).
    Returns the temp file path on success, None if soundfile can't open it.
    The caller is responsible for deleting the temp file.
    """
    try:
        import soundfile as sf
        data, samplerate = sf.read(src, dtype="float32", always_2d=True)
    except Exception:
        return None
    # Mix down to mono the same way the C++ extractor does (simple mean).
    if data.shape[1] > 1:
        data = data.mean(axis=1)
    else:
        data = data[:, 0]
    return _write_temp_pcm_wav(data, samplerate)


def extract_features_for_arrays(
    items: List[tuple],
    binary: Optional[str] = None,
) -> Dict[str, Optional[np.ndarray]]:
    """Extract features for in-memory waveforms.

    `items` is a list of `(key, mono_float_array, samplerate)` tuples. Each
    array is written to a temporary PCM WAV and run through the same C++
    extractor as on-disk files, so augmented audio produces features identical
    to what the plugin would compute. Returns `{key: feature_array | None}`.

    Keys must be unique within the call. Temp files are always cleaned up.
    """
    if binary is None:
        binary = find_cpp_extractor()
    out: Dict[str, Optional[np.ndarray]] = {key: None for key, _, _ in items}
    tmp_to_key: Dict[str, str] = {}
    for key, data, sr in items:
        tmp = _write_temp_pcm_wav(data, sr)
        if tmp is not None:
            tmp_to_key[tmp] = key
    if not tmp_to_key:
        return out
    try:
        res = extract_features_batch(list(tmp_to_key.keys()), binary=binary)
        for tmp, key in tmp_to_key.items():
            feat = res.get(tmp)
            if feat is not None and not np.all(feat == 0):
                out[key] = feat
    finally:
        import os
        for tmp in tmp_to_key:
            try:
                os.unlink(tmp)
            except OSError:
                pass
    return out


def extract_features_batch(
    paths: List[str],
    binary: Optional[str] = None,
) -> Dict[str, Optional[np.ndarray]]:
    """Extract features for a list of audio file paths using the C++ extractor.

    Returns {path: feature_array} for each successfully processed path;
    value is None for files the extractor could not decode even after the
    soundfile transcoding fallback.
    Raises FileNotFoundError if the binary is not built.
    """
    from feature_config import NUM_FEATURES

    if binary is None:
        binary = find_cpp_extractor()

    result: Dict[str, Optional[np.ndarray]] = {p: None for p in paths}
    if not paths:
        return result

    def _run_extractor(path_list: List[str]) -> Dict[str, Optional[np.ndarray]]:
        """Run the binary on a flat list of paths; return {path: array|None}."""
        out: Dict[str, Optional[np.ndarray]] = {}
        for batch_start in range(0, len(path_list), _BATCH_SIZE):
            batch = path_list[batch_start:batch_start + _BATCH_SIZE]
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
                    out[path] = np.array([float(v) for v in vals], dtype=np.float32)
                except ValueError:
                    pass
            if proc.returncode != 0 and proc.stderr:
                print(f"  extractor stderr: {proc.stderr[:300]}", file=sys.stderr)
        return out

    # First pass: extract everything.
    first_pass = _run_extractor(list(paths))

    # Second pass: retry all-zero results via soundfile transcoding.
    retry_map: Dict[str, str] = {}  # original_path -> temp_path
    for orig in paths:
        feat = first_pass.get(orig)
        if feat is None or np.all(feat == 0):
            tmp = _transcode_to_pcm_wav(orig)
            if tmp is not None:
                retry_map[orig] = tmp
            elif feat is None:
                pass  # stays None
            else:
                print(
                    f"  skip: all-zero features and soundfile fallback failed for "
                    f"{Path(orig).name}",
                    file=sys.stderr,
                )

    if retry_map:
        print(
            f"  transcoding {len(retry_map)} file(s) with unsupported encoding "
            f"and retrying ...",
            file=sys.stderr,
        )
        retry_results = _run_extractor(list(retry_map.values()))
        for orig, tmp in retry_map.items():
            feat = retry_results.get(tmp)
            if feat is not None and not np.all(feat == 0):
                first_pass[orig] = feat
                print(f"  transcoded OK: {Path(orig).name}", file=sys.stderr)
            else:
                print(
                    f"  skip: extraction still failed after transcoding "
                    f"{Path(orig).name}",
                    file=sys.stderr,
                )
            try:
                import os; os.unlink(tmp)
            except OSError:
                pass

    for orig in paths:
        feat = first_pass.get(orig)
        if feat is not None and not np.all(feat == 0):
            result[orig] = feat

    return result
