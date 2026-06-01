# Siren tag-classifier training pipeline

This folder generates the tiny C function that Siren's "Suggest tags…" action
calls to score audio. It is **not** shipped to users — it runs once on the
developer's machine and the output is pasted into the plugin's source.

The plan this implements is in [`docs/siren/Classifier.md`](../../../docs/siren/Classifier.md). TL;DR: a small `scikit-learn` `RandomForest` is trained on a labeled dataset of 10 audio features, then transpiled to a plain C function with [`m2cgen`](https://github.com/BayesWitnesses/m2cgen). The function is checked into `src/modules/Siren/SirenTagClassifier.hpp`, so the plugin ships with **zero model file, zero JSON loader, and zero new runtime dependency**.

---

## The 15-tag vocabulary

The single source of truth for the tag list is
[`res/data/SirenTags.json`](../../res/data/SirenTags.json).
It is read by:

- the **C++ plugin** at module load time, via
  `Siren::starterTags()` in `SirenMetadata.hpp` (uses jansson + the bundled
  `res/` directory),
- the **Python pipeline** at import time, via
  `tag_manifest.py`.

To add, remove, or rename a tag: edit `SirenTags.json`, then re-run
`bash run.sh`. The Python pipeline picks it up on next import; the C++
plugin picks it up on next module load (no recompile needed if the count
stays the same).

Current vocabulary (alphabetical), 4 categories:

| Tag        | Category | Notes |
|------------|----------|-------|
| bass       | role     | Sub-bass / low-register |
| bright     | timbre   | High spectral centroid |
| dark       | timbre   | Low spectral centroid |
| drone      | source   | Sustained, slowly evolving |
| field      | source   | Environmental recording |
| lead       | role     | Melodic, mid/high register |
| loop       | time     | Cleanly cyclical |
| noise      | source   | Broadband, non-tonal |
| one-shot   | time     | Single transient event |
| pad        | role     | Sustained harmonic bed |
| percussion | source   | Clear onsets (drum, cymbal) |
| stab       | role     | Short pitched chord hit |
| texture    | source   | Non-pitched evolving material |
| tonal      | timbre   | Clear sense of pitch |
| vocal      | source   | Voice / formant tones |

---

## The 10 features

All features are extracted from the first 30 seconds of audio, decimated to
~8820 Hz before analysis (STFT: `FFT_SIZE=512`, `HOP=128`, Hann window, 4×
overlap). All values are clamped to `[0, 1]`. Order is the contract between
the C++ runtime and the Python training script — both must agree exactly.

| # | Name | What it captures |
|---|------|-----------------|
| 0 | `spectral_centroid`  | Brightness — power-weighted mean frequency / Nyquist |
| 1 | `spectral_rolloff85` | Upper spectral extent — freq below which 85 % of energy lies / Nyquist |
| 2 | `zero_crossing_rate` | Noisiness — fraction of sign changes per frame, averaged |
| 3 | `rms`                | Loudness / density |
| 4 | `onset_density`      | Rhythmic activity — spectral-flux peaks per second, / 30 |
| 5 | `low_band_ratio`     | Bass content — energy below 250 Hz / total |
| 6 | `spectral_flatness`  | Tonality — geometric / arithmetic mean of magnitude per frame |
| 7 | `spectral_bandwidth` | Frequency spread — power-weighted std dev / Nyquist |
| 8 | `high_band_ratio`    | Brightness detail — energy above 2000 Hz / total |
| 9 | `mean_spectral_flux` | Overall spectral change rate — mean log-domain half-rectified flux |

The feature contract is defined in `feature_config.py` (`FEATURE_NAMES`) and
implemented in `src/modules/Siren/SirenTagClassifierAPI.hpp`
(`TagClassifier::extractFeatures()`). **The C++ implementation is the
authoritative source**; `features.py` is the fallback used when the C++
extractor binary is not available.

---

## One-time setup

You need Python 3.10+ and a C++ compiler (`c++`/`clang++`) on your `PATH`
(macOS, Linux, WSL all fine). `run.sh` handles everything else.

```bash
cd scripts/siren-tag-model
bash run.sh
```

This will:

1. Create `.venv/` and install `numpy`, `scikit-learn`, `m2cgen`, `soundfile`.
2. Build the C++ feature extractor (`build/siren_extract_features`).
3. Generate a synthetic labeled dataset (15 classes × 80 clips by default).
4. Train a small Random Forest.
5. Print test-set metrics + a smoke test.
6. Write `build/SirenTagClassifier.generated.hpp`.

The whole thing takes ~3 minutes the first time and ~15 seconds on re-runs.

If the C++ build fails (e.g. missing Rack SDK), a warning is printed and
Python feature extraction is used automatically — results will still be
correct, but less efficient.

## Building the C++ extractor separately

```bash
cd scripts/siren-tag-model
make                  # builds build/siren_extract_features
make clean            # removes the binary and pffft.o
```

Override compiler or Rack path if needed:

```bash
make CXX=clang++ CC=clang RACK_DIR=/path/to/Rack
```

The binary is standalone — it only needs the drlibs and pffft headers from
the plugin/Rack tree. It has no dependency on `<rack.hpp>` or any other
Rack runtime.

## Paste the result into the plugin

Open the generated file and copy the whole thing. In
`src/modules/Siren/SirenTagClassifier.hpp` there is a marked region:

```cpp
// ─── BEGIN GENERATED MODEL (do not edit by hand) ────────────────────────
//   1. bash scripts/siren-tag-model/run.sh
//   2. paste the contents of scripts/siren-tag-model/build/SirenTagClassifier.generated.hpp here
//   3. rebuild with `make`
// ─── END GENERATED MODEL ────────────────────────────────────────────────
```

Paste between the BEGIN / END markers and rebuild:

```bash
cd ../..   # back to the plugin root
make
```

## Re-running

```bash
# Re-train on the same synthetic dataset (more clips per class)
bash run.sh --n-per-class 200

# Use a different number of trees / depth
python3 train_model.py --csv build/synthetic_dataset.csv --n-estimators 64 --max-depth 12
```

After pasting the new generated body, bump `MODEL_VERSION` in
`feature_config.py` and rebuild. The version number is exported as
`SIREN_TAG_MODEL_VERSION` in the C header — useful for diagnostics.

## Classifying a single file (Python smoke test)

```bash
# Score one of the synthetic clips
python3 classify_wav.py build/synthetic_audio/drone_0010.wav

# Try a real wav from anywhere
python3 classify_wav.py ~/samples/kick.wav

# Just dump the 10 features (skip the model step entirely)
python3 classify_wav.py ~/samples/kick.wav --no-model

# Show top 5 instead of top 3
python3 classify_wav.py build/synthetic_audio/percussion_0020.wav --top-k 5
```

For a quick feature dump from the C++ binary:

```bash
build/siren_extract_features build/synthetic_audio/percussion_0020.wav
```

Note: `classify_wav.py` uses the Python `extract_features()` function. For
a coherence check — verifying that training and inference agree — run the
C++ binary on a file and compare its output to `classify_wav.py --no-model`
on the same file; the 10 feature values should be identical.

## Training on your own audio

The pipeline ships with a small **synthetic** dataset so the end-to-end
flow works without any real labels. To replace it with your own audio,
organize clips into a folder tree, one subdirectory per tag:

```
my_samples/
    drone/         # folder name MUST match a tag in SirenTags.json
        A3_drone.wav
        sub_pad.wav
    bass/
        sub_bass_001.wav
    lead/
        melody_*.wav
```

Then:

```bash
# 1. Build a CSV (uses the C++ extractor if available):
python3 load_folder_dataset.py my_samples --out build/my_samples.csv

# 2. Train on it:
bash run.sh --csv build/my_samples.csv
```

### The folder-name → tag convention

Each subdirectory name must match one of the 15 entries in
`res/data/SirenTags.json` (case-insensitive). Anything that doesn't match is
skipped with a warning. To see the accepted names:

```bash
python3 load_folder_dataset.py --list-known-tags
```

Recognized audio extensions: `.wav`, `.flac`, `.mp3`, `.ogg`, `.aif`,
`.aiff`, `.aifc`. Multi-channel files are mixed to mono automatically.

### The CSV row format

Both `load_folder_dataset.py` and `generate_synthetic_dataset.py` write:

```
path,label,f0,f1,f2,f3,f4,f5,f6,f7,f8,f9
my_samples/bass/sub001.wav,bass,0.031,0.035,0.031,1.0,0.0,1.0,0.12,0.18,0.05,0.22
```

- `path` — relative path to the audio file.
- `label` — one tag name per row, exactly as in the manifest.
- `f0` … `f9` — the 10 features in contract order (see table above).
  Each is in `[0, 1]`.

### Class balancing

```bash
# Cap each tag at 200 clips
python3 load_folder_dataset.py my_samples --max-per-class 200 --out build/my.csv
```

### How many clips do I need?

Empirical guide for the current 10-feature Random Forest on 15 classes:

| Clips per class | Top-1 F1 (real audio) | Notes |
|-----------------|----------------------|-------|
| 20–50           | ~50–65%              | Barely better than random. Useful as a first pass. |
| 50–200          | ~65–80%              | Practical "good enough" range for a synth sample browser. |
| 200–1000        | ~80–90%              | Diminishing returns past ~500. |

With 10 features the model can separate classes better than the old 6-feature
version, but it also needs slightly more data to avoid overfitting.

### Where to source real audio

- **Your existing Siren user-tagged files** — every sample you have tagged in
  the browser is a free labeled example.
- **Freesound** ([freesound.org](https://freesound.org)) — search by keyword,
  filter to Creative Commons, download 50–200 per category.
- **Synthesizer preset packs** — WAV previews organized by category are ideal
  because the metadata is already curated.

---

## Files in this folder

| File | Purpose |
|------|---------|
| `siren_extract_features.cpp` | **C++ feature extractor CLI.** Takes audio file paths as args, outputs `path,f0..f9` CSV to stdout. Calls the same `TagClassifier::extractFeatures()` as the plugin — this is the authoritative feature extraction. |
| `Makefile` | Builds `siren_extract_features` from the above source. `make` / `make clean`. |
| `tag_manifest.py` | Reads `res/data/SirenTags.json`; exposes `CLASS_NAMES`, `TAGS`, `NUM_CLASSES`. Python-side source of truth for the tag vocabulary. |
| `feature_config.py` | The 10-feature contract (`FEATURE_NAMES`) + re-exports from `tag_manifest`. |
| `features.py` | Python feature extractor (fallback when the C++ binary is not built) + `find_cpp_extractor()` / `extract_features_batch()` helpers used by the dataset scripts. |
| `generate_synthetic_dataset.py` | Synthesizes 15 types of audio and writes a CSV. Uses the C++ extractor if available, Python fallback otherwise. |
| `load_folder_dataset.py` | Walks a folder of tag-named subdirectories, extracts features, writes a CSV. Bridge to real data. |
| `train_model.py` | Fits a Random Forest, prints metrics, calls `emit_cpp.py`. |
| `emit_cpp.py` | Uses `m2cgen` to write the C header fragment. |
| `classify_wav.py` | Python smoke-test tool: score a single wav file. |
| `run.sh` | One-shot convenience wrapper: build extractor → venv → dataset → train → emit. |
| `requirements.txt` | Pinned Python deps. |
| `build/` | Output directory — binary, WAV clips, CSV, and generated header all land here. |
| `.venv/`, `__pycache__/` | Cached, gitignored. |

---

## Troubleshooting

**`ImportError: No module named 'm2cgen'`** — re-run `bash run.sh`; it
installs deps into the local venv.

**`m2cgen` RecursionError** — your forest is too deep. Pass
`--max-depth 6` to `train_model.py`.

**Generated body is very large** — reduce `--n-estimators` to 16 or
`--max-depth` to 6. A useful model needs only ~1–5 KB of generated code.

**"warn: C++ build failed"** — the C++ extractor could not be compiled.
Python feature extraction is used automatically. Common causes: `c++` not
on `PATH`, or `RACK_DIR` pointing at the wrong location. Fix with
`make RACK_DIR=/path/to/Rack` or set the `RACK_DIR` environment variable.

**Smoke-test predictions look random** — the Python and C++ extractors
disagree. Both must use the same STFT parameters (`FFT_SIZE=512`,
`HOP=128`, `TARGET_SR=8820`) and the same 10 features in the same order.
Run `build/siren_extract_features <file>` and
`python3 classify_wav.py <file> --no-model` on the same file; the feature
values should match.

**The plugin's predictions differ from the Python smoke test** — same root
cause as above. Rebuild the C++ extractor with `make` and re-run
`bash run.sh` to regenerate the model using the C++ feature values.

**Trainer reports F1 = 0 on classes you know are present** — the CSV has
fewer than 15 labels or a folder name doesn't match the manifest. Check
folder names against `python3 load_folder_dataset.py --list-known-tags`.

**"skip: cannot decode foo.mp3"** — the file is corrupt or uses an
unsupported codec. Re-encode with `ffmpeg -i foo.mp3 -ar 22050 foo.wav`.

## When you're done

After pasting the generated body you can delete this folder from your
working copy — the plugin is self-contained. Keep it in the repo so future
maintainers can regenerate the model when the training data improves.
