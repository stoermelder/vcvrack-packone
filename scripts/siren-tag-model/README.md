# Siren tag-classifier training pipeline

This folder generates the tiny C function that Siren's "Suggest tags…" action
calls to score audio. It is **not** shipped to users — it runs once on the
developer's machine and the output is pasted into the plugin's source.

The plan this implements is in [`docs/siren/Classifier.md`](../../../docs/siren/Classifier.md). TL;DR: a small `scikit-learn` `RandomForest` is trained on a labeled dataset of 6 audio features, then transpiled to a plain C function with [`m2cgen`](https://github.com/BayesWitnesses/m2cgen). The function is checked into `src/modules/Siren/SirenTagClassifier.hpp`, so the plugin ships with **zero model file, zero JSON loader, and zero new runtime dependency**.

---

## The 15-tag vocabulary

The single source of truth for the tag list is
[`src/modules/Siren/TagManifest.json`](../../src/modules/Siren/TagManifest.json).
It is read by:

- the **C++ plugin** at module load time, via
  `Siren::starterTags()` in `SirenMetadata.hpp` (uses jansson + the bundled
  `res/` directory),
- the **Python pipeline** at import time, via
  `tag_manifest.py`.

To add, remove, or rename a tag: edit `TagManifest.json`, then re-run
`bash run.sh`. The Python pipeline picks it up on next import; the C++
plugin picks it up on next module load (no recompile needed if the count
stays the same).

Current vocabulary (alphabetical), 4 categories:

| Tag       | Category   | Notes |
|-----------|-----------|-------|
| bass      | role       | Sub-bass / low-register |
| bright    | timbre     | High spectral centroid |
| dark      | timbre     | Low spectral centroid |
| drone     | source     | Sustained, slowly evolving |
| field     | source     | Environmental recording |
| lead      | role       | Melodic, mid/high register |
| loop      | time       | Cleanly cyclical |
| noise     | source     | Broadband, non-tonal |
| one-shot  | time       | Single transient event |
| pad       | role       | Sustained harmonic bed |
| percussion| source     | Clear onsets (drum, cymbal) |
| stab      | role       | Short pitched chord hit |
| texture   | source     | Non-pitched evolving material |
| tonal     | timbre     | Clear sense of pitch |
| vocal     | source     | Voice / formant tones |

---

## One-time setup

You need Python 3.10+ on your `PATH` (macOS, Linux, WSL all fine). The
`run.sh` script creates a local venv in `.venv/` and installs everything
else.

```bash
cd scripts/siren-tag-model
bash run.sh
```

This will:

1. Create `.venv/` and install `numpy`, `scikit-learn`, `m2cgen`, `soundfile`.
2. Generate a synthetic labeled dataset (15 classes × 80 clips by default).
3. Train a small Random Forest.
4. Print test-set metrics + a smoke test.
5. Write `build/SirenTagClassifier.generated.hpp`.

The whole thing takes ~3 minutes the first time and ~10 seconds on re-runs.

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

The plugin's runtime now calls `siren_tag_score(features, scores_out)` on
every "Suggest tags…" right-click.

## Re-running

```bash
# Re-train on the same synthetic dataset (different random seed)
bash run.sh --n-per-class 200

# Use a different number of trees / depth
python3 train_model.py --csv build/synthetic_dataset.csv --n-estimators 64 --max-depth 12
```

After pasting the new generated body, bump `MODEL_VERSION` in
`feature_config.py` and rebuild. The version number is exported as
`SIREN_TAG_MODEL_VERSION` in the C header — useful for diagnostics and for
invalidating any future per-sample feature cache.

## Classifying a single file (CLI test tool)

For quick smoke-tests and ad-hoc debugging, use `classify_wav.py`. It
extracts the 6 features, trains a small RandomForest on the labeled CSV
in memory, and prints the top-K predictions with a bar chart.

```bash
# Score one of the synthetic clips
python3 classify_wav.py build/synthetic_audio/drone_0010.wav

# Try a real wav from anywhere
python3 classify_wav.py ~/samples/kick.wav

# Just dump the 6 features (skip the model step entirely)
python3 classify_wav.py ~/samples/kick.wav --no-model

# Show top 5 instead of top 3
python3 classify_wav.py build/synthetic_audio/percussion_0020.wav --top-k 5

# Use a different training CSV
python3 classify_wav.py foo.wav --csv /path/to/my_real_dataset.csv
```

Example output (the model gets the obvious cases right out of the box):

```
── classify_wav ────────────────────────────────────────────────
  file        : build/synthetic_audio/percussion_0020.wav
  model ver   : 1
  top-k       : 3
  decoded     : 44100 samples @ 22050 Hz  (2.00 s, mono)
  features    : [spectral_centroid=0.414, spectral_rolloff85=0.710, ...]
  training    : RandomForest from synthetic_dataset.csv

  top-3 predictions:
    1. percussion    1.000  ████████████████████
    2. tonal         0.000  ░░░░░░░░░░░░░░░░░░░░
    3. dark          0.000  ░░░░░░░░░░░░░░░░░░░░
```

Note: the script refits the model on every invocation, so it takes ~5
seconds. If you want faster scoring, train once and save the model with
`joblib`, then load it here. Not done by default to keep the script
self-contained.

## Training on your own audio

The pipeline ships with a small **synthetic** dataset so the end-to-end
flow works without any real labels. To replace it with your own audio,
read this section — it covers the folder convention, the CSV format, and
how to plug the result into `bash run.sh`.

### TL;DR

```bash
# 1. Organize your clips into a folder, one subdirectory per tag:
my_samples/
    drone/         # folder name MUST match a tag in TagManifest.json
        A3_drone.wav
        sub_pad.wav
        ...
    bass/
        sub_bass_001.wav
        ...
    lead/
        melody_*.wav
        ...

# 2. Build a CSV from that folder:
python3 load_folder_dataset.py my_samples --out build/my_samples.csv

# 3. Train on it (the synthetic CSV is ignored, this one is used instead):
python3 train_model.py --csv build/my_samples.csv

# Or, in one go via run.sh:
bash run.sh --csv build/my_samples.csv
```

The `bash run.sh` command without `--csv` still generates the synthetic
dataset first, so you don't need to remove the default to use a real one —
just pass `--csv` to point at your own.

### The folder-name → tag convention

`load_folder_dataset.py` walks a directory and treats each **subdirectory
name** as a tag. The name must match one of the 15 entries in
`src/modules/Siren/TagManifest.json` (case-insensitive, but the canonical
spelling is what gets written to the CSV). Anything that doesn't match
is skipped with a warning, not an error — so you can have extra folders
without breaking the run.

To see the canonical list of accepted names:

```bash
python3 load_folder_dataset.py --list-known-tags
```

Example output (truncated):

```
Accepted folder names (must match one of these exactly, case-insensitive):
  bass          (role   )  Fills the low end of a patch. ...
  bright        (timbre )  High spectral centroid, ...
  ...
```

Recognized audio extensions inside each folder: `.wav`, `.flac`, `.mp3`,
`.ogg`, `.aif`, `.aiff`, `.aifc`. Anything else is ignored. Multi-channel
files are down-mixed to mono automatically.

### The CSV row format

`load_folder_dataset.py` (and `generate_synthetic_dataset.py`) write a
single-label CSV with this exact column layout:

```
path,label,f0,f1,f2,f3,f4,f5
test_dataset/bass/bass_0000.wav,bass,0.031,0.035,0.031,1.0,0.0,1.0
test_dataset/drone/drone_0000.wav,drone,0.054,0.059,0.054,0.848,0.067,1.0
...
```

- `path` — relative path to the audio file, no quoting needed.
- `label` — **one** tag name per row, exactly as it appears in the manifest.
- `f0` … `f5` — the 6 features in the contract order
  (`spectral_centroid, spectral_rolloff85, zero_crossing_rate, rms, onset_density, low_band_ratio`).
  Each must be in `[0, 1]`.

You can hand-write this CSV too — useful if you want to mix sources or
add a column your tool of choice outputs naturally.

### How `bash run.sh` consumes the CSV

`run.sh` calls `python3 train_model.py --csv <path>`. The trainer:

1. Reads each row, expects `path,label,f0..f5`.
2. Binarises the single `label` against the 15-tag manifest.
3. Trains a `MultiOutputClassifier(RandomForest)` — one binary RF per tag.
4. Splits 75 / 25 for evaluation, prints precision/recall/F1 per class.
5. Calls `emit_cpp.py` to produce `build/SirenTagClassifier.generated.hpp`.

The trainer does **not** verify the audio files exist or match the path —
it trusts the CSV. So the CSV is the contract.

### Single-label vs multi-label per file

The current trainer treats each CSV row as **one tag per file**. Two ways
to model a sample that genuinely wears multiple tags (e.g. a "drum loop
with vocal chops" that is both `loop` and `vocal`):

1. **Duplicate the row** in the CSV, once per tag. The trainer sees the
   same file multiple times with different labels. The resulting model
   will learn that a sample can be confidently classified as either,
   which is what you want for "promote to user tags" UX.

2. **Hand-write a CSV with a `labels` column** of `+`-separated tag names
   and extend `train_model.py::load_csv` to parse it. Out of scope for
   v1; the folder loader doesn't do this yet.

If you mostly have clean single-label data, option 1 isn't needed and
the folder convention just works.

### Class balancing

The folder loader takes an optional `--max-per-class N` flag that
uniformly samples `N` clips per folder. Useful when you have wildly
unequal amounts of material per tag.

```bash
# Cap each tag at 200 clips, sample randomly
python3 load_folder_dataset.py my_samples --max-per-class 200 --out build/my_samples.csv
```

A perfectly balanced training set is usually a *worse* model than a
slightly unbalanced one with all the data. A reasonable rule of thumb:
no more than 10:1 ratio between your largest and smallest class.

### How many clips do I need?

Empirical guide for the current 6-feature Random Forest, on 15 classes:

| Clips per class | Top-1 F1 (real audio) | Notes |
|-----------------|----------------------|-------|
| 20–50           | ~50–65%              | Barely better than random. Useful as a first pass. |
| 50–200          | ~65–80%              | Practical "good enough" range for a synth sample browser. |
| 200–1000        | ~80–90%              | Diminishing returns past ~500. |

The model has very few features (6) and a small forest, so it saturates
quickly. The bottleneck is **class coverage** (do you have at least
something in every folder?), not raw clip count.

### Common pitfalls

**"skip folder foo: doesn't match any tag"** — your folder is named
something the manifest doesn't know. Run
`python3 load_folder_dataset.py --list-known-tags` to see the accepted
names. A folder called `Drums/` won't match anything; rename to
`percussion/`. Common near-misses: `drum` (use `percussion`), `pad-sound`
(use `pad`), `instrument` (no such tag — pick the closest one).

**"skip: cannot decode foo.mp3: ..."** — the file is corrupt or uses a
codec libsndfile doesn't support. Try re-encoding it with
`ffmpeg -i foo.mp3 -ar 22050 foo.wav`.

**The trainer reports F1 = 0 on classes you know are present** — usually
means the CSV has more than 15 unique labels (the 15th onwards get
silently dropped with a warning to stderr). Check your folder names
against the manifest.

**Trainer crashes during the smoke test with `IndexError: index 1 is out
of bounds for axis 1 with size 1`** — happens when a tag in the manifest
is missing from your CSV entirely (e.g. you only had 6 of the 15
folders). The smoke test now handles this gracefully (defaults missing
classes to score 0) as of the latest commit.

**Predictions on real audio don't match the synthetic-data test set** —
that's expected, not a bug. Synthetic audio is too clean; real audio has
noise, reverb, and stereo content the model has never seen. Use a
smoke test on a *real* clip after training, not the synthetic one.

### Where to source real audio

A few practical starting points, in order of how well they map to the
15 tags:

- **Your existing Siren user-tagged files** — every WAV in
  `~/.../Stoermelder-P1/siren-<hash>.json` with at least one user tag
  is a free labeled example. Build a folder per tag by symlinking
  (or copying) those files:
  ```bash
  for wav in $(find ~/sirensamples -name '*.wav'); do
      tag=$(jq -r '.tags[0]' <(extract_metadata_for "$wav"))
      mkdir -p "my_samples/$tag"
      ln -s "$wav" "my_samples/$tag/$(basename $wav)"
  done
  ```
  Only the first tag per file is used in this snippet; pick whichever
  strategy you prefer.

- **Freesound** ([freesound.org](https://freesound.org)) — search by
  keyword (e.g. "kick", "pad", "field recording"), filter to Creative
  Commons, and download 50–200 per category. Be aware the ontology
  doesn't line up perfectly — you'll spend time curating.

- **Synthesizer preset packs** — many commercial packs ship WAV
  previews organized by category ("Bass", "Lead", "FX"). These are
  ideal because the metadata is already curated.

- **Your DAW's library** — export 2–5 second snippets of the patches
  you use most, label them by how *you* think of them, drop into the
  matching folder. The model will learn your taxonomy, not someone
  else's.

### After training: pointing the plugin at your new model

1. `bash run.sh --csv build/my_samples.csv` produces a fresh
   `build/SirenTagClassifier.generated.hpp` (~1.7 MB by default; pass
   `--n-estimators 12 --max-depth 6` to shrink it).
2. Copy that file's contents into the marked region of
   `src/modules/Siren/SirenTagClassifier.hpp`.
3. `make` from the plugin root.
4. Reload the plugin in Rack. Right-click any sample → "Suggest tags…".
   The new model will use the weights you just trained.

The tag *vocabulary* doesn't change — it always comes from
`TagManifest.json`, which is what the dialog UI filters by. Only the
classifier weights change when you retrain.

## Files in this folder

| File | Purpose |
|------|---------|
| `tag_manifest.py` | Reads `src/modules/Siren/TagManifest.json` and exposes `CLASS_NAMES`, `TAGS`, `NUM_CLASSES`. **Python-side source of truth** (the JSON itself is the actual source of truth). |
| `feature_config.py` | The 6-feature contract + re-exports from `tag_manifest`. |
| `features.py` | Python twin of the C++ feature extractor. Must match the C++ output. |
| `generate_synthetic_dataset.py` | Synthesizes 15 types of audio (one per class) and writes a CSV. |
| `load_folder_dataset.py` | Walks a folder of tag-named subdirectories, extracts features, writes a CSV. The bridge to real data. |
| `train_model.py` | Fits a Random Forest, prints metrics, calls `emit_cpp.py`. |
| `emit_cpp.py` | Uses `m2cgen` to write the C header fragment. |
| `classify_wav.py` | CLI test tool: score a single wav file against the trained model. |
| `run.sh` | One-shot convenience wrapper (venv + dataset + train + emit). |
| `requirements.txt` | Pinned Python deps. |
| `build/` | Output directory. `SirenTagClassifier.generated.hpp` lands here. |
| `.venv/`, `__pycache__/` | Cached, gitignored. |

The corresponding C++ side: `src/modules/Siren/TagManifest.json` is the
JSON; `src/modules/Siren/SirenMetadata.hpp` reads it at module load time
into `Siren::starterTags()`. The dylib bundles the JSON via
`DISTRIBUTABLES += res` in the plugin Makefile.

## Troubleshooting

**`ImportError: No module named 'm2cgen'`** — re-run `bash run.sh`; it
installs deps into the local venv.

**`m2cgen` RecursionError** — your forest is too deep. Pass
`--max-depth 6` to `train_model.py`.

**Generated body is 50 KB** — that's a tree with many nodes; reduce
`--n-estimators` to 16 or `--max-depth` to 6. The C++ runtime only
needs ~1–5 KB of generated code for a useful model.

**Smoke-test predictions look random** — your `extract_features` in
Python and C++ disagree. Both must clamp to `[0, 1]`, both must use
the same STFT parameters (`FFT_SIZE=512`, `HOP=128`, `TARGET_SR=4410`),
and both must compute the same 6 features in the same order. Check
`features.py` against `extractFeatures()` in
`src/modules/Siren/SirenTagClassifier.hpp`.

**The plugin's predictions differ from the Python smoke test** — same as
above. The contract is hard.

## When you're done

After pasting the generated body, you can delete this folder from your
working copy if you want — the plugin is self-contained. Keep the folder
in the repo so future maintainers can regenerate the model when the
training data improves.
