# Siren: VCV Rack Sample Browser Module — Implementation Plan

## Context

Siren is a new wide-panel VCV Rack module that provides a file-system sample browser with waveform preview, drag-to-other-modules support, user tagging/favorites, and groundwork for future trim/volume-curve editing. It follows the same settings-persistence and UI patterns already established in this plugin (MB, MidiCat).

---

## Architecture Overview

```
SirenModule (rack::Module)
  └─ audio output ports, dataToJson/dataFromJson (patch state)

SirenWidget (ThemedModuleWidget<SirenModule>)
  ├─ SirenBrowserPane  (left ~35%)  — tree browser
  └─ SirenPreviewPane  (right ~65%) — waveform + metadata bar
```

Global state (root folders, UI prefs) lives in `pluginSettings` and persists to separate files. Per-root metadata (tags, favorites) lives in root-scoped JSON files.

> **Actual:** `SirenSettings` is a self-contained struct in `Siren.cpp` (not extending `pluginsettings`). Patch-local state (last file, playhead, active root index) is also persisted in `dataToJson`/`dataFromJson` and takes priority over global settings on restore.

---

## File Structure

```
src/modules/siren/
  Siren.cpp              — module + widget, entry point
  SirenDataSource.hpp    — abstract DataSource interface
  SirenFileSystem.hpp    — filesystem DataSource implementation
  SirenMetadata.hpp      — SampleEntry, tag/favorite model, JSON I/O
  SirenAudio.hpp         — dr_libs wrappers, WaveformCache
  SirenBrowserPane.hpp   — left tree-browser widget
  SirenPreviewPane.hpp   — right waveform preview widget
  Siren.test.cpp         — Catch2 unit tests (co-located, follows project pattern)

dep/drlibs/
  dr_wav.h
  dr_flac.h
  dr_mp3.h               — header-only; #define DR_*_IMPLEMENTATION in Siren.cpp
```

---

## Phase 1 — Settings & Persistence ✅ DONE

### File paths (follow pluginsettings.cpp pattern)
```cpp
// In pluginsettings.cpp / pluginsettings.hpp additions:
std::vector<std::string> sirenRootFolders;
int    sirenSortMode  = 0;
bool   sirenShowExts  = true;

static std::string sirenFilePath()  { return settingsDirPath() + "/siren.json"; }
// per-root: settingsDirPath() + "/siren-" + hashPath(root) + ".json"
```

`siren.json` stores: root folder paths, UI state (last selected file, scroll positions, sort mode).

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

> **Actual:** Implemented as a self-contained `SirenSettings` struct in `Siren.cpp` rather than extending `pluginsettings`. File paths and JSON format match the plan.

---

## Phase 2 — DataSource Interface ✅ DONE

```cpp
// SirenDataSource.hpp
struct DataSourceNode {
    std::string name;
    std::string fullPath;
    bool isDirectory;
    bool isExpanded = false;
    std::vector<DataSourceNode> children;  // populated on expand
};

struct DataSource {
    virtual std::string rootPath() const = 0;
    virtual std::vector<DataSourceNode> children(const std::string& path) = 0;
    virtual bool isSupportedFile(const std::string& path) const = 0;
};
```

`FileSystemDataSource : DataSource` — reads the OS filesystem. Returns nodes for `.wav`, `.flac`, `.mp3` files and directories. Children are populated lazily on expand.

**Async population (reference mb-selection-browser branch for the proven pattern):**  
Directory listing is dispatched through `TaskWorker` (see `src/utils/TaskWorker.hpp`). The DataSource holds a `std::atomic<LoadState>` (IDLE → LOADING → READY). `SirenBrowserPane` polls this state in `step()` and inserts child rows only when state transitions to READY. A spinner indicator is shown while loading. This prevents UI freezes on large folder trees.

This interface is designed so a future data source (e.g. cloud, Rack library) can be added without changing the browser widget.

> **Actual:** `DataSourceNode` also carries `relativePath`, `childrenLoaded`, and `durationSeconds`. Thread sync uses a **generation counter** (`std::atomic<int> treeGeneration`) + single-slot `PendingResult` + `std::atomic<bool> pendingReady` instead of `LoadState` enum — no mutex. `FileSystemDataSource` populates `durationSeconds` during async scan via `loadAudioInfo`.

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

```cpp
// SirenAudio.hpp
struct AudioInfo {
    int    sampleRate;
    int    channels;
    int    bitDepth;
    int64_t frameCount;
    float  durationSeconds;
};

struct WaveformCache {
    // Downsampled peak pairs (min/max) per channel, one per display pixel bucket
    std::vector<std::vector<std::pair<float,float>>> peaks;  // [channel][bucket]
    int bucketCount;
    int64_t fileTimestamp;   // mtime at build time, for cache invalidation
};

// Decodes header only (fast path for metadata)
bool loadAudioInfo(const std::string& path, AudioInfo& out);

// Decodes full file and builds waveform peaks for display
bool buildWaveformCache(const std::string& path, int pixelWidth, WaveformCache& out);
```

**dr_libs integration:**  
- `#define DR_WAV_IMPLEMENTATION`, `#define DR_FLAC_IMPLEMENTATION`, `#define DR_MP3_IMPLEMENTATION` once in `Siren.cpp`.  
- `dep/drlibs/` contains the three single-header files.  
- `Makefile` already picks up `dep/` includes; just add the headers.

Waveform building is dispatched through `TaskWorker` (same instance owned by the module/widget). The widget polls a `std::atomic<bool> cacheReady` flag each draw frame.

**File timestamps via `ghc::filesystem`:**  
Use `#include <ghc/filesystem.hpp>` (available at `Rack/dep/include/ghc/filesystem.hpp`, already on the include path). File modification time:
```cpp
auto mtime = ghc::filesystem::last_write_time(path);
int64_t timestamp = mtime.time_since_epoch().count();
```

**Waveform cache persistence:**  
Serialized peak data is stored as JSON alongside the settings files:
```
~/.../Stoermelder-P1/siren-cache/
  <8-char-hash-of-full-path>.json   — { "path", "timestamp", "peaks": [...] }
```
On load, cache file is read first; if `timestamp` matches the file's current `mtime` via `ghc::filesystem::last_write_time`, the peaks are used directly — no decode needed. If `timestamp` differs or cache is absent, `TaskWorker` decodes and overwrites the cache file. Reference mb-selection-browser for the async decode + cache-check pattern.

---

## Phase 5 — Tree Browser Widget (Left Pane) ✅ DONE

> **Before implementing:** Study the mb-selection-browser branch — it contains a working async tree browser with `TaskWorker`-driven directory loading, spinner state, and a `ScrollWidget`-based row layout that closely matches this requirement.

```cpp
// SirenBrowserPane.hpp
struct SirenBrowserPane : widget::OpaqueWidget {
    // Header: root-folder selector (dropdown of pluginSettings.sirenRootFolders)
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
    // Folder: expand/collapse triangle + folder name; expand dispatches TaskWorker child load
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

> **Actual:** Thread sync uses generation counter + atomic bool (no `LoadState`). `ScrollWidget::container->box.size` must be set explicitly after `rebuildRowWidgets()` for the scrollbar to appear. `SirenTreeRow` shows file duration right-aligned and a `StarButton` for favorites. Tag chips are drawn directly in `SirenBrowserPane::draw()` in a fixed strip at the bottom. Header shows root folder name, dropdown arrow, and favorites star toggle. `onFileSelected` and `getMetadata` are callbacks to `SirenWidget`.

---

## Phase 6 — Waveform Preview Widget (Right Pane) ✅ DONE

```cpp
// SirenPreviewPane.hpp
struct SirenPreviewPane : widget::OpaqueWidget {
    std::string currentPath;
    AudioInfo   info;
    WaveformCache cache;
    std::atomic<bool> cacheReady{false};
    float playheadPos = 0.f;       // normalized 0.0–1.0 across waveform width
    bool  draggingPlayhead = false;

