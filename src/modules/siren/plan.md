# Siren: VCV Rack Sample Browser Module — Implementation Plan

## Context

Siren is a new wide-panel VCV Rack module that provides a file-system sample browser with waveform preview, drag-to-other-modules support, user tagging/favorites, and groundwork for future trim/volume-curve editing. It follows the same settings-persistence and UI patterns already established in this plugin (MB, MidiCat).

---

## Architecture Overview

```
SirenModule (rack::Module)
  ├─ fill thread — AudioStream → ring buffers (rbL, rbR)
  ├─ process()   — ring buffers → OUT L / OUT R
  └─ dataToJson / dataFromJson (patch state)

SirenWidget (ThemedModuleWidget<SirenModule>)
  ├─ SirenBrowserPane  (left ~35%)  — tree browser
  ├─ SirenPreviewPane  (right ~65%) — waveform + metadata bar
  └─ SirenVuMeter                   — two-channel LED bar
```

**Settings split:**
- `SirenSettings` (self-contained struct in `Siren.cpp`) — global: root containers, active root index, last file, last playhead. Persisted to `~/.../Stoermelder-P1/siren.json`.
- `dataToJson`/`dataFromJson` — patch-local: last file, playhead, active root. Takes priority over global settings on restore.
- Per-root metadata (`RootMetadata`) — tags and favorites, persisted to `siren-<8-char-hash>.json` alongside `siren.json`.

**Audio ownership chain:**  
UI calls `SirenModule::openStream()` → transfers `AudioStream*` to the fill thread via `pendingStream` atomic. UI calls `startPlayback(pos)` → posts `pendingSeekFrame`. Fill thread populates `rbL`/`rbR`; `process()` drains them on the DSP thread. No mutex on the audio path — only atomics and ring buffers.

---

## File Structure

```
src/modules/siren/
  Siren.cpp              — SirenModule (fill thread, DSP, JSON) + SirenWidget
  SirenDataSource.hpp    — DataSource + AudioStream abstract interfaces
  SirenFileSystem.hpp    — FileSystemDataSource: dr_libs decode + async dir scan
  SirenMetadata.hpp      — RootMetadata, SampleEntry, tag/favorite model, JSON I/O
  SirenAudio.hpp         — AudioInfo, WaveformCache, build/load/save helpers
  SirenBrowserPane.hpp   — left tree-browser widget + SirenDragState
  SirenPreviewPane.hpp   — right waveform preview widget
  SirenVuMeter.hpp       — two-channel vertical LED bar widget
  Siren.test.cpp         — Catch2 unit tests (co-located, follows project pattern)

dep/drlibs/
  dr_wav.h
  dr_flac.h
  dr_mp3.h               — header-only; DR_*_IMPLEMENTATION defined in SirenFileSystem.hpp
dep/hidapi/              — (unrelated to Siren)
```

---

## Phase 1 — Settings & Persistence ✅ DONE

### File paths (follow pluginsettings.cpp pattern)
```cpp
// In pluginsettings.cpp / pluginsettings.hpp additions:
std::vector<std::string> sirenRootContainers;
int    sirenSortMode  = 0;
bool   sirenShowExts  = true;

static std::string sirenFilePath()  { return settingsDirPath() + "/siren.json"; }
// per-root: settingsDirPath() + "/siren-" + hashPath(root) + ".json"
```

`siren.json` stores: root container paths, UI state (last selected file, scroll positions, sort mode).

`siren-<xxxx>.json` (one per root, name = 8-char hex hash of root path) stores:
```json
{
  "rootPath": "/Users/ben/Samples",
  "favorites": ["relative/path/to/sample.wav", ...],
  "tags": {
    "relative/path/to/sample.wav": ["drone", "loop", "field"]
  }
}
```

`saveToJson()` / `readFromJson()` on `Settings` extended to call `buildSirenJson()` / `parseSirenJson()` following existing `buildMbJson` pattern in [pluginsettings.cpp](src/pluginsettings.cpp).

> **Actual:** Implemented as a self-contained `SirenSettings` struct in `Siren.cpp` rather than extending `pluginsettings`. File paths and JSON format match the plan. The JSON key for root paths is `"rootContainers"`.

---

## Phase 2 — DataSource Interface ✅ DONE

