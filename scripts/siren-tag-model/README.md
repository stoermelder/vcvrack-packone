# Siren tag-classifier training pipeline

This folder generates the tiny C function that Siren's "Suggest tags…" action
calls to score audio. It is **not** shipped to users — it runs once on the
developer's machine and the output is pasted into the plugin's source.

> **Quick start (macOS / Linux / WSL):**
> ```bash
> cd scripts/siren-tag-model
> make pipeline                # full flow: venv → extractor → dataset → train
> ```
> On Windows without GNU make, use `run.bat` instead. The two flows are
> identical and produce the same output. See the ["`make` targets" section](#make-targets)
> below for everything else.

The plan this implements is in [`docs/siren/Classifier.md`](../../../docs/siren/Classifier.md). TL;DR: 18 independent `scikit-learn` `RandomForestClassifier` heads (one per binary tag in the manifest) are trained on a labeled dataset of **53 audio features**, then transpiled to a plain C source file with [`m2cgen`](https://github.com/BayesWitnesses/m2cgen). The result is pasted into `src/modules/siren/SirenTagClassifier.cpp`, so the plugin ships with **zero model file, zero JSON loader, and zero new runtime dependency**.

Per-(row, class) sample weights let you push back on individual tag heads
using a `Non-<Tag>/` folder convention — see the "Hard-negative folders"
section below for the full design.

---

## The 18-tag vocabulary

The single source of truth for the tag list is
[`res/data/SirenTags.json`](../../res/data/SirenTags.json).
It is read by:

- the **C++ plugin** at module load time, via
  `Siren::starterTags()` and `Siren::starterTagKeywords()` in
  `SirenMetadata.hpp` (uses jansson + the bundled `res/` directory),
- the **Python pipeline** at import time, via
  `tag_manifest.py`.

To add, remove, or rename a tag: edit `SirenTags.json`, then re-run
`bash run.sh`. The Python pipeline picks it up on next import; the C++
plugin picks it up on next module load (no recompile needed if the count
stays the same). The JSON also carries a `keywords` array per tag — those
keywords are matched against the filename stem in the browser pane
(`TagClassifier::applyFilenameBoosts`) to bump tag scores when names like
"kick" or "pad" are present.

Current vocabulary (alphabetical), 4 categories:

| Tag          | Category | Notes |
|--------------|----------|-------|
| Acoustic     | timbre   | Acoustic instrument character: natural harmonics, exponential decay |
| Atmospheric  | timbre   | Slowly evolving ambient texture: pads, washes, drones |
| Bass         | role     | Sub- or bass-register sustained or rhythmic material |
| Clap         | source   | Layered noise bursts with mid-frequency character |
| Cymbal       | source   | High-frequency metallic noise with longer decay |
| Drone        | source   | Sustained, slowly evolving pitch |
| Drums        | source   | Full drum pattern or kit loop |
| FX           | role     | Sweeps, risers, impacts, designed sounds |
| Glitch       | timbre   | Stuttering or fragmented audio: artifacts, stutter edits |
| HiHat        | source   | Short metallic noise burst, high-frequency transient |
| Kick         | source   | Low-frequency transient with punchy bass content |
| Lead         | role     | Melodic, mid/high register, often rhythmic |
| Loop         | time     | Cyclical material that loops cleanly |
| Nature       | source   | Environmental recording: wind, rain, outdoor ambience |
| Noise        | source   | Broadband noise without a clear tonal component |
| Pad          | role     | Sustained harmonic bed: chords or textures beneath a melody |
| Snare        | source   | Mid-frequency transient with noise component and sharp attack |
| Vocal        | source   | Voice or vocal-like material: sung voice, formant tones |

> The C++ `SirenMetadata.hpp::fallbackTags()` returns a separate 15-tag list
> (adds `One-Shot` and `Stab`, drops `Atmospheric`) used by the test harness
> and as a safety net when the JSON manifest can't be read at load time. It
> is **not** what the classifier scores against — only `SirenTags.json` is.

---

## The 53 features

All features are extracted from the first 30 seconds of audio, decimated to
`TARGET_SR ≈ 8820 Hz` before analysis (decim rate = `sampleRate / 8820`; e.g.
factor 5 for 44.1/48 kHz inputs; STFT: `FFT_SIZE=512`, `HOP=128`, Hann
window, 4× overlap). All values are clamped to `[0, 1]`. Order is the
contract between the C++ runtime and the Python training script — both must
agree exactly.

### Spectral — centroid family (STFT pass, per-hop averaged)

| #  | Name | What it captures |
|----|------|-----------------|
| 0  | `spectral_centroid`  | Brightness — power-weighted mean frequency / (Nyquist − 1) |
| 1  | `spectral_rolloff85` | Upper spectral extent — bin below which 85 % of energy lies / (Nyquist − 1) |
| 7  | `spectral_bandwidth` | Frequency spread — power-weighted std dev / (Nyquist − 1) |
| 14 | `spectral_slope`     | Pearson r(bin, mag) × 0.5 + 0.5: falling = 0, rising = 1 |
| 15 | `spectral_decrease`  | Spectral decrease (low-freq bias), `+ 0.5` offset |
| 16 | `spectral_skewness`  | 3rd standardised moment of spectrum, `/6 + 0.5` |
| 17 | `spectral_kurtosis`  | Excess 4th moment (peakedness), `(kurt − 3 + 3)/10` |

### Spectral — energy and texture

| #  | Name | What it captures |
|----|------|-----------------|
| 5  | `low_mid_ratio`     | Energy in `[80, 250) Hz` / total |
| 6  | `spectral_flatness` | Tonality — geometric / arithmetic mean of magnitude per frame |
| 8  | `high_band_ratio`   | Brightness detail — energy above 2000 Hz / total |
| 12 | `spectral_crest`    | `max(mag)/mean(mag)/HALF_N`: single-tone → 1, flat noise → ~0 |
| 13 | `spectral_entropy`  | Shannon entropy of PSD / log(N): noise → 1, tone → 0 |
| 37 | `sub_bass_ratio`    | Energy below 80 Hz / total: kick → high, bass → moderate, everything else → low |
| 38 | `mid_band_ratio`    | Energy `[250, 2000) Hz` / total: snare/clap/vocal → high, kick/sub-bass → low |

### Temporal dynamics

| #  | Name | What it captures |
|----|------|-----------------|
| 4  | `onset_density`      | Rhythmic activity — spectral-flux peaks per second, / 30 |
| 9  | `mean_spectral_flux` | Overall spectral change rate — mean log-domain half-rectified flux / 50 |
| 39 | `flux_variance`      | Std dev of per-hop spectral flux / 50: glitch/drums → high, drone/pad → low |

### Time-domain (computed before STFT)

| #  | Name | What it captures |
|----|------|-----------------|
| 2  | `zero_crossing_rate` | Noisiness — fraction of sign changes per frame, averaged |
| 3  | `rms`                | Loudness / density (normalised by 1/√2) |
| 10 | `crest_factor`       | Peak / RMS (log-normalised, capped 1): transients → high, sustained → low |
| 11 | `harmonic_ratio`     | Normalised autocorrelation peak over lags 8–180: pitched → high, noise → low |

### Temporal envelope (32 RMS blocks; computed before STFT)

| #  | Name | What it captures |
|----|------|-----------------|
| 31 | `temporal_centroid`  | Time-weighted centre of mass of RMS envelope: one-shot → 0, loop/pad → 0.5 |
| 32 | `tail_head_ratio`    | RMS(last 20%) / (RMS(first 20%) + RMS(last 20%)): one-shot → 0, loop → 0.5, fade-in → 1 |
| 33 | `env_ac_peak`        | Peak normalised autocorrelation of the 100 ms RMS envelope at 0.25–4 s lags: rhythmic loop → 1, one-shot → 0 |
| 34 | `attack_time`        | Block of peak-RMS position / 31: percussive (kick, snare) → 0, pad/drone → high |
| 35 | `env_rms_variance`   | Normalised std dev of RMS envelope blocks (σ×2, capped 1): sustained/flat → 0, rhythmic/one-shot → high |
| 36 | `temporal_entropy`   | Shannon entropy of normalised RMS envelope: one-shot → 0, drone/noise → 1 |

### MFCCs — timbral identity (mel filterbank → log → DCT)

26 triangular mel filters, 0–Nyquist. Log-compressed per hop, averaged, DCT-II applied.
C[0] is normalised by 200 (energy, wide range); C[1..12] by 30 (shape). Both mapped
to `[0, 1]` via `c/norm + 0.5`.

| #  | Names |
|----|-------|
| 18–30 | `mfcc_0` … `mfcc_12` (mean of log-mel) |
| 40–52 | `mfcc_delta_0` … `mfcc_delta_12` (mean absolute frame-to-frame difference, no `+0.5` offset) |

The MFCC deltas (40–52) are computed during the STFT pass itself — no second
forward FFT pass. They are the main reason the feature count grew from the
original 32; they add temporal-dynamics signal at very low cost.

The feature contract is defined in `feature_config.py` (`FEATURE_NAMES`) and
implemented in `src/modules/siren/SirenTagClassifierApi.hpp`
(`TagClassifier::extractFeatures()`). **The C++ implementation is the
authoritative source** — it is the one and only feature extractor, used by
both the plugin and the training pipeline.

---

## One-time setup

You need Python 3.10+ and a C++ compiler (`c++`/`clang++`) on your `PATH`
(macOS, Linux, WSL all fine). The `Makefile` handles everything else.

```bash
cd scripts/siren-tag-model
make pipeline                    # the full flow: venv → extractor → dataset → train
```

This will:

1. Create `.venv/` and install `numpy`, `scikit-learn`, `m2cgen`, `soundfile`
   (skipped on re-runs when the venv is up to date).
2. Build the C++ feature extractor (`build/siren_extract_features`).
3. Generate a synthetic labeled dataset (18 classes × 80 clips by default).
4. Train 18 independent Random Forest heads.
5. Print test-set metrics + a smoke test.
6. Write `build/SirenTagClassifier.generated.cpp`.

The whole thing takes ~3 minutes the first time and ~15 seconds on re-runs.

If the C++ build fails (e.g. missing Rack SDK), a warning is printed and
Python feature extraction is used automatically — results will still be
correct, but less efficient.

### `make` vs `run.sh` / `run.bat`

The `Makefile` is the **primary** workflow on macOS, Linux, and WSL. It is
incremental (skips work that's already done) and lets you override any
parameter on the command line:

```bash
make train CSV=build/my_samples.csv N_ESTIMATORS=60 NEGATIVE_WEIGHT=5.0
```

`run.sh` (macOS/Linux) and `run.bat` (Windows) are kept as **fallback
wrappers** for users who don't have GNU make on PATH (notably some Windows
setups without MSYS / Git-Bash). They delegate to the same Python scripts
and produce identical output. Pick whichever fits your environment.

## `make` targets

Run from this directory. All variables are optional and have sensible
defaults — call `make <target>` and the override what you need.

| Target | What it does | Required vars | Common overrides |
|---|---|---|---|
| `make extractor` | Build the C++ feature extractor (`build/siren_extract_features`). Same as the default `make`. | — | `CXX=clang++`, `CC=clang`, `RACK_DIR=/path/to/Rack` |
| `make venv` | Create `.venv/` and install `requirements.txt` (idempotent). | — | `PYTHON=python3.12` |
| `make dataset` | Generate the synthetic labeled dataset. | — | `N_PER_CLASS=200 SEED=7` |
| `make csv-from-folder` | Walk a `<Tag>/` + `Non-<Tag>/` folder tree, write a CSV. | `ROOT=path/to/dir` | `OUT=build/my.csv MAX_PER_CLASS=200` |
| `make train` | Train the model and emit `build/SirenTagClassifier.generated.cpp`. | `CSV=build/my_samples.csv` | `N_ESTIMATORS=60 MAX_DEPTH=8 AUGMENT=3 NEGATIVE_WEIGHT=5.0` |
| `make classify` | Score a single audio file against the trained model. | `WAV=path/to/snippet.wav` | `TOP_K=5 NO_MODEL=1` |
| `make pipeline` | End-to-end: `dataset` (or `csv-from-folder` if `ROOT` is set) → `train` → emit C++. Mirrors `run.sh`. | none — auto-detects mode | any of the above |
| `make self-test` | Run the loader's folder-name parser self-test. No C++ extractor needed. | — | — |
| `make clean` | Remove the C++ extractor binary and `pffft.o`. | — | — |
| `make clean-all` | `clean` + remove generated dataset/model/audio + remove `.venv`. | — | — |

### Examples

```bash
# Default flow — synthetic dataset, 80 clips/class, default hyperparameters
make pipeline

# Bigger synthetic dataset, more trees, more aggressive hard-negative weight
make pipeline N_PER_CLASS=200 N_ESTIMATORS=60 NEGATIVE_WEIGHT=5.0

# Real audio from a folder tree (includes Non-<Tag>/ hard negatives)
make pipeline ROOT=path/to/my_samples MAX_PER_CLASS=150

# Already have a CSV? Just train on it
make train CSV=build/my_samples.csv N_ESTIMATORS=64 MAX_DEPTH=10

# Score a single audio file against the most recent model
make classify WAV=~/samples/kick.wav TOP_K=5

# Inspect the loader's folder parsers (no build needed)
make self-test
```

The C++ extractor binary is standalone — it only needs the drlibs and
pffft headers from the plugin/Rack tree. It has no dependency on
`<rack.hpp>` or any other Rack runtime.

## Paste the result into the plugin

The generated file is the entire `SirenTagClassifier.cpp` — there is no
"marked region" anymore, the whole file is the model. Copy
`build/SirenTagClassifier.generated.cpp` over
`src/modules/siren/SirenTagClassifier.cpp` and rebuild:

```bash
cp scripts/siren-tag-model/build/SirenTagClassifier.generated.cpp \
   src/modules/siren/SirenTagClassifier.cpp
cd ../..   # back to the plugin root
make
```

The generated file is large (~20 MB of m2cgen-emitted code) and is picked up
by the root `Makefile`'s `wildcard src/**/**/*.cpp` glob — there is no
explicit `#include` in any plugin TU.

After pasting, the generated `static_assert` will catch any feature-count
mismatch:

```cpp
static_assert(::StoermelderPackOne::Siren::SIREN_TAG_NUM_FEATURES == 53,
    "Model was trained with 53 features; re-run scripts/siren-tag-model/run.sh");
```

## Re-running

The `Makefile` is incremental: re-running it skips venv setup, skips the
C++ build (if sources are unchanged), and re-uses the existing dataset
CSV (if its inputs are unchanged). Override any parameter on the
command line:

```bash
# Bigger synthetic dataset, more trees
make pipeline N_PER_CLASS=200 N_ESTIMATORS=60 MAX_DEPTH=10

# Different hyperparameters, same CSV
make train CSV=build/my_samples.csv N_ESTIMATORS=64 MAX_DEPTH=12 \
    AUGMENT=0 SEED=7 NEGATIVE_WEIGHT=5.0

# Underlying Python invocations (if you don't want the Make wrapper)
python3 train_model.py --csv build/synthetic_dataset.csv \
    --n-estimators 64 --max-depth 12
```

After pasting the new generated body, bump `MODEL_VERSION` in
`feature_config.py` and rebuild. The version number is exported as
`model_version` in the `SIREN_TAG_TRAINING_INFO_JSON` blob (and mirrored in the `// Model version:` header comment) — useful for diagnostics.

The full set of training parameters used for the current run
(`--csv`, `--n-estimators`, `--max-depth`, `--augment`, `--seed`,
dataset shape, calibrated-class count, scikit-learn / m2cgen /
numpy / Python versions, host platform) is also embedded in the
generated source as a compact JSON blob, and exposed to the plugin
at runtime via `TagClassifier::trainingInfo()`. The plugin can
read this back to populate a "model info" panel so users always
know which dataset / hyper-parameters produced the bundled
classifier — no sidecar files, no JSON parser dependency.

## Classifying a single file (Python smoke test)

```bash
# Score one of the synthetic clips
make classify WAV=build/synthetic_audio/drone_0010.wav
# — or, equivalently:
python3 classify_wav.py build/synthetic_audio/drone_0010.wav

# Try a real wav from anywhere
make classify WAV=~/samples/kick.wav

# Just dump the 53 features (skip the model step entirely)
make classify WAV=~/samples/kick.wav NO_MODEL=1
# — or:
python3 classify_wav.py ~/samples/kick.wav --no-model

# Show top 5 instead of top 3
make classify WAV=build/synthetic_audio/percussion_0020.wav TOP_K=5
```

For a quick feature dump from the C++ binary:

```bash
build/siren_extract_features build/synthetic_audio/percussion_0020.wav
```

Note: `classify_wav.py` uses `find_cpp_extractor()` to delegate feature
extraction to the C++ binary — Python feature extraction has been removed
so training and inference are guaranteed to use the same implementation.
For a coherence check, run the C++ binary on a file and compare its CSV
output to `classify_wav.py --no-model` on the same file; all 53 feature
values should be identical.

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
make csv-from-folder ROOT=my_samples OUT=build/my_samples.csv
# — or, equivalently:
python3 load_folder_dataset.py my_samples --out build/my_samples.csv

# 2. Train on it:
make train CSV=build/my_samples.csv
# — or, end-to-end:
make pipeline ROOT=my_samples
```

### Hard-negative folders (`Non-<Tag>`)

For real-world mis-classified samples — a recording that keeps getting
tagged as "Kick" but isn't — drop it into a `Non-<Tag>/` folder. The
loader treats every clip there as a hard-negative for the named tag
during training, with a 3× sample weight. The binary head for that tag
is pushed strongly away from the feature vector.

```
my_samples/
    Kick/
        kick_001.wav
    Non-Kick/                # hard negatives for the Kick head
        snare_loop_001.wav
        bass_sub_001.wav
    Pad/
        pad_001.wav
    Non-Pad/                 # hard negatives for the Pad head
        kick_002.wav
        clap_001.wav
```

`Non-<Tag>` is case-insensitive and must reference a real tag from
`res/data/SirenTags.json`. Multiple `Non-<Tag>` folders may be supplied
# Disable weighting entirely (or 0.0 to skip the rows completely)
make train CSV=build/my_samples.csv NEGATIVE_WEIGHT=1.0

# Strong pushback on every hard-negative row
make train CSV=build/my_samples.csv NEGATIVE_WEIGHT=5.0

# Underlying Python invocation (same flags)
python3 train_model.py --csv build/my_samples.csv --negative-weight 5.0

```bash
python3 train_model.py --csv build/my_samples.csv --negative-weight 5.0
# or 1.0 to disable weighting, or 0.0 to skip hard-negatives entirely
```

### The folder-name → tag convention

Each subdirectory name must match one of the 18 entries in
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
path,label,negatives,f0,f1,...,f52
my_samples/Bass/sub001.wav,Bass,,0.031,0.035,0.031,1.0,0.0,1.0,0.12,0.18,0.05,0.22,...
my_samples/Non-Kick/snare001.wav,,Kick,0.27,0.51,0.34,1.0,0.1,0.9,0.22,0.41,0.15,0.62,...
```

- `path` — relative path to the audio file.
- `label` — one tag name per row, exactly as in the manifest. Empty for
  pure-negative rows from `Non-<Tag>/` folders.
- `negatives` — `~`-separated list of tags the row is a hard negative
  for. Empty for positive rows. Old CSVs that pre-date the
  `Non-<Tag>` convention simply omit the column and still parse.
- `f0` … `f52` — the 53 features in contract order (see table above).
  Each is in `[0, 1]`.

### Class balancing

```bash
# Cap each tag at 200 clips
python3 load_folder_dataset.py my_samples --max-per-class 200 --out build/my.csv
```

### How many clips do I need?

Empirical guide for the current 53-feature `PerClassRFBag` (18 independent `RandomForestClassifier` heads) on 18 classes:

| Clips per class | Top-1 F1 (real audio) | Notes |
|-----------------|----------------------|-------|
| 20–50           | ~50–65%              | Barely better than random. Useful as a first pass. |
| 50–200          | ~65–80%              | Practical "good enough" range for a synth sample browser. |
| 200–1000        | ~80–90%              | Diminishing returns past ~500. |

With 53 features (including MFCCs, MFCC deltas, temporal envelope, and
repetitiveness features) the model can separate timbral classes much better
than earlier versions, but it also needs slightly more data to avoid
overfitting — aim for the 100–200 clips/class range.

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
| `siren_extract_features.cpp` | **C++ feature extractor CLI.** Takes audio file paths as args, outputs `path,f0..f52` CSV to stdout. Calls the same `TagClassifier::extractFeatures()` as the plugin — this is the authoritative feature extraction. |
| `Makefile` | Primary workflow. Builds the C++ extractor (`make`, `make extractor`, `make clean`) and exposes Python-orchestrated targets for the full pipeline: `make pipeline`, `make train CSV=…`, `make dataset`, `make csv-from-folder ROOT=…`, `make classify WAV=…`, `make self-test`, `make venv`, `make clean-all`. All Python targets are incremental and OS-portable. |
| `tag_manifest.py` | Reads `res/data/SirenTags.json`; exposes `CLASS_NAMES`, `TAGS`, `NUM_CLASSES`. Python-side source of truth for the tag vocabulary. |
| `feature_config.py` | The 53-feature contract (`FEATURE_NAMES`) + re-exports from `tag_manifest`. |
| `features.py` | Subprocess wrapper around the C++ extractor. Provides `find_cpp_extractor()` and `extract_features_batch()`. Includes an automatic transcoding fallback: files that the C++ extractor cannot decode (e.g. compressed WAV encodings) are re-encoded to temporary PCM WAV via `soundfile` and retried transparently. Also tiles clips that are too short for the STFT minimum. |
| `generate_synthetic_dataset.py` | Synthesizes 18 types of audio and writes a CSV. Requires the C++ extractor. |
| `load_folder_dataset.py` | Walks a folder of tag-named subdirectories, extracts features, writes a CSV. Recognises `Non-<Tag>/` subfolders as hard-negative buckets. Bridge to real data. |
| `train_model.py` | Fits 18 independent `RandomForestClassifier` heads (one per binary tag) via the `PerClassRFBag` adapter, prints metrics, calibrates per-class probabilities, calls `emit_cpp.py`. Accepts a `--negative-weight` flag for per-(row, class) hard-negative weighting. |
| `emit_cpp.py` | Uses `m2cgen` to write the C source fragment, with Platt-scaled per-class dispatcher, the `SIREN_TAG_TRAINING_INFO_JSON` metadata blob, and the `registerTrainingInfo()` call into the API header. |
| `classify_wav.py` | Python smoke-test tool: score a single wav file via the C++ extractor + Python reference model. |
| `run.sh` | macOS/Linux convenience wrapper: venv → extractor → dataset → train → emit. Use `make pipeline` instead if you have GNU make. |
| `run.bat` | Windows convenience wrapper (no GNU make required). Same flow as `run.sh`. |
| `requirements.txt` | Pinned Python deps. |
| `build/` | Output directory — binary, WAV clips, CSV, and generated source all land here. |
| `.venv/`, `__pycache__/` | Cached, gitignored. |

---

## Troubleshooting

**`ImportError: No module named 'm2cgen'`** — run `make venv` (or
`bash run.sh`); the venv installer skips this step when already up
to date.

**`m2cgen` RecursionError** — your forest is too deep. Pass
`MAX_DEPTH=6` to `make train`, or `--max-depth 6` to
`train_model.py` directly.

**Generated body is very large** — reduce `--n-estimators` to 16 or
`--max-depth` to 6. The current default model produces ~20 MB of generated
C; a minimal useful model is ~1–5 MB.

**"warn: C++ build failed"** — the C++ extractor could not be compiled.
The pipeline will not work without it; the Python feature fallback has
been removed. Common causes: `c++` not on `PATH`, or `RACK_DIR` pointing
at the wrong location. Fix with `make extractor RACK_DIR=/path/to/Rack`
or set the `RACK_DIR` environment variable.

**Smoke-test predictions look random** — training and inference are out of
sync. Both must use the same STFT parameters (`FFT_SIZE=512`, `HOP=128`,
`TARGET_SR=8820`) and the same 53 features in the same order. Run
`build/siren_extract_features <file>` and `make classify WAV=<file> NO_MODEL=1`
on the same file; all 53 feature values should match.

**The plugin's predictions differ from the Python smoke test** — same root
cause as above. Rebuild the C++ extractor with `make extractor` and re-run
`make pipeline` to regenerate the model using the C++ feature values.
Check that both sides produce exactly 53 values per file. Also check the
plugin and CLI agree by running the same WAV through both `make
testrun` paths and `classify_wav.py`.

**Trainer reports F1 = 0 on classes you know are present** — the CSV has
fewer than 18 labels or a folder name doesn't match the manifest. Check
folder names against `make self-test` (validates the parsers) or
`python3 load_folder_dataset.py --list-known-tags`.

**"skip: extraction still failed after transcoding"** — the file could not be
decoded even by the `soundfile` fallback. It is either genuinely silent, badly
corrupt, or uses an encoding that libsndfile also doesn't support. Re-encode
manually: `ffmpeg -i bad.wav -ar 44100 -sample_fmt s16 fixed.wav`

Note: most compressed WAV files (ADPCM, mu-law, IMA, etc.) are handled
**automatically** by the transcoding fallback and do not require manual
intervention. You will only see the message above for files that both drwav
and libsndfile cannot open.

**"skip: cannot decode foo.mp3"** — the file is corrupt or uses an
unsupported codec. Re-encode with `ffmpeg -i foo.mp3 -ar 22050 foo.wav`.

**Generated source is ~20 MB** — that's the m2cgen-emitted model body, not
a bug. It compiles to a few hundred KB of object code and runs in
microseconds per clip at the call sites in the browser pane.

**Build fails with `static_assert` mismatch** — `SIREN_TAG_NUM_FEATURES` in
the API header is `53` but the generated source was trained with a different
count. Re-run `bash run.sh` and paste the new `SirenTagClassifier.generated.cpp`
over `src/modules/siren/SirenTagClassifier.cpp`. The generated file
self-checks with `static_assert(::StoermelderPackOne::Siren::SIREN_TAG_NUM_FEATURES == 53, ...)`.

## When you're done

After pasting the generated body you can delete this folder from your
working copy — the plugin is self-contained. Keep it in the repo so future
maintainers can regenerate the model when the training data improves.