    void loadFile(const std::string& path, RootMetadata* meta);
    void drawLayer(const DrawArgs& args, int layer) override;
};
```

**Layout zones (top to bottom, matching screenshot):**

```
┌─────────────────────────────────────────────────────────────────┬──────┐
│ ▶ Harbour_Drone.wav   STEREO  48k · 24bit  00:32.18    SIREN    │      │
├─────────────────────────────────────────────────────────────────┤      │
│                                                                 │  L   │
│  L ──── waveform channel 1 (peaks) ──────────────────────────   │  ●   │
│                                                                 │      │
│  R ──── waveform channel 2 (peaks) ──────────────────────────   │  R   │
│                                                                 │  ●   │
├─────────────────────────────────────────────────────────────────┤      │
│  IN  7.080s   OUT  20.595s   LEN  13.516s   POS  12.293s        │      │
└─────────────────────────────────────────────────────────────────┴──────┘
```

**Top bar** (single row, drawn or widget-based):
- Play/stop button (triangle icon)
- Filename (bold)
- STEREO / MONO badge
- Sample rate (`48k`), bit depth (`24bit`)
- Total duration (`00:32.18`)
- Module name label `SIREN` (right-aligned, decorative)

**Waveform area** (NanoVG, `drawLayer` layer 1):
- Dark background `#1a1a12`
- Per-channel waveform drawn as filled peak rectangles (min/max per pixel bucket), light grey `#c8c8b4`
- `L` / `R` channel labels on left edge
- If stereo, the two channels share the vertical space equally with a small gap
- Selection region (future trim hook): dashed gold border rect + two `||` drag handles, rendered as non-interactive placeholders from the start
- Playhead: thin vertical gold/white line at `playheadPos`

**Playhead interaction:**
- Any click inside the waveform area immediately places the playhead at that X position AND starts playback from `playheadPos * durationSeconds` (no separate play button needed — clicking the waveform IS the play action).
- Dragging moves the playhead continuously; playback seeks and restarts on `onDragEnd`.
- Scrubbing (audio during drag) is a future feature.
- A right-click context menu on the waveform offers "Drag to module" to initiate a path-drop.

**Bottom readout bar** (fixed-width monospace labels):
- `IN` — start of selection (0.000s until trim is implemented)
- `OUT` — end of selection (= duration until trim is implemented)
- `LEN` — selection length
- `POS` — current playhead position in seconds, updates live during drag

**Right-side level circles:**
- One circle per channel (L, R), labeled
- Fill color derived from RMS of the peak data (static, not live metering)
- Serve as a quick visual loudness indicator

**Tag editor (below waveform area):**
- Row of tag chip buttons for tags assigned to the current sample
- `+` button opens an inline `TextField` for typing a new tag (created immediately on Enter, same pattern as MB v2 custom tag creation)

> **Actual deviations:**
> - Waveform rendered as a **filled closed contour** (top peaks forward + bottom peaks reverse = closed NanoVG path), not rectangular bars.
> - **Level circles removed** at user request.
> - **Background rects (top bar, waveform area) removed** at user request — SVG provides all backgrounds.
> - Layout constants: `TB_H = 34.f`, `READOUT_H = 26.f`, `WAVE_X = 8.f`. No `LEVEL_W`.
> - Playhead: white vertical line + downward triangle pointer at top edge.
> - Tick marks added along waveform bottom edge (auto-scaled interval).
> - Playhead drag uses `e.mouseDelta.x` (widget-space, handles rack zoom correctly).
> - Bug fixed: successive selects of same file restart playback (state reset in `loadFile`).
> - Thread sync: generation counter + atomic bool, same pattern as browser pane.

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

## Phase 8 — Module I/O Ports & Audio Engine ✅ DONE

The module exposes **outputs only**:
- **OUT L** — left channel audio output
- **OUT R** — right channel audio output

No inputs in the initial release. Preview playback is UI-driven (play button in the preview pane). The audio thread reads from a decoded sample buffer (populated by the same worker thread that builds the waveform cache). Mono files are summed to both outputs.

`dataToJson()` persists: last loaded file path, IN/OUT markers (zeroed initially), active root path.

---

## Phase 9 — Tests ⚠️ PARTIAL

Tests are co-located with source and follow the project pattern (`*.test.cpp` using Catch2 + `Test::TestContext` + `SYNC_MODEL`). Test file:

```
src/modules/siren/Siren.test.cpp
```

**Test cases to write from the start:**

| Test case | Status | What it checks |
|-----------|--------|---------------|
| `Construction and initialization` | ❌ TODO | `SirenModule` and `SirenWidget` construct without crash; outputs default to 0 channels |
| `JSON serialization` | ✅ DONE | `dataToJson` / `dataFromJson` round-trips: active root path, last loaded file, playhead pos |
| `RootMetadata: favorites` | ❌ TODO | `setFavorite(rel, true)` adds to favorites; `setFavorite(rel, false)` removes; `load`/`save` round-trip preserves state |
| `RootMetadata: tags` | ❌ TODO | `addTag` / `removeTag` work; `allTags()` returns union; inline creation works |
| `RootMetadata: JSON file I/O` | ❌ TODO | writes `siren-<hash>.json` and reads it back correctly |
| `WaveformCache: timestamp invalidation` | ❌ TODO | if stored timestamp != current `mtime`, cache is rebuilt; if equal, peaks are reused |
| `FileSystemDataSource: supported file filter` | ❌ TODO | `.wav`, `.flac`, `.mp3` accepted; `.txt`, `.aif` rejected |
| `FileSystemDataSource: async load state` | ❌ TODO | state transitions IDLE → LOADING → READY after `TaskWorker` completes |
| `SirenDragHelper: path drop` | ❌ TODO | on `endDrag`, `onPathDrop` is called on the widget at cursor position |
| `Audio output: stereo` | ❌ TODO | decoded stereo file writes non-zero values to both OUTPUT_L and OUTPUT_R |
| `Audio output: mono to stereo` | ❌ TODO | decoded mono file writes same value to both channels |
| `Playhead: drag clamps to [0,1]` | ❌ TODO | dragging playhead beyond edges stays within valid range |

Test infrastructure used:
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

### Playhead Scrubbing
- Currently: playhead snaps to drag position and triggers playback on mouse release.
- Future: continuous audio output during drag (scrubbing). Requires low-latency seek in the decoded buffer; architecture is compatible since the buffer is already decoded by the time the user can interact with it.

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
2. Add a root folder → tree populates with `.wav`/`.flac`/`.mp3` files, directories expand asynchronously without UI freeze.
3. Click a file → waveform renders in preview pane; filename, STEREO/MONO badge, sample rate, bit depth, and duration are correct.
4. Click in waveform → playhead moves to that position and audio begins playing through OUT L/R immediately.
5. Star a file → `siren-<hash>.json` appears in `~/.../Stoermelder-P1/` with `favorites` entry.
6. Add a tag → chip appears; reopen Rack → tag persists.
7. Second load of same file → waveform loads instantly from cache (no re-decode), confirmed by `siren-cache/` file present with matching timestamp.
8. Drag a file from browser or preview onto a sampler module that handles `onPathDrop` → that module loads the file.
9. Close and reopen Rack → root folders, last selected file, and playhead position are restored.

---

## Critical Files

| File | Role |
|------|------|
| [src/modules/siren/Siren.cpp](src/modules/siren/Siren.cpp) | Module + widget + build definitions |
| [src/pluginsettings.hpp](src/pluginsettings.hpp) | Add `sirenRootFolders` + UI state fields |
| [src/pluginsettings.cpp](src/pluginsettings.cpp) | Add `buildSirenJson` / `parseSirenJson` |
| [src/plugin.hpp](src/plugin.hpp) | Add `extern Model* modelSiren` |
| [src/plugin.cpp](src/plugin.cpp) | Register model |
| [plugin.json](plugin.json) | Module manifest entry |
| [dep/drlibs/dr_wav.h](dep/drlibs/dr_wav.h) | WAV decode (add file) |
| [dep/drlibs/dr_flac.h](dep/drlibs/dr_flac.h) | FLAC decode (add file) |
| [dep/drlibs/dr_mp3.h](dep/drlibs/dr_mp3.h) | MP3 decode (add file) |

### Key Reuse from Existing Code
- `pluginsettings.cpp:loadJsonFile` / `saveJsonFile` — reuse directly for siren-\*.json
- `ThemedModuleWidget` — base class for `SirenWidget`
- MB v2 tag creation inline text-field pattern — replicate for Siren tag editor
- MB v2 dropdown with search filter — replicate for tag filter chip row
- `osdialog_file(OSDIALOG_OPEN_DIR)` — folder picker for adding roots