```cpp
// SirenDataSource.hpp
struct DataSourceNode {
    std::string name;
    std::string fullPath;
    std::string relativePath;   // relative to root, '/' separated
    bool isContainer = false;
    bool childrenLoaded = false;
    float durationSeconds = 0.f;
    std::vector<DataSourceNode> children;
};

struct AudioStream {
    virtual int     channels()    const = 0;
    virtual int     sampleRate()  const = 0;
    virtual int64_t totalFrames() const = 0;
    virtual int64_t readF32(float* buffer, int64_t frameCount) = 0;
    virtual bool    seekTo(int64_t frameIndex) = 0;
};

struct DataSource {
    virtual std::string rootPath() const = 0;
    virtual bool isSupportedFile(const std::string& path) const = 0;
    virtual void loadChildrenAsync(const std::string& path, TaskWorker&,
                                   std::function<void(std::vector<DataSourceNode>)> onDone) = 0;
    virtual std::vector<DataSourceNode> loadChildrenSync(const std::string& path) = 0;
    virtual RootMetadata* getMetadata()  { return nullptr; }
    virtual bool loadAudioInfo(const std::string& id, AudioInfo& out) const { return false; }
    virtual bool decodeAudioF32(const std::string& id, std::vector<float>& samples,
                                int& channels, int& sampleRate) const { return false; }
    virtual std::unique_ptr<AudioStream> openAudioStream(const std::string& id) const { return nullptr; }
    // ... display name, relative path, timestamp helpers
};
```

`FileSystemDataSource : DataSource` — reads the OS filesystem. Returns nodes for `.wav`, `.flac`, `.mp3` files and containers (directories). Children are populated lazily on expand via `TaskWorker`. `durationSeconds` is populated during async scan via `loadAudioInfo`.

Thread sync: **generation counter** + single-slot `PendingResult` + `std::atomic<bool> pendingReady` — no mutex, no `LoadState` enum.

This interface is designed so a future data source (e.g. cloud, Rack library) can be added without changing the browser or preview widgets.

---

## Phase 3 — Sample Metadata Model ✅ DONE

```cpp
// SirenMetadata.hpp
struct SampleMetadata {
    std::string relativePath;   // relative to root
    bool favorite = false;
    std::vector<std::string> tags;
};

struct RootMetadata {
    std::string rootPath;
    std::map<std::string, SampleMetadata> samples;  // key = relativePath

    void load(const std::string& jsonPath);
    void save(const std::string& jsonPath) const;

    void setFavorite(const std::string& rel, bool fav);
    void addTag(const std::string& rel, const std::string& tag);
    void removeTag(const std::string& rel, const std::string& tag);
    std::set<std::string> allTags() const;
};
```

Tags created inline (same pattern as MB v2 custom tags): user types in a text field; if the tag doesn't exist yet it is created immediately and stored.

A **pre-defined starter tag list** is shipped with the module (e.g. `drone`, `percussion`, `loop`, `one-shot`, `vocal`, `field`, `texture`, `bass`, `fx`, `ambient`) to avoid an empty tag panel on first use. These are suggestions only — users can ignore, delete, or add to them freely. The list is never enforced and is not stored in settings unless the user assigns them to a sample.

---

## Phase 4 — Audio Decoding & Waveform Cache ✅ DONE

`SirenAudio.hpp` contains **format-agnostic** helpers only. All format-specific decode (dr_libs) is encapsulated in `FileSystemDataSource`.

```cpp
// SirenAudio.hpp
struct AudioInfo { int sampleRate, channels, bitDepth; int64_t frameCount; float durationSeconds; };

struct WaveformCache {
    std::vector<std::vector<std::pair<float,float>>> peaks;  // [channel][bucket] = {min, max}
    int     bucketCount    = 0;
    int64_t fileTimestamp  = 0;  // mtime at build time; used for cache invalidation
    bool empty() const { return peaks.empty() || bucketCount == 0; }
};

// Build peak waveform from pre-decoded interleaved float samples.
bool buildWaveformCache(int64_t timestamp, const std::vector<float>& samples,
                        int64_t frameCount, int channels, int pixelWidth, WaveformCache& out);

// JSON cache file I/O (timestamp validation: pass 0 to skip).
bool loadWaveformCacheFile(const std::string& path, int64_t expectedTimestamp, WaveformCache& out);
void saveWaveformCacheFile(const std::string& path, const WaveformCache& cache);
```

