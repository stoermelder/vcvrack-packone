# stoermelder SIREN

SIREN is an audio file browser and playback module: you can drop samples out of the browser onto your sampler modules. 

**Features**

- Streams WAV, FLAC, and MP3 from disk; no full-file loading.
- Drag-and-drop onto any compatible module, with optional resampling or WAV conversion.
- Multiple root folders, switchable from the source menu.
- Search by file or folder name with live filtering and keyboard navigation.
- Favorites and a per-root metadata store (tags, favorites) shared between SIREN instances.
- Tag filter with include and exclude states; per-file or per-folder tag editing.
- Automatic tag suggestions driven by an on-device audio classifier (18-tag vocabulary).
- Waveform preview with trim IN/OUT handles, looping, zoom, and scrubbing.
- Resampling on playback and on drop, with selectable quality.


## Setting Up

SIREN browses one folder at a time, called a *root container*. To add one:

1. Click the **source button** (top-left of the display) to open its menu.
2. Select **Add root…** and choose a folder.

You can add as many roots as you like and switch between them via the same menu. Each root has a checkmark in the menu — click another entry to switch to it. Use **Remove root** to delete the currently active root. Tags and favorites are stored per root and recalled automatically.

The lower part of the source menu controls how audio is processed:

- **Resample on playback** — resample on the fly to Rack's current sample rate while SIREN plays through its own outputs. Has no effect on the file on disk.
- **Resample on drop** — resample to Rack's current sample rate before handing the file to another module. Prevents pitch shifting in modules that don't resample themselves.
- **Resample quality** — choose between **Fast** (lowest CPU), **Default** (balanced), and **Best** (highest quality, most CPU). Affects resampling on drop only.
- **Convert to WAV on drop** — convert FLAC or MP3 files to WAV before dropping. Useful for modules that only accept WAV.

## Drag and Drop

**Drag** any file from the browser onto a compatible module to load it there. If a trim region is set, only that region is prepared for the drop.

You can also start a drag from anywhere in your patch: hold `Ctrl/Cmd + Shift` and click, then drag — SIREN will load the last selected sample and start a drag-and-drop from that point.

The **Resample on drop**, **Resample quality**, and **Convert to WAV on drop** options in the source menu (see [Adding a Root Folder](#adding-a-root-folder)) control how the file is prepared when it is handed to another module. **Resample on playback** applies to SIREN's own L/R outputs and has no effect on drops.

## Browsing Files

The left pane shows a folder tree. Click a **▶** triangle to expand a folder; click **▼** to collapse it.

Click any **audio file** to load it into the preview pane. Hovering over a file shows a tooltip with its name, duration, and any tags assigned to it.

### Search

Type in the **Search** field (top-right of the display) to filter the tree. Search matches:
- file names
- folder names — a folder is shown if its name matches, along with all its contents
- files whose parent folder name matches

Press **Escape** or double-click the field to clear the search.

## Keyboard Navigation

With the browser focused, use the arrow keys to move through the tree:

| Key | Action |
|---|---|
| ↑ / ↓ | Move selection up or down |
| → | Expand a folder / move into it |
| ← | Collapse a folder / move to parent |

### Favorites Filter

Click the **☆ button** (next to the source button) to show only favorited files.

### Tag Filter

The **tag bar** at the bottom of the browser lists all known tags as chips. Clicking a chip cycles its state through three modes:

| State | Appearance | Effect |
|---|---|---|
| Off | dim chip | tag is ignored when filtering |
| Included | highlighted chip | browser shows only files carrying this tag |
| Excluded | red, strike-through chip | browser hides files carrying this tag |

Multiple chips can be active at once, and include and exclude states can be mixed. When several included tags are active, only files carrying **all** of them are shown; any excluded tag further removes files that carry it.

**Right-click** the tag bar for filter actions:

- **Clear tag filters** — reset both included and excluded chips.
- **Clear included tag filter** — keep only excludes.
- **Clear excluded tag filter** — keep only includes.

## Playing Files

**Click** a file to load it. **Shift-click** (or click while the **Autoplay** button is lit) to load *and* start playback immediately.

The **Autoplay button** (below the volume knob) toggles automatic playback on every selection. When lit, any file you click starts playing straight away. Shift-click always plays regardless of the Autoplay setting.

Press **Space** to toggle play/stop from anywhere.

### Waveform Preview

The right pane shows the waveform of the loaded file. During playback a vertical cursor marks the current position.

**Click** anywhere on the waveform to jump to that position. **Click and drag** to scrub.

### Trim Points

Two handles at the top edge of the waveform define the playback range:

- Drag the **left handle** holding *Shift* to set the start point.
- Drag the **right handle** holding *Shift* to set the end point.

The highlighted region between them is the active playback range.

### Loop

Click the **Loop** context menu option to repeat playback between the IN and OUT points. When loop is off, playback stops at the OUT point.

## Organizing Files

### Favorites

Click the **star** at the right edge of any file row to toggle it as a favorite. Use the ☆ button in the top bar to show only favorites.

### Tags

Right-click a file to open its context menu. Under **Tags**:

- Type in the text field and press **Enter** to create and apply a new tag.
- Click any tag name in the list to toggle it on or off for that file. A checkmark indicates the tag is currently applied.

Tags are case-insensitive — "Kick" and "kick" are treated as the same tag.

To tag all audio files inside a folder at once, right-click the folder and use **Tag all files**.

### Suggest Tags

SIREN can suggest tags automatically from the audio itself. Right-click any file or folder in the browser and pick **Suggest tags** to start a one-shot analysis. A streaming dialog opens that fills with proposals as each file is processed.

SIREN analyses the first 30 seconds of audio, extracts a feature vector, and proposes up to five tags from a fixed vocabulary of 18 entries: **Acoustic, Atmospheric, Bass, Clap, Cymbal, Drone, Drums, FX, Glitch, HiHat, Kick, Lead, Loop, Nature, Noise, Pad, Snare, Vocal**. Each proposal shows the file it would be applied to and a confidence score; only suggestions scoring at or above 50% are listed.

The dialog groups suggestions by tag. **Check** the ones you want to apply and confirm — checked tags are added to the matching files in the active root. Already-applied tags are filtered out of the proposal list, so you only see tags that are genuinely new for each file. Right-click a proposal to remove it from its group before confirming.

## Keyboard Shortcuts

| Key | Action |
|---|---|
| `Space` | Play / Stop |
| `↑` / `↓` / `←` / `→` | Navigate browser |
| `+` or `]` | Zoom in |
| `−` or `[` | Zoom out |
| `0` | Fit waveform to width |
| `Escape` | Clear search |

## Tips

- **Searching large libraries** — The search field filters live as you type and also matches folder names, so you can find a subfolder quickly without expanding the whole tree.

- **Trimming before drop** — Set IN/OUT trim points first, then drag the file. The receiving module gets only the trimmed segment.

- **Metadata location** — Tags and favorites are saved as a JSON file alongside each root container. They survive module removal and can be backed up or shared.

- **Waveform cache** — The first time a file is previewed, SIREN builds a waveform cache in your Rack user folder (`Stoermelder-P1/siren-cache/`). Subsequent opens are instant.

## Changelog

- v2.x.0
    - Initial release