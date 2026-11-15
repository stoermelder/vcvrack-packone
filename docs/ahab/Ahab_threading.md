<!-- manual:exclude-file -->
# AHAB: threading

AHAB's simulator (`AhabSim`) is touched by two threads: the audio thread (DSP) runs `process()` and
`step()`, the UI thread draws, edits and saves.

## The rule

`field_.buffer`, `mbuf_.buffer` and the undo/redo deques **change shape** (reallocate, resize,
push, pop) only inside a DSP section (the command drain) or under Rack's exclusive engine lock
(`fromJson`, `onReset`). The UI reads those buffers and touches the deques only inside a UI
section, and keeps no pointer into them after leaving it. Everything else crossing threads is
atomic or a tolerated dirty read. Code on the DSP thread never calls a UI-section method.

The primitive is `UiDspExclusion` (`src/utils/UiDspExclusion.hpp`), the member `AhabSim::exclusion`.

## The one DSP section

`AhabSim::process()` drains the UI command queue inside a DSP section. `setFieldSize`,
`replaceField`, `undo` and `redo` run only there. If the UI is inside a section, the drain is
skipped and `pending_ui_update_` stays set, so the commands wait for the next `process()` call and
nothing is lost.

The clock path needs no section. `step()` and `orca_run` write the field in place, which is only a
dirty read for the UI, and `step()` never reallocates: the mark buffer is allocated at
`MAX_FIELD_HEIGHT × MAX_FIELD_WIDTH` in the constructor. So no tick is ever delayed; only edits
wait, and the queue keeps them in order.

## UI sections

Each is a few microseconds (one copy of at most 10 KB, a deque push or pop, or the base64 string
in `toJson`). Playback continues meanwhile.

| Method | Why |
|---|---|
| `copyForDisplay()` | the widget's only way to get field and mark data |
| `pushUndo()` | copies the field, pushes/pops the deques |
| `setUndoLimit()`, `resetUndo()` | pop the deques |
| `toJson()`, `saveToFile()` | read the field with its size |

`*Request` methods call `pushUndo()` and therefore enter a section. Sections nest.
`getFieldBuffer()` and `getMbufBuffer()` are DSP-thread only (commander, single-thread tests).

## Deadlock rule

A UI section entered on the DSP thread inside the drain spins forever. Outside the drain it breaks
the one-UI-thread assumption (`uiDepth` is a plain `int`). Never call a UI-section method from DSP
code (`AhabCommander` uses only `step`, `pasteCells`, `cutRect`, `getFieldBuffer` and
`setTickNumber`).

## No widget code on the audio thread

The DSP bumps two counters, `displaySeq` (after `step()` and after a drain) and `resetEpoch`
(`reset()`, `replaceField()`). `AhabSimWidget::step()` polls them once per frame and does the
display copy and the cursor reset itself.

## Tolerated dirty reads

Stale or torn values are not bugs here; only crash-class races are (use-after-free, out-of-bounds,
two threads modifying one container). Tolerated: glyph bytes read while `orca_run` writes in place,
`getFieldHeight()`/`getFieldWidth()`/`getDisplayBuffer()` outside a section, undo counts
(`canUndo()`, `getUndoCount()`), and plain settings fields.

## Test

`Ahab.test.threads.hpp` runs a real DSP thread against UI edits, undo/redo, resizes, saves and
module creation. The default run only has to not crash. Under the thread sanitizer it must report
nothing but the dirty reads above:

```
rm -rf build/test && make -j8 DEBUGPLUGIN=1 SANITIZER=thread testrun-one NAME=Ahab FILTER='[threads]'
```