**dr_libs integration:**  
`#define DR_WAV_IMPLEMENTATION` etc. once in `SirenFileSystem.hpp` (implementation TU). `dep/drlibs/` contains the three single-header files.

**Waveform build flow (async via TaskWorker):**  
1. `loadItem()` checks cache file: if `timestamp` matches → use immediately, no decode.  
2. Otherwise dispatch to `TaskWorker`: `DataSource::decodeAudioF32` → `buildWaveformCache` → `saveWaveformCacheFile`. Result handed back via `pendingCache` + `pendingCacheReady` atomic.

**File timestamps:** `ghc::filesystem::last_write_time` → `time_since_epoch().count()`. `hashPath()` produces an 8-char hex string (CRC32 of the path) used as the cache filename.

**Cache storage:** `~/.../Stoermelder-P1/siren-cache/<hash>.json` → `{ "timestamp", "bucketCount", "channels": [[min,max], ...] }`.

---

## Phase 5 — Tree Browser Widget (Left Pane) ✅ DONE

> **Before implementing:** Study the mb-selection-browser branch — it contains a working async tree browser with `TaskWorker`-driven directory loading, spinner state, and a `ScrollWidget`-based row layout that closely matches this requirement.

```cpp
// SirenBrowserPane.hpp
struct SirenBrowserPane : widget::OpaqueWidget {
    // Header: root-container selector (dropdown of pluginSettings.sirenRootContainers)
    //         + "+" button to add new root via osdialog_file(OSDIALOG_OPEN_DIR)
    //         + "★" toggle to filter favorites only
    //         + tag filter chips (multi-select, same style as MB custom tag dropdown)
    // Body: ScrollWidget → SequentialLayout (vertical) of SirenTreeRow widgets

    std::string selectedPath;
    std::string activeRoot;
    bool favoritesOnly = false;
    std::set<std::string> tagFilter;   // AND filter

    void setRoot(const std::string& root);
    void refresh();                    // rebuild visible rows from DataSource
    // step() polls DataSource::loadState each frame; inserts child rows when READY
};

struct SirenTreeRow : widget::OpaqueWidget {
    // Indent level indicator (16px per level)
    // Container: expand/collapse triangle + container name; expand dispatches TaskWorker child load
    // File: filename + duration text + ★ favorite button
    // Selected state: highlight background
    // Single-click on file: loads preview in SirenPreviewPane
    // onDragStart / onDragEnd: initiates path-drop drag to other modules (see Phase 7)

    int indentLevel;
    DataSourceNode node;
    RootMetadata* metadata;            // for reading/writing favorites, tags
};
```

Row height: 18px. Supports keyboard navigation (↑↓ arrows, Enter to load, Space to toggle favorite) when the pane has focus.

> **Actual:** Thread sync uses generation counter + atomic bool (no `LoadState`). `ScrollWidget::container->box.size` must be set explicitly after `rebuildRowWidgets()` for the scrollbar to appear. `SirenTreeRow` shows file duration right-aligned and a `StarButton` for favorites. Tag chips are drawn directly in `SirenBrowserPane::draw()` in a fixed strip at the bottom. Header shows root container name, dropdown arrow, and favorites star toggle. `onFileSelected` and `getMetadata` are callbacks to `SirenWidget`.

---

## Phase 6 — Waveform Preview Widget (Right Pane) ✅ DONE

```cpp
// SirenPreviewPane.hpp
struct SirenPreviewPane : widget::OpaqueWidget {
    std::string  currentId;      // opaque item id from data source
    DataSource*  source;         // cached for context-menu use; never stored past loadItem
    std::string  displayName;    // human-readable name, cached at loadItem time
    std::string  relPath;        // root-relative path for metadata keying
    AudioInfo    info;
    WaveformCache cache;
    std::atomic<bool> cacheReady{false};
    std::atomic<bool> cacheBuilding{false};

    float inPoint          = 0.f;  // stored start position (set on click/drag)
    float scrubPos         = 0.f;  // drag-only position; never written by audio thread
    bool  draggingPlayhead = false;

    // Audio state lives entirely in SirenModule. The pane drives it via callbacks
    // and reads display-only atomics via raw pointers.
    std::function<void(const std::string& id, DataSource* src)> openStreamCallback;
    std::function<void(float pos)> startPlaybackCallback;
    std::function<void()>          stopPlaybackCallback;
    std::atomic<float>* modulePlayheadPos = nullptr;  // written by DSP thread
    std::atomic<bool>*  modulePlaying     = nullptr;  // written by fill thread

    void loadItem(const std::string& id, DataSource* src, RootMetadata* meta,
                  bool startPlay = false, bool forceRebuild = false);
};
```

**Layout zones (top to bottom):**

```
┌─────────────────────────────────────────────────────────────────┐
│ ▶ Harbour_Drone.wav   STEREO · 48k · 24bit   00:32.18           │  top bar (TB_H = 34px)
├─────────────────────────────────────────────────────────────────┤
│  L ──── waveform channel 1 (filled contour) ──────────────────  │
│                                                                 │  waveform area
│  R ──── waveform channel 2 (filled contour) ──────────────────  │
├─────────────────────────────────────────────────────────────────┤
│  IN  0.00s   OUT  32.18s   LEN  32.18s   POS  12.29s            │  readout (READOUT_H = 26px)
└─────────────────────────────────────────────────────────────────┘
```

**Top bar:** play/stop button (▶/■, gold when playing) · filename · STEREO/MONO badge · `48k` · `24bit` · duration.

**Waveform area:** filled closed NanoVG contour (top-peaks forward + bottom-peaks reversed), light grey. `L`/`R` channel labels on left edge. Tick marks along waveform bottom (auto-scaled interval: 0.5 s → 5 min). Playhead: white vertical line + downward triangle pointer. InPoint: gold vertical line + downward gold triangle.

**Playhead interaction:** left-click in waveform → sets `inPoint` + `scrubPos`, starts drag. `onDragMove` → calls `startPlaybackFrom(scrubPos)` on every position change (scrubbing: fill thread seeks and refills from each new position). `onDragEnd` → `startPlaybackFrom(inPoint)` for a clean restart from the final position. Top-left play/stop button area → toggle play/stop from stored `inPoint`.

**Right-click context menu:** favorite toggle + tag list with checkmarks + inline `TextField` for new tags.

**Bottom readout:** `IN` / `OUT` / `LEN` / `POS` in `mm:ss.ff` format.

**Waveform cache:** built asynchronously via `TaskWorker`. Generation counter + `pendingCacheReady` atomic bool for lock-free handoff. Serialised to `siren-cache/<8-char-hash>.json`; timestamp validation on load.

---

## Phase 7 — Drag-Drop to Other Modules ✅ DONE

Path-drop is initiated from **both** the `SirenTreeRow` (browser pane) and the `SirenPreviewPane` waveform area. Both share the same helper:

```cpp
// Shared drag helper
struct SirenDragHelper {
    std::string dragPath;   // full file path being dragged
    bool active = false;
    Vec  mousePos;          // updated in onDragMove, used to draw floating label

    void startDrag(const std::string& path);
    void endDrag();         // fires onPathDrop on widget under cursor
};
```

```cpp
void SirenDragHelper::endDrag() {
    Vec pos = APP->scene->mousePos;
    Widget* target = APP->scene->rack->getModuleAtScreenPos(pos);
    if (target) {
        event::PathDrop pd;
        pd.paths.push_back(dragPath);
        target->onPathDrop(pd);
    }
    active = false;
}
```

This fires Rack's standard `onPathDrop` on whatever module widget is under the cursor — no changes needed to target modules.

**Preview pane drag:** clicking and dragging anywhere on the waveform area initiates a drag for the currently loaded file. When trim selection is later implemented, dragging from within the selected region will instead drop the trimmed range. The exact storage location and format for the trimmed file (e.g. temp WAV, named export) is deferred to that phase.

While dragging, both panes draw a small floating label with the filename following the cursor via `drawLayer`.

> **Actual:** Implemented as `SirenDragState` struct (no methods, plain data). `onDragEnd` fires `Widget::PathDropEvent` on `APP->scene`. Floating label drawn in `SirenPreviewPane::drawLayer`.

---

## Phase 8 — Module I/O Ports & Streaming Audio Engine ✅ DONE

The module exposes **outputs only** plus one knob:
- **OUT L** / **OUT R** — stereo audio output. Mono files are duplicated to both channels.
- **PARAM_VOLUME** — output gain knob (0–2×, default 1×, displayed in dB). DSP applies `vol * 5.f` to normalize the [-1, 1] PCM range to Rack's [-5, 5] V convention.

### Streaming architecture

Audio I/O is fully streaming — no full-file decode before playback. Ownership is:

```
UI thread                 fill thread               DSP thread (process())
─────────                 ───────────               ──────────────────────
openStream()  ──atomic──▶ AudioStream* stream       rbL / rbR  ──▶  outputs
startPlayback() ──seek──▶ seekTo() + ring fill
stopPlayback()  ──flag──▶ drain + playing=false
```

**Lock-free command channel (UI → fill thread):**
- `std::atomic<AudioStream*> pendingStream` — UI transfers ownership; fill thread `exchange(nullptr)` to adopt.
- `std::atomic<int64_t> pendingSeekFrame` — ≥0 triggers a seek and resumes `playing`.
- `std::atomic<bool> pendingStop` — fill thread drains ring and clears `playing`.

**Ring buffers:** `dsp::RingBuffer<float, 8192>` for L and R (8192 frames ≈ 186 ms at 44.1 kHz). Single-producer (fill thread) / single-consumer (`process()`). No mutex on any audio path.

**Fill thread** (`std::thread fillThread`): wakes via `std::condition_variable` (5 ms timeout fallback). Reads `AudioStream::readF32` in 1024-frame chunks into the ring. Sets `eofReached` when the decoder returns 0 frames; `process()` drains the ring before stopping playback.

**Playhead tracking** (DSP thread):
- `seekBaseFrame` + `outputFrameCount` (atomic) → normalized `playheadPos` (atomic float) read by UI.
- When `seekBaseFrame + outputFrameCount >= streamTotalFrames`, `playing` is set false.

**VU meter:** `peakL` / `peakR` (DSP-only floats) decay at 30 dB/s; exported to UI as `std::atomic<float> levelL / levelR` (dBFS, −100 = silence). `SirenVuMeter` widget reads these each draw frame.

**`dataToJson()` persists:** last file path, last playhead position, active root index. Restored by `SirenWidget` constructor using the module's persisted values (patch priority) or global `sirenSettings` as fallback.

---

## Phase 9 — Tests ✅ DONE

Tests are co-located with source and follow the project pattern (`*.test.cpp` using Catch2 + `Test::TestContext` + `SYNC_MODEL`). Test file:

```
src/modules/siren/Siren.test.cpp
```

**35 test cases, 122 assertions — all passing.**

| Test case | Area | What it checks |
|-----------|------|----------------|
| `Construction and initialization` | Module | Construct without crash; OUTPUT_L/R default to 0 V |
| `JSON serialization` | Module | `dataToJson`/`dataFromJson` round-trip: `lastFile`, `lastPlayheadPos`, `activeRootIdx` |
| `RootMetadata: favorites` (3 sections) | Metadata | set/clear favorite; entry removed when no tags remain; entry kept when tags remain |
| `RootMetadata: tags` (7 sections) | Metadata | add/remove; no exact duplicates; case-insensitive dedup; first spelling wins; `allTags` union |
| `addTag: custom tag spelling is stored verbatim` | Metadata | Verbatim storage of mixed-case and hyphenated tags |
| `addTag: case-insensitive duplicate prevention` | Metadata | Three variants of same word → only first stored; different tag still accepted |
| `addTag: case-insensitive check does not affect allTags display` | Metadata | `allTags` contains exact stored spelling, not lowercase variant |
| `RootMetadata: JSON round-trip` | Metadata | `toJson` / `fromJson` preserves rootPath, favorites, tags |
| `FileSystemDataSource: supported file filter` | FileSystem | `.wav`/`.WAV`/`.flac`/`.mp3` accepted; `.txt`/`.aif`/`.png`/`.json` rejected |
| `WaveformCache: timestamp validation` (3 sections) | Audio | `fileTimestamp` field; `empty()` on default and non-empty cache |
| `hashPath produces 8-char hex string` | Utility | Output is 8 lowercase hex chars |
| `hashPath is deterministic` | Utility | Same input → same output; different inputs → different outputs |
| `Audio output: silence without loaded file` | Audio/DSP | `process()` without a stream outputs 0 V on both channels |
| `Playhead clamps to [0, 1]` (3 sections) | Preview | Below-0 clamps; above-1 clamps; `posToPlayhead` returns value in [0, 1] |
| `SirenDragState initial state` | DragDrop | `active == false`, `dragPath` empty at construction |
| `allTags: starter tags present even when samples have tags` | Metadata | Regression: custom tag + all `STARTER_TAGS` present simultaneously |
| `allTags: user tags merge with starter tags without duplicates` | Metadata | Starter tag `"drone"` added by user → count stays 1 |
| `PARAM_VOLUME: default value and range` | Module | Default = 1.0; extremes 0.0 and 2.0 accepted |
| `PARAM_VOLUME: zero volume produces silence` | Module/DSP | Vol=0 + process() → 0 V, no crash |
| `loadItem resets inPoint and scrubPos` | Preview | `loadItem("")` resets both to 0.f |
| `loadItem: no item loaded for empty id or null source` | Preview | `currentId` stays empty; no stream opened |
| `loadItem: playing stays false when no stream can be opened` | Preview | `startPlay=true` + empty id → `modulePlaying` stays false |
| `FileSystemDataSource: getMetadata returns valid pointer` | FileSystem | Non-null pointer; `rootPath` matches constructor arg |
| `FileSystemDataSource: metadata is mutable through pointer` | FileSystem | Tag added via pointer visible on second call; pointer stable |
| `toTitleCase: basic cases` | Utility | Lowercase, multi-word, hyphen, underscore, all-caps, empty string |
| `process: reads samples from ring buffer and scales by volume` | Audio/DSP | 0.5 in ring → 2.5 V out (vol=1, ×5 scale) |
| `process: stops playing when ring drained after EOF` | Audio/DSP | `eofReached=true` + empty ring → `playing` goes false, output 0 V |
| `process: ring samples consumed before EOF stop` | Audio/DSP | One frame + eofReached: first call outputs, second call stops |
| `process: VU meter updated by non-silent signal` | Audio/DSP | `levelL`/`levelR` > −100 dBFS after non-silent process() |
| `process: volume knob at zero produces silence even with ring data` | Audio/DSP | Vol=0 mutes ring output |
| `startPlayback: seekBaseFrame computed from position and total frames` | Audio | pos=0.5, total=1000 → `seekBaseFrame == 500` |
| `startPlayback: position 0 seeks to frame 0` | Audio | pos=0.0 → `seekBaseFrame == 0` |
| `startPlayback: rapid successive calls — last position wins` | Audio/Scrub | Three calls → `seekBaseFrame` and `pendingSeekFrame` reflect the last position |
| `startPlayback: outputFrameCount reset on each call` | Audio/Scrub | Counter reset to 0 on every seek so playhead is relative to the new base |
| `openStream: null source leaves pendingStream nullptr` | Audio | `openStream("", nullptr)` → `pendingStream` remains null |

**Not (unit) tested — require integration / real audio files:**
- Fill thread ring population (async; needs a mock `AudioStream` + sleep/sync)
- Actual stereo vs. mono path through a decoded file
- Waveform cache build from real PCM (covered indirectly by `buildWaveformCache` which is format-agnostic)
- `FileSystemDataSource` async directory scan (TaskWorker completion)

Test infrastructure:
- `#include "../../test/test_plugin.hpp"` and `test_context.hpp`
- `SYNC_MODEL(modelSiren, "Siren")`
- `Test::TestContext<> testContext` (file-scope)
- `Test::createModule<SirenModule>("Siren")` / `Test::destroyModule(m)`

---

## Phase 10 — Plugin Registration ✅ DONE

1. Add `extern Model* modelSiren;` to [src/plugin.hpp](src/plugin.hpp) inside the `#ifndef METAMODULE` block.
2. Add `p->addModel(modelSiren);` in [src/plugin.cpp](src/plugin.cpp).
3. Add Siren entry to [plugin.json](plugin.json):
   ```json
   { "slug": "Siren", "name": "SIREN", "description": "Sample browser", "tags": ["Utility"] }
   ```
4. Add `src/modules/siren/Siren.cpp` to `SOURCES` in `Makefile`.
5. Add `src/modules/siren/Siren.test.cpp` to the test build in `Makefile`.

---

## Future Features (Planned, Not Initially Implemented)

### Trim / Sample Selection
- IN and OUT handle widgets in `SirenPreviewPane` are already rendered (as non-interactive placeholders).
- When implemented: mouse-drag handles set `inPoint` / `outPoint` in samples; "Export Trim" context menu writes a new WAV via `dr_wav` to a user-chosen path.

### ~~Playhead Scrubbing~~ ✅ DONE
`onDragMove` calls `startPlaybackFrom(scrubPos)` on every position change. `pendingSeekFrame` (a single atomic) is overwritten by rapid calls — the fill thread always picks up the latest position, seeks `AudioStream`, clears the ring buffers, and starts refilling. DSP output follows within ≤5 ms. Implementation: one `if (newPos != scrubPos)` guard + one `startPlaybackFrom` call in `onDragMove`.

### Volume Curve
- NanoVG overlay in `SirenPreviewPane` drawing a Bezier/spline envelope.
- Control points stored per-sample in `RootMetadata`.
- Applied at export time by multiplying samples by the envelope.
- Architecture: `VolumeEnvelope` struct with `std::vector<Vec> controlPoints`, serialized in `siren-<root>.json`.

---

## Verification

**Automated (primary):** Tests in `Siren.test.cpp` are written from the very first commit, covering all logic units (see Phase 9). Run with the project's existing test runner (`make tests` or equivalent). Tests must pass before any feature is considered done.

**Manual smoke test after build:**
1. `make` — zero errors, Siren appears in the plugin browser.
2. Add a root container → tree populates with `.wav`/`.flac`/`.mp3` files, containers expand asynchronously without UI freeze.
3. Click a file → waveform renders in preview pane; filename, STEREO/MONO badge, sample rate, bit depth, and duration are correct.
4. Click in waveform → playhead moves to that position and audio begins playing through OUT L/R immediately.
5. Star a file → `siren-<hash>.json` appears in `~/.../Stoermelder-P1/` with `favorites` entry.
6. Add a tag → chip appears; reopen Rack → tag persists.
7. Second load of same file → waveform loads instantly from cache (no re-decode), confirmed by `siren-cache/` file present with matching timestamp.
8. Drag a file from browser or preview onto a sampler module that handles `onPathDrop` → that module loads the file.
9. Close and reopen Rack → root containers, last selected file, and playhead position are restored.

---

## Critical Files

| File | Role |
|------|------|
| [src/modules/siren/Siren.cpp](src/modules/siren/Siren.cpp) | `SirenModule` (fill thread, DSP, JSON) + `SirenWidget` |
| [src/modules/siren/SirenDataSource.hpp](src/modules/siren/SirenDataSource.hpp) | `DataSource` + `AudioStream` abstract interfaces |
| [src/modules/siren/SirenFileSystem.hpp](src/modules/siren/SirenFileSystem.hpp) | `FileSystemDataSource`: dr_libs decode + async dir scan |
| [src/modules/siren/SirenAudio.hpp](src/modules/siren/SirenAudio.hpp) | Format-agnostic `WaveformCache` helpers |
| [src/modules/siren/SirenPreviewPane.hpp](src/modules/siren/SirenPreviewPane.hpp) | Waveform preview, callback wiring to module |
| [src/modules/siren/SirenBrowserPane.hpp](src/modules/siren/SirenBrowserPane.hpp) | File tree browser + `SirenDragState` |
| [src/modules/siren/SirenVuMeter.hpp](src/modules/siren/SirenVuMeter.hpp) | Two-channel LED bar, reads `levelL`/`levelR` atomics |
| [src/modules/siren/SirenMetadata.hpp](src/modules/siren/SirenMetadata.hpp) | `RootMetadata`: tags, favorites, JSON I/O |
| [src/plugin.hpp](src/plugin.hpp) | `extern Model* modelSiren` |
| [src/plugin.cpp](src/plugin.cpp) | `p->addModel(modelSiren)` |
| [plugin.json](plugin.json) | Module manifest entry |
| [dep/drlibs/](dep/drlibs/) | dr_wav.h / dr_flac.h / dr_mp3.h |

### Key Reuse from Existing Code
- `ThemedModuleWidget` — base class for `SirenWidget`
- MB v2 tag creation inline text-field pattern — `NewTagField` in `SirenPreviewPane`
- `osdialog_file(OSDIALOG_OPEN_DIR)` — folder picker for adding roots
- `TaskWorker` — async dir scan and waveform cache build
