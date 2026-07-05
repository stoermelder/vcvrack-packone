# Siren automatic tag classification

The Siren module can suggest tags for audio files and folders directly from the browser pane. A right-click on any file or folder exposes a **Suggest tags…** action that extracts a **53-element audio feature vector**, scores it against 18 independent classifiers, and surfaces the top suggestions in a confirmation dialog. Confirmed tags are written directly to the file's metadata.

The classifier is implemented as a small C function compiled into the plugin — there is no model file, no JSON loader, and no new runtime dependency. Training and C emission happen offline via `scripts/siren-tag-model/`; see `scripts/siren-tag-model/README.md` for the training workflow.

## Architecture

```
res/data/SirenTags.json          ← tag vocabulary (18 tags, source of truth)
SirenMetadata.hpp                ← tagManifest() / starterTags() / starterTagKeywords()
                                   loaders (jansson + rack::asset::plugin + isTesting short-circuit)
SirenTagClassifierApi.hpp        ← feature extraction (SIREN_TAG_NUM_FEATURES = 53)
                                   TagClassifier model-registration slot (lazy-loaded on first use)
                                   filename-keyword boost helpers
                                   STFT helpers (Hann window, PFFFT setup guard, magnitudes, flux,
                                   median-filter, mean-subtract, peak-count)
SirenTagClassifier.cpp           ← auto-generated model body (compiled into the plugin via the
                                   `wildcard src/**/**/*.cpp` glob in the root Makefile;
                                   includes API, registers itself via _setLoader() at static init)
SirenBrowserPane.hpp             ← right-click menu + startTagClassification() entry point
SirenTopBar.hpp                  ← "Suggest tags on all files" + cancel menu
SirenBackgroundTasks.hpp         ← SirenClassifyTask: worker thread, recursive folder scan,
                                   per-file classification, post-scan confirm dialog
SirenBpmDetector.hpp             ← independent; also includes the API header for shared STFT helpers
src/ui/AutoTagDialog.hpp         ← generic TagConfirmDialog<TPayload> +
                                   AsyncTagConfirmDialog<TPayload> (legacy, kept for Mb)

siren_extract_features (CLI)     ← includes API only; no model needed for feature extraction
```

**Dependency direction.** `SirenTagClassifier.cpp` (model) → `SirenTagClassifierApi.hpp` (API). The API does not include the model. The model registers itself through a static initializer when the `.cpp` is linked into the plugin, using a lazy-init slot (`_setLoader`) that fires on the first `score()` / `classify()` call. `extractFeatures()` always works with no model loaded.

The plugin picks the model up without an explicit `#include`: the root `Makefile` uses `SOURCES += $(wildcard src/*.cpp src/**/**/*.cpp)`, so `SirenTagClassifier.cpp` is compiled and linked automatically. The CLI tool (`scripts/siren-tag-model/siren_extract_features.cpp`) does **not** compile that `.cpp`, so it gets feature extraction with zero model code linked.

## Tag vocabulary

Defined in `res/data/SirenTags.json` (single source of truth). Capitalized names are stored and displayed exactly as written — no case transformation in the UI.

| Tag | Category | Description |
|-----|----------|-------------|
| Acoustic | timbre | Acoustic instrument character: natural harmonics with exponential decay |
| Atmospheric | timbre | Slowly evolving ambient texture, pads with movement |
| Bass | role | Sub- or bass-register sustained or rhythmic material |
| Clap | source | Clap: layered noise bursts with mid-frequency character |
| Cymbal | source | Cymbal or crash: high-frequency metallic noise, longer decay |
| Drone | source | Sustained, slowly evolving pitch |
| Drums | source | Full drum pattern: combination of kick, snare, hi-hat |
| FX | role | Special effect: sweeps, risers, impacts |
| Glitch | timbre | Stuttering, fragmented audio: random bursts, stutter edits |
| HiHat | source | Hi-hat: short metallic high-frequency noise burst |
| Kick | source | Kick drum: low-frequency transient with bass content |
| Lead | role | Melodic, mid/high register, often rhythmic |
| Loop | time | Cyclical material that loops cleanly |
| Nature | source | Environmental recording: room tone, wind, rain |
| Noise | source | Broadband noise without a clear tonal component |
| Pad | role | Sustained harmonic bed: chords or textures beneath a melody |
| Snare | source | Snare drum: mid-frequency transient with noise component |
| Vocal | source | Vocal or vocal-like: sung voice, formant tones |

A 15-tag **fallback list** lives in `SirenMetadata.hpp::fallbackTags()` and is used by the test harness and when the JSON manifest cannot be read on disk. The fallback intentionally contains `One-Shot` and `Stab` so older patches keep their tags readable; they are never *suggested* by the classifier.

### Manifest schema

`res/data/SirenTags.json` is a JSON document with three top-level fields:

| Field | Type | Purpose |
|-------|------|---------|
| `version` | integer | Manifest version; bumped on breaking tag-vocabulary changes. |
| `description` | string | Free-form human-readable note (also serves as the file's docstring). |
| `tags` | array of objects | The 18 tag entries, sorted by `name`. |

Each tag entry carries:

| Field | Type | Purpose |
|-------|------|---------|
| `name` | string | The tag as stored, displayed, and emitted by the classifier. Case-preserving. |
| `category` | string | `timbre`, `role`, `source`, or `time` — a UI grouping hint. |
| `description` | string | Free-form text shown in the tag editor. |
| `classifier_can_suggest` | boolean | When `true`, the classifier may surface this tag; when `false`, it stays editable only. Currently `true` for every shipped tag. |
| `keywords` | object | Optional. Maps each filename keyword to its reliability prior in `(0, 1]`. Word-boundary matched against the filename stem; the matched prior is fused with the audio score by noisy-OR in `TagClassifier::applyFilenameBoosts`. Use `0.9` for reliable cues and a lower prior for ambiguous ones (`loop`, `fx`, `sub`, `hat`). |

The manifest is parsed once per module load by `tagManifest()` in `SirenMetadata.hpp` into a `TagManifest { tags, keywords }`. The function short-circuits to `fallbackTags()` when `isTesting()` is true (no asset path is available in the test harness), and falls back on parse failure with a `WARN` log. The keyword map is consumed by the browser pane via `starterTagKeywords()` and registered into `TagClassifier` at the start of each classification.

## Feature extraction

All features are in `[0, 1]`. The extractor reads the first 30 s of audio, decimates to `TAG_TARGET_SR ≈ 22050 Hz` (decim rate = `sampleRate / 22050`; e.g. factor 2 for 44.1/48 kHz inputs), and analyses it with `TAG_FFT_SIZE = 1024`, `TAG_HOP = 256`, Hann window, 4× overlap. The decimated mono buffer is **peak-normalised to ±1.0** in `prepareMono()` so every feature is gain-invariant; near-silent buffers are left untouched.

The rate is deliberately higher than the BPM detector's 8820 Hz: the metallic / high-frequency tags (HiHat, Cymbal, bright Snare) carry their discriminating energy above the 4.41 kHz Nyquist the old rate implied, and `TAG_FFT_SIZE` is raised in proportion so frequency resolution stays ~21.5 Hz/bin.

The feature order is the contract between `extractFeatures()` in `SirenTagClassifierApi.hpp` and `feature_config.FEATURE_NAMES` in the training pipeline — they must match exactly. **`SIREN_TAG_NUM_FEATURES` is defined in `SirenTagClassifierApi.hpp`** (currently `53`), not in the generated model header; the generated header contains a `static_assert` to catch retraining mismatches.

### Time-domain (computed before STFT)

| # | Name | What it captures |
|---|------|-----------------|
| 2 | `zero_crossing_rate` | Noisiness — sign-change fraction per frame |
| 3 | `rms` | Loudness / density (normalised by 1/√2) |
| 10 | `crest_factor` | Transient sharpness — peak/RMS, log-normalised: `min(1, log(1+crest)/log(31))` |
| 11 | `harmonic_ratio` | Periodicity — normalised autocorrelation peak over the 49–1100 Hz pitch band (lag bounds derived from `outSR`, so the band is fixed across sample rates; at 22050 Hz this is `lagMin ≈ 20`, `lagMax ≈ 450`) |

### Spectral — centroid family (one STFT pass, per-hop averaged)

| # | Name | What it captures |
|---|------|-----------------|
| 0 | `spectral_centroid` | Brightness — power-weighted mean frequency / (Nyquist − 1) |
| 1 | `spectral_rolloff85` | Upper extent — bin below which 85% of energy lies, / (Nyquist − 1) |
| 7 | `spectral_bandwidth` | Spread — power-weighted std dev of frequency / (Nyquist − 1) |
| 16 | `spectral_skewness` | Asymmetry of spectral shape (3rd standardised moment), /6+0.5 |
| 17 | `spectral_kurtosis` | Peakedness (excess 4th moment), `(kurtosis − 3 + 3)/10` |
| 14 | `spectral_slope` | Spectral tilt — Pearson r(bin, mag) × 0.5 + 0.5 |
| 15 | `spectral_decrease` | Weighted low-frequency bias, `decrease + 0.5` |

### Spectral — energy and texture

| # | Name | What it captures |
|---|------|-----------------|
| 5 | `low_mid_ratio` | Energy in `[80, 250) Hz` / total |
| 8 | `high_band_ratio` | Energy above 2000 Hz / total |
| 6 | `spectral_flatness` | Tonality — geometric/arithmetic mean of magnitude (noise=1, tone=0) |
| 12 | `spectral_crest` | Spectral peak-to-mean ratio / HALF_N (single tone=1, flat noise≈0) |
| 13 | `spectral_entropy` | Disorder — Shannon entropy of PSD normalised by log(N) |

### Temporal dynamics

| # | Name | What it captures |
|---|------|-----------------|
| 4 | `onset_density` | Rhythmic activity — peaks/sec of median-filtered spectral flux / 30 |
| 9 | `mean_spectral_flux` | Overall spectral change rate (log-domain, per-hop mean) / 50 |
| 39 | `flux_variance` | Std dev of per-hop flux / 50 — glitch/drums→high, drone/pad→low |

### Extra band ratios

The STFT pass tracks four non-overlapping frequency regions whose ratios sum to 1.0:
`sub_bass [0, 80)` / `low_mid [80, 250)` / `mid [250, 2000)` / `high [2000+)`. The four
ratios are written to the feature vector as:

| # | Name | Band |
|---|------|------|
| 37 | `sub_bass_ratio` | `[0, 80) Hz` — kick → high, almost everything else → low |
| 38 | `mid_band_ratio` | `[250, 2000) Hz` — snare/clap/vocal → high, kick/sub-bass → low |

### Temporal envelope (32 RMS blocks; computed before STFT)

The audio is split into 32 equal-length blocks; per-block RMS drives five features:

| # | Name | What it captures |
|---|------|-----------------|
| 31 | `temporal_centroid` | Time-weighted centre of mass of RMS envelope — one-shot→0, loop/pad→0.5 |
| 32 | `tail_head_ratio` | `rms(last 20%) / (rms(first 20%) + rms(last 20%))` — one-shot→0, loop→0.5, fade-in→1 |
| 33 | `env_ac_peak` | Peak normalised autocorrelation of the 100 ms RMS envelope at 0.25–4 s lags — rhythmic loop→1, one-shot→0 |
| 34 | `attack_time` | Block of peak RMS / 31 — percussive (kick, snare)→0, pad/drone→high |
| 35 | `env_rms_variance` | Normalised std dev of RMS envelope blocks (σ×2, capped 1) — sustained→0, rhythmic/one-shot→high |
| 36 | `temporal_entropy` | Shannon entropy of normalised RMS envelope — one-shot→0, drone/noise→1 |

### MFCCs — timbral identity (mel filterbank → log → DCT)

Mel filterbank: 26 triangular filters, starting at `MEL_F_MIN = 80 Hz` (the API code notes that at the tag-classifier sample rate of 22050 Hz, the lowest filters would otherwise collapse into the same bin and go dead) up to Nyquist. Log-compressed per hop, averaged, DCT-II applied (`c += log_mel[m] * cos(π·n·(m + 0.5) / N_MELS)`). C[0] captures log energy (normalization `÷200`); C[1..12] capture shape (normalization `÷30`). Both mapped to `[0, 1]` via `c/norm + 0.5` and clamped.

| # | Names |
|---|-------|
| 18–30 | `mfcc_0` … `mfcc_12` (mean of log-mel) |
| 40–52 | `mfcc_delta_0` … `mfcc_delta_12` (mean absolute frame-to-frame difference, no `+0.5` offset since deltas are non-negative; static sounds → near 0, melodic/rhythmic material → higher) |

The MFCC deltas are computed in the same per-hop pass as the base MFCCs — the per-hop DCT output is buffered in `acc.mfccPrev` and compared with the next hop's coefficients. No second forward FFT pass is needed.

### Extraction pipeline

`extractFeatures(AudioStream& stream, float out[53])` in `SirenTagClassifierApi.hpp` runs in four phases:

1. **`prepareMono`** — read the stream, decimate, mix to mono, peak-normalise to ±1.0. (`prepareMono` takes a `targetSR` parameter, default `TARGET_SR` 8820 so the BPM detector is unaffected; the tag classifier passes `TAG_TARGET_SR`.)
2. **`extractTimeDomainFeatures`** — ZCR, RMS + crest, the 32-block temporal envelope (centroid, tail/head, attack, RMS variance, entropy), 100 ms-block envelope autocorrelation (lags corresponding to 0.25–4 s), and harmonic ratio over the 49–1100 Hz pitch band using up to ~0.5 s of audio (`min(mono.size(), max(4096, outSR / 2))`).
3. **`runSTFT`** — two inner bin loops per hop. Loop 1: power, centroid, band ratios, slope + crest inputs. Loop 2: bandwidth, flatness, skewness, kurtosis, decrease, entropy, mel filterbank, per-hop MFCC, 85% rolloff.
4. **`finalizeSpectralFeatures`** — normalize accumulators → 30 spectral/MFCC outputs + 13 MFCC-delta outputs (40–52).

## Model

The classifier is an ensemble of 18 independent `RandomForestClassifier` heads (one per tag) trained offline with `sklearn` and emitted to C via `m2cgen`. Each head produces an independent positive-class probability for its tag; the final per-tag scores are Platt-scaled using per-class calibration parameters fitted on a held-out split.

### Why per-class heads

`MultiOutputClassifier` accepts a single per-row `sample_weight`. That makes a `Non-Kick/` sample equally expensive across all 18 heads, which is the wrong trade — we only want it to penalise the Kick head. `train_model.py::PerClassRFBag` is a thin adapter that fits 18 independent `RandomForestClassifier` instances and passes each one its own slice of the per-(row, class) weight matrix. The adapter exposes the same `.estimators_` interface expected by `CalibratedClassifierCV` so Platt calibration can run unchanged on each head.

### Generation

`emit_cpp.py` emits:

- **Per-class bodies** (`siren_tag_class_<c>_impl`) — one C function per binary classifier. Each one takes a `double*` feature vector and writes its own tree-ensemble accumulator into a `double*` output. Hoisted `add_vectors` / `mul_vector_number` helpers are shared across all 18 bodies.
- **Per-class bridges** (`siren_tag_score_class_<c>`) — uniform wrappers that return the positive-class probability of each binary head.
- **Dispatcher** (`siren_tag_score`) — loops over the 18 classes, calls each bridge, applies the per-class Platt sigmoid `1 / (1 + exp(-a·raw + b))` using the per-head `a` / `b` parameters from training, and writes the calibrated scores to a `float*`. The signature is `(const float* features_in, float* scores_out)` — plain pointers, no compile-time dependency on `SIREN_TAG_NUM_FEATURES`. The `for` loop internally widens the 53 input floats to a `double _x[53]` array once and feeds the same array to all 18 bridges.
- **Training-metadata blob** (`SIREN_TAG_TRAINING_INFO_JSON`) — embedded as a string literal. Fields: `augment_copies`, `calibrated_classes`, `calibration` (always `"platt_sigmoid"`), `dataset_classes` / `dataset_csv` / `dataset_features` / `dataset_shape` (rows × cols), `m2cgen_version`, `max_depth`, `min_samples_leaf`, `model_version`, `n_estimators`, `negative_cells`, `negative_classes`, `negative_weight`, `numpy_version`, `platform`, `python_version`, `random_seed`, `sklearn_version`, `test_samples`, `test_size`, `train_samples`. Read at runtime via `TagClassifier::trainingInfo()` for diagnostics.
- **Registration** — a `_siren_load_model()` helper calls `TagClassifier::registerModel()` and `TagClassifier::registerTrainingInfo()`. An anonymous-namespace static const initialiser calls `TagClassifier::_setLoader(_siren_load_model)` so the actual registration fires on the first `score()` / `classify()` call. The header at the top of the generated file records the auto-generated warning, model version, class list, feature count, and a human-readable dump of the training parameters.

### Lazy loading

The model registers itself through `_setLoader()` at static-init time (one pointer store). The actual `registerModel()` call fires the first time `score()` / `classify()` is invoked. `extractFeatures()` never touches the model.

### Filename-keyword boost

The 4-arg `classify(stream, filePath, k=5, ...)` overload applies `applyFilenameBoosts()` to the audio-evidence scores. The keyword map (with per-keyword priors) is loaded from `SirenTags.json`. For each tag, the strongest matched keyword's prior `p` is fused with the audio score `s` by **noisy-OR**: `s' = 1 − (1 − s)·(1 − p)`. This is monotone in the audio score (calibration preserved, "filename + audio agree" reads higher than either alone), never reduces audio evidence, and never hard-pins to a constant the way the old `max(score, 0.9)` did. The browser pane uses this overload with `k=5` and a score cutoff of `0.5` — tags must clear the threshold to be surfaced.

## API

`SirenTagClassifierApi.hpp` is self-contained and depends only on `SirenAudioStream.hpp` (which uses `<cstdint>`). It defines `SIREN_TAG_NUM_FEATURES = 53`, `SuggestedTag`, and the `TagClassifier` namespace:

- `TagClassifier::extractFeatures(AudioStream&, float out[53], float maxDurationSeconds = 30.f)` — always available; performs the full feature pipeline. Clamps output to `[0, 1]`; returns all-zeros if the stream is invalid or yields fewer than 4 STFT hops.
- `TagClassifier::score(const float features[53], float* out)` — runs the registered model over a feature vector, writing `numClasses()` floats to `out` (18 in the shipped model). Clamps both inputs and outputs to `[0, 1]`. No-op if no model is loaded.
- `TagClassifier::topK(const float* scores, int k = 3)` — returns the top-k `(tag, score)` pairs from a pre-scored vector. Returns `{}` if no model is loaded.
- `TagClassifier::classify(const float features[53], int k = 3)` — `topK` after a single `score()`.
- `TagClassifier::classify(AudioStream&, int k = 3, float maxDurationSeconds = 30.f)` — `extractFeatures()` then the feature-vector overload.
- `TagClassifier::classify(AudioStream&, const std::string& filePath, int k = 3, float maxDurationSeconds = 30.f)` — filename-aware overload (default `k=3`); the browser pane calls it with `k=5`. Applies `applyFilenameBoosts()` after scoring.
- `TagClassifier::filenameStem(path)` — utility: lowercase basename without extension.
- `TagClassifier::wordContains(stem, kw)` — utility: word-boundary keyword match in a lowercase stem.
- `TagClassifier::applyFilenameBoosts(stem, scores, n, classNames, boost = 0.9f)` — bumps each score to `max(score, boost)` when any of its registered keywords appears in the stem; audio evidence is never reduced.
- `TagClassifier::registerKeywords(keywords)` — replaces the keyword map used by the filename boost. The browser pane calls it with `starterTagKeywords()` at the start of each classification.
- `TagClassifier::trainingInfo()` — returns the embedded training-metadata JSON for diagnostics. Returns `""` when no model is loaded or the model did not embed metadata.
- `TagClassifier::numClasses()` — number of classes in the loaded model (0 if no model is registered yet).

Model registration is internal and used only by the generated `.cpp`:

- `TagClassifier::_setLoader(void (*fn)())` — registers a deferred loader at static-init time (one pointer store).
- `TagClassifier::registerModel(scoreFn, numClasses, classNames)` — wires the loaded model up; called by the loader on first scoring use.
- `TagClassifier::registerTrainingInfo(json)` — stores the embedded training-metadata blob (pointer with static storage duration; not copied).

## Integration

The **Suggest tags** entry points live in two places:

- `SirenBrowserPane.hpp::startTagClassification(DataSourceNode)` — wired to the right-click menu on any file or folder in the browser tree, with menu label **Suggest tags**.
- `SirenTopBar.hpp` source menu — **Suggest tags on all files** bulk-classifies the active root (a synthetic root `DataSourceNode` is constructed and passed to `startTagClassification`). A **Cancel tag classification** entry appears while a scan is in flight.

Both paths converge on `SirenClassifyTask` (`SirenBackgroundTasks.hpp`), which owns the worker:

1. **Single file** — a one-element file list; `TagClassifier::classify(*stream, f, 5)` (filename-aware) runs on the worker thread.
2. **Container** — recursively collects audio files via `DataSource::loadChildrenSync` and classifies each. Per-tag payloads accumulate all `relPath`s scoring `≥ 0.5`.

The scan collects tags in a `std::map<std::string, std::set<std::string>>` (tag → relative paths). While a scan is running, the status line shows `Analysing… N/M`. When the worker signals `done`, the main thread:

- Shows `osdialog_message` "No new tag assignments found." if the result map is empty.
- Otherwise opens a `ui::TagConfirmDialog<std::string>` (the payload type is the relative path string) via `openTagConfirmDialog`. Header is `"Suggest tags"` for a single file or `"Suggest tags — <dirName>"` for a folder. Tags already present on a file are filtered out by lowercase-trimmed name before the worker emits them, so the dialog never suggests a tag the file already has.
- The `onApply` callback in the pane writes the accepted tags via `MetadataStore::addTag` and saves metadata. Tags are applied directly; no intermediate suggestion state.
- Filename labels in the confirm dialog are clickable — clicking one calls `selectPath(resolveNode(fileId), true)` which loads and plays the file in the preview pane. Right-click on a label offers **Remove from group**.

## Training pipeline

Training happens entirely off the plugin's runtime path. The `make pipeline` target (or `bash scripts/siren-tag-model/run.sh` / `run.bat` on platforms without GNU make) runs the full cycle:

1. Create `.venv/` and install pinned deps from `requirements.txt`.
2. **Build the C++ feature extractor** (`make` in `scripts/siren-tag-model/`) — this binary is the authoritative feature implementation; training always uses the same code as inference. The Python side calls the binary via `features.py`, with a `soundfile`-based transcode fallback for encodings `drwav` can't decode (ADPCM, mu-law, IMA, …).
3. Load a labeled dataset — synthetic via `generate_synthetic_dataset.py`, a real folder tree via `load_folder_dataset.py` (which understands `Non-<Tag>/` hard-negative folders), or an existing CSV.
4. Train 18 independent `RandomForestClassifier` heads through the `PerClassRFBag` adapter, applying a per-(row, class) sample-weight matrix.
5. Apply Platt calibration per-class on the held-out split.
6. Emit `build/SirenTagClassifier.generated.cpp` with the `SIREN_TAG_TRAINING_INFO_JSON` metadata blob embedded.

The split is **by clip** (`GroupShuffleSplit` on `path` from `sklearn.model_selection`) into train / cal / test. `train_model.py` carves out the test partition first (`test_size = args.test_size`, default `0.25`), then splits the remaining 80% again with `test_size = 0.25` to produce a calibration slice (`0.25 × 0.8 = 0.20` of the original dataset), leaving roughly 60% train. Augmented rows are kept OUT of the calibration and test partitions — augmentation is a training-time signal, and metrics / calibration must never see augmented variants. Feature-space jitter (`--augment`) is applied to the train partition only.

When the calibration slice has too few distinct clips for a separate hold-out, `train_model.py` falls back to fitting calibration on the test set and warns. (Synthetic datasets hit this path because the calibration slice can degenerate to a single class.)

Per-class Platt calibration is fitted by `fit_calibration()` in `train_model.py`. The function implements the Platt (1999) sigmoid directly — the same algorithm `CalibratedClassifierCV(method='sigmoid')` uses internally — with the addition of label smoothing to handle the degenerate single-label case common with synthetic data. Returns `None` for a class when calibration cannot be fitted (fewer than two distinct label values in the calibration set); the dispatcher's per-class branch is skipped for that class and the un-calibrated bridge score is used as-is.

To roll a new model into the plugin:

1. `make pipeline` (or `bash scripts/siren-tag-model/run.sh`).
2. Copy `build/SirenTagClassifier.generated.cpp` over `src/modules/siren/SirenTagClassifier.cpp`.
3. Rebuild.
4. Bump `MODEL_VERSION` in `scripts/siren-tag-model/feature_config.py` when the new model should be distinguishable from the old one.

The full pipeline takes ~15 s end-to-end.

## Hard negatives

`load_folder_dataset.py` defines `NEGATIVE_FOLDER_PREFIX = "Non-"`. A folder named `Non-<Tag>/` (case-insensitive prefix match) is treated as a hard-negative bucket for `<Tag>` — samples that are not `<Tag>` but might look `<Tag>`-ish. Each clip in such a folder writes a per-(row, class) weight matrix entry of `negative_weight` (default `3.0`, exposed as `DEFAULT_NEGATIVE_WEIGHT` in `train_model.py`) on the target column only. The weight only affects the matching classifier head; it does not contaminate training of the other 17 heads.

Edge cases:

- The bare prefix `Non-` (no tag suffix) is skipped with a warning.
- `Non-<unknown>` — the suffix doesn't match a known tag in `SirenTags.json` — is also skipped with a warning.
- A hard-negative row is mutually exclusive with a positive label: a sample's row sets the matching column's weight to `negative_weight` and zeros out all other class weights, so the row contributes to exactly one head.

In the CSV format produced by `load_folder_dataset.py`, hard negatives are recorded in a `negatives` column; older CSVs use the `Non-<Tag>` folder convention. `train_model.py` accepts both and produces the same per-(row, class) sample-weight matrix either way.

## Relevant files

### Plugin code

| File | Role |
|------|------|
| `src/modules/siren/SirenTagClassifierApi.hpp` | Self-contained API. Defines `SIREN_TAG_NUM_FEATURES = 53`, all 53 features, lazy model registration, filename-keyword boosting. No Rack or model dependency. Shared with `SirenBpmDetector.hpp` for the STFT helpers. |
| `src/modules/siren/SirenTagClassifier.cpp` | Auto-generated model body (18-class, currently ≈5 MB of m2cgen-emitted code). Includes the API, self-registers via a `_setLoader()` / `_siren_load_model()` static initializer pair (lazy: `registerModel()` fires on the first scoring call, not at static init), and embeds a `SIREN_TAG_TRAINING_INFO_JSON` blob for runtime diagnostics. Regenerated by `run.sh` / `make train`; do not hand-edit. |
| `src/modules/siren/SirenBackgroundTasks.hpp` | `SirenClassifyTask` — owns the worker thread, the recursive file collection for folders, the per-file `TagClassifier::classify()` call, the `tagToRels` aggregation, and the post-scan confirm dialog. |
| `src/modules/siren/SirenAudioStream.hpp` | `AudioStream` interface (pure virtual, `<cstdint>` only). Used by both the API and the CLI tool. |
| `src/modules/siren/SirenMetadata.hpp` | Loads the tag manifest + keyword map at module load time via jansson (`starterTags()`, `starterTagKeywords()`). Has a 15-tag fallback list for tests and parse failures. |
| `src/modules/siren/SirenBrowserPane.hpp` | UI integration. Right-click context menu on any file/folder with **Suggest tags** entry; `startTagClassification()` method that constructs a `SirenClassifyTask` and wires the apply callback into `MetadataStore::addTag`. |
| `src/modules/siren/Siren.cpp` | Module + audio streaming. |
| `src/modules/siren/SirenBpmDetector.hpp` | Also includes the API header (shares STFT setup). |
| `src/ui/AutoTagDialog.hpp` | Generic `TagConfirmDialog<TPayload>` plus a legacy `AsyncTagConfirmDialog<TPayload>` adapter (kept for the Mb module). Siren instantiates `TagConfirmDialog<std::string>` directly. |
| `res/data/SirenTags.json` | 18-tag vocabulary (source of truth for plugin and training pipeline). |
| `src/modules/siren/SirenTagClassifierAPI.test.cpp` | Not yet written. Would pattern-match `SirenBpmDetector.test.cpp` (`MockAudioStream` + `AudioStream` factories for synthesised tones, noise, transients) and assert qualitative feature behaviour — e.g. high `spectral_centroid` for a bright tone, high `sub_bass_ratio` for an 80 Hz sine, high `onset_density` for a click train, high `harmonic_ratio` for a sustained sine, distinct `mfcc_0..mfcc_2` for tonal vs. noise streams. |

### Training pipeline (`scripts/siren-tag-model/`)

| File | Purpose |
|------|---------|
| `siren_extract_features.cpp` | C++ CLI tool — reads audio, outputs `path,f0..f52` CSV. Authoritative feature extractor. |
| `Makefile` | Orchestrates the whole pipeline: builds the CLI tool, creates the venv, generates / loads datasets, trains, classifies, runs the loader's self-test. See `make help` (top comment) for the full target list. |
| `requirements.txt` | Pinned Python deps (numpy, scikit-learn, m2cgen, soundfile, …). |
| `feature_config.py` | 53-feature name list (`FEATURE_NAMES`), `MODEL_VERSION`, re-exports from `tag_manifest.py`. |
| `tag_manifest.py` | Reads `res/data/SirenTags.json`; exposes `CLASS_NAMES`, `TAGS`, `NUM_CLASSES`. |
| `features.py` | Bridge to the C++ extractor binary (`extract_features_batch` for files, `extract_features_for_arrays` for in-memory augmented waveforms), with a `soundfile` transcode fallback for encodings `drwav` can't decode. |
| `audio_augment.py` | Pure-numpy audio-domain augmentation (noise, speed/pitch, EQ, reverb, time shift) applied to waveforms before extraction. No gain/level transform (the extractor peak-normalises). |
| `generate_synthetic_dataset.py` | Synthesizes 18 audio types, calls the C++ extractor, writes CSV. |
| `load_folder_dataset.py` | Walks tag-named folder tree (including `Non-<Tag>/` hard-negative folders), calls the C++ extractor, writes CSV. `--augment N` adds N audio-domain variants per clip (sharing the clip's `path` for leak-free grouping). |
| `train_model.py` | Fits the `PerClassRFBag` (18 independent RF heads), per-class sample-weight matrix, prints metrics, applies Platt calibration, embeds training-info JSON, calls `emit_cpp.py`. |
| `emit_cpp.py` | Emits C source via `m2cgen` with per-class bodies, uniform bridges, Platt-scaled dispatcher, and the `SIREN_TAG_TRAINING_INFO_JSON` blob. |
| `run.sh` / `run.bat` | No-Make one-shot wrappers (build extractor → venv → dataset → train → emit). Identical output to `make pipeline`. |
| `README.md` | Day-to-day "how do I run this" guide; the canonical user-facing reference for the pipeline. |

## Design notes

- **Zero runtime dependency.** The model ships as compiled C source — no model file, no JSON loader, no `sklearn` / `m2cgen` / numpy in the plugin's link graph. `make dist` is unchanged.
- **C++ extractor is authoritative.** `siren_extract_features` and the plugin's `extractFeatures()` share the same implementation. The Python side bridges to that binary in `features.py`; training always produces the same features as inference.
- **Dependency inversion.** The generated model includes the API; the API does not include the model. The plugin pulls the model in via the wildcard glob in the root `Makefile` — no explicit `#include` in any plugin TU.
- **Tags stored and displayed as-is.** No `toTitleCase` anywhere — the JSON is the source of truth. Tags are applied directly to the file's metadata; no intermediate suggestion state.
- **Platt scaling per class.** The dispatcher applies `1 / (1 + exp(-a·raw + b))` to each per-class raw score using the per-head `a` / `b` parameters emitted by `emit_cpp.py` from the calibration set. Output stays in `[0, 1]`.
- **Top-k with score cutoff ≥ 0.5.** The Python smoke test and the C++ runtime use the same threshold.
- **Background scan.** File-by-file classification runs on the worker thread; the main thread shows a status line (`Analysing… N/M`) until the scan completes, then opens the confirm dialog. Folder classification does not block the UI but does not stream partial results — the dialog opens once the full scan finishes. The earlier `StreamingTagDialog<>` and `AsyncTagConfirmDialog<>` patterns are kept in `src/ui/AutoTagDialog.hpp` for the Mb module and other call sites; Siren uses the simpler `TagConfirmDialog<std::string>` directly.
- **Explicit trigger only.** No auto-classify on load.
- **Race-condition behaviour.** Rapid right-clicks each create a new worker task and overlay. Each completes independently; no in-flight sentinel.
- **MFCC deltas.** Features 40–52 are mean absolute frame-to-frame differences per coefficient, computed during the STFT pass (no second forward FFT). They add temporal-dynamics signal at very low cost and are the main reason the feature count grew from 32 to 53.