# MIDI-KIT scripting reference

MIDI-KIT scripts run in one of two embedded engines, chosen by the `@engine`
header tag: **QuickJs** (a full JavaScript engine) or **Lua** (a sandboxed Lua 5.4 via
minilua). Both engines expose the *same* API (`midi`, `midiOut`, `input`,
`trig`, `param`, `number`, `rack`) with 1-based indices for ports,
channels, and params. Pick the engine per script; the module identifies it
from the header, not the file extension.

Implementation: [MidiScriptEngineQuickJs.h](MidiScriptEngineQuickJs.h) (uses
[quickjs.h](../../../dep/quickjs/quickjs.h)) and [MidiScriptEngineLua.h](MidiScriptEngineLua.h)
(uses [minilua.h](minilua.h)/[minilua.c](minilua.c)). Shared interface:
[MidiScriptEngine.h](MidiScriptEngine.h).

## When to write QuickJs (JavaScript) vs Lua

Both engines are similarly capable for the common case (reacting to
`rack.onMidiMessage`, building/sending messages). Pick based on these differences:

| | QuickJs (JS) | Lua |
|---|---|---|
| Language completeness | Full JavaScript (ES2020): `while`, `switch`, `try`, `class`, `new`, `this`, `var`/`let`/`const`, function declarations, arrow functions — see [QuickJS language support](#quickjs-language-support) | Full Lua 5.4 syntax; only the *library* is trimmed |
| Data structures | Array literals `[1,2,3]`, object literals `{a:1}` | Only tables (`{}`); no literal array sugar, must use `{ {...}, {...} }` and `#t`/`ipairs` |
| Stdlib | Full JS standard library: `Math`, `JSON`, `String`, `Array`, ... | Real Lua stdlib subset: `math`, `string`, `table` (no `io`, `os`, `package`, `debug` — sandboxed) |
| String formatting | JS auto-coerces numbers in `+` concatenation; `number.toString()` helper available | Lua auto-coerces numbers in `..` concatenation; `string.format` available |
| Familiarity | Preferred if the user/preset is JS-oriented or ports logic from another JS script | Preferred if the script needs `string.format`, `table.sort`, pattern matching, or other real stdlib features |
| Performance/footprint | QuickJS is a full embeddable JS engine with a 1 MiB memory limit | minilua is a stripped full Lua VM; similarly small footprint |

Default guidance: **match whatever engine sibling/companion scripts in the
same preset pair already use** (e.g. a generator + converter pair should
both be Lua or both be QuickJs, so users can read them side by side). If there's
no existing convention, prefer Lua for anything doing string formatting or
table sorting, and QuickJs for anything that benefits from array/object literal
syntax or is adapted from existing JS example scripts. Both are full languages,
so either is a safe choice for non-trivial control flow.

## Required file header

The engine is selected by parsing `@key value` tags out of the leading
comment block only — nothing after the block is scanned for tags.

QuickJs (JS-style `/** ... */`):
```javascript
/**
 * @target stoermelder MIDI-KIT
 * @engine QuickJs
 * @author yourname
 * @description One-line summary shown in the module log on load
 */
```

Lua (Lua-style `--[[ ... --]]`):
```lua
--[[
@target stoermelder MIDI-KIT
@engine Lua
@author yourname
@description One-line summary shown in the module log on load
--]]
```

`@engine` is mandatory and must exactly match `QuickJs` or `Lua` for the
respective loader, or the script is rejected. `@author`/`@description` are
optional but get echoed to the module's log on load. `@target` is
conventionally present but not checked by the loader.

## Script structure

- Top-level code runs once, synchronously, when the script is (re)loaded.
- `rack.onMidiMessage(midiPort, msg)` is called for every incoming MIDI
  message (`midiPort` is 1-based). Define it as a method on the `rack`
  object — it's the only callback the engine looks for per message; a
  script that never defines it loads fine but silently ignores all incoming
  MIDI (logged once at load time). In QuickJs, assign it to the `rack`
  object — `rack.onMidiMessage = function(midiPort, msg) {...}`. Lua
  likewise: `rack.onMidiMessage = function(...) end` or
  `rack.onMidiMessage = function(midiPort, msg) end`.
- Optional `input.getName(i)`, `param.getName(i)`, `param.getValueFormat(i)`
  functions may be overridden to customize panel/input labeling; both
  engines seed defaults (`"Port " .. i` / `"Param " .. i`) that scripts can
  replace by reassigning the table field.
- Optional `rack.onTrigger(trigPort)` is called whenever the module's
  trigger/gate input (`trigPort`, 1-based — MIDI-KIT currently exposes a
  single trigger input, so this is always `1`) crosses the trigger
  threshold. It is the only way to run script logic that isn't driven by an
  incoming MIDI message — e.g. reading `trig.getTicks()`/`input.*` and
  sending a MIDI message in response to an external clock/gate. A script
  that never defines it simply never has it called; unlike
  `rack.onMidiMessage`, there is no load-time log warning for omitting it.
  In QuickJs, assign it to the `rack` object —
  `rack.onTrigger = function(trigPort) {...}`. Lua likewise:
  `rack.onTrigger = function(trigPort) end` or
  `function rack.onTrigger(trigPort) end`.
- There is no per-sample or per-frame callback — logic only runs in
  response to incoming MIDI messages (including clock 0xF8 realtime bytes)
  or trigger-input ticks via `rack.onTrigger`.
- Optional `rack.onLoad()` and `rack.onUnload()` hooks run once each:
  - `rack.onLoad()` runs once, right after top-level code, when the script
    has parsed and loaded successfully.
  - `rack.onUnload()` runs once, right before the *current* script's state
    is torn down — because it's about to be replaced by another script, the
    module was reset, or the module is being removed from the patch. This
    is the only place a script can reliably clean up: sending an
    all-notes-off for anything it left sounding is the main use case, since
    nothing else will ever get a chance to release those notes once the
    script's own state is gone.
  - Both can call `midi.create()`/`midiOut.send()` like `rack.onMidiMessage`
    can; messages sent from either are flushed the same way.
  - In QuickJs, assign them to the `rack` object —
    `rack.onLoad = function() {...}` / `rack.onUnload = function() {...}`.
    Lua likewise: `rack.onLoad = function() ... end` /
    `rack.onUnload = function() ... end`.

## QuickJS language support

QuickJs is a full JavaScript engine (ES2020), so all standard JavaScript
syntax is available: `function` declarations, `while`/`do`/`for` loops,
`switch`, `try`/`catch`/`throw`, `class`, `new`, `this`, `var`/`let`/`const`,
arrow functions, destructuring, template literals, and the standard library
(`Math`, `JSON`, `String`, `Array`, ...). There are no scriptlet subset
restrictions to work around.

The only constraints come from the module, not the language:

- A **1 MiB memory limit** on the QuickJS heap (see
  `MidiScriptEngineQuickJs.h`).
- The script runs in a **sandboxed API**: only the globals documented here
  (`rack`, `number`, `input`, `trig`, `param`, `midi`, `midiOut`) are
  available. There is no `require`/`import`, no `console`, and no file or
  network access.

Booleans are real JavaScript booleans: `if (flag)`, `flag = !flag`,
`cond ? a : b`, and `flag === true` all work as in any JS engine.

Note also that a script whose *only* header tag is `@engine` fails to load —
the tag parser captures a trailing space and the value no longer matches
exactly. Always include at least one more tag (`@description` is conventional);
every example in this document does.

## API surface

### `rack.*`
- `rack.log(value [, value ...])` — write a line to the module's log/console.
  Any number of arguments are concatenated (no separator) into one line, each
  coerced the same way as a single value: strings are logged verbatim (no
  added quotes), numbers use the same format as `number.toString()` (so
  `rack.log(1 / 3)` prints `0.333333`), booleans log as `true`/`false`, and
  `null`/`undefined` (QuickJs) / `nil` (Lua) log as `null`/`undefined`. Other
  values (objects, arrays, tables, functions) use each engine's own
  stringification — scalars are guaranteed to format identically in both
  engines.
- `rack.overlay(s1 [, s2 [, s3]])` — show up to 3 lines in the on-panel overlay.
- `rack.getFrame()` — the current engine frame number (`APP->engine->getFrame()`).
- `rack.random()` — a random number in the interval [0, 1), drawn from Rack's own
  RNG (`rack::random::uniform()`), so it shares the patch's seed/determinism.
- `rack.registerContextMenu(options)` — add one item to the module's right-click
  context menu, in registration order (multiple items are allowed). Returns
  `true` on success; throws (load fails) if `options` is malformed. Two variants:

  *Boolean toggle* — a single menu line with a checkmark:
  ```js
  rack.registerContextMenu({
      type: "boolean",
      label: "Velocity to CC",
      onGetValue: function() {
          // Return true/false: the checkmark is read lazily, when the
          // menu is opened, so it always reflects the current state
          // (e.g. a config restored by onLoad()).
          return config.emitTrigger;
      },
      onChange: function(checked) {
          // checked: true/false (boolean)
      }
  });
  ```
  *Options submenu* — a submenu with one entry per option, checkmark on the
  current selection:
  ```js
  rack.registerContextMenu({
      type: "options",
      label: "Out mode",
      options: ["Internal", "External", "Both"],
      onGetValue: function() {
          // Return the selected index, read lazily when the menu is
          // opened. Return the index, or -1 for no selection.
          return config.outMode;
      },
      onChange: function(selectedIndex, selectedLabel) {
          // selectedIndex: number, selectedLabel: string
      }
  });
  ```
  Notes:
  - `label` must be a non-empty string; `options` must be a non-empty array of
    strings; `onChange` must be a function.
  - `onGetValue` is optional and, when present, must be a function returning
    the item's current value: a boolean for `type: "boolean"`, an index
    number for `type: "options"`. It is evaluated just-in-time on the worker
    thread every time the context menu is opened, so the checkmark/selection
    always reflects the script's live state — including config restored by
    `onLoad()` on a patch reload. When `onGetValue` is absent the value
    defaults to `false` / `0`.
  - `onChange` runs on the worker thread (like the other callbacks) when the
    menu item is clicked, and may call any other `rack.*` function. Exceptions
    inside it are logged as `Context menu callback error: ...` without
    crashing.
  - The module's presentation state (checkmark/selection) is updated as soon as
    the item is clicked, so the menu reflects the change immediately even
    before the callback has run.
  - All registered items are cleared when the script is reloaded or cleared.
  - Lua uses an equivalent table: `{ type = "boolean", label = "...",
    onGetValue = function() return config.emitTrigger end,
    onChange = function(checked) ... end }`.

### `number.*`
`rescale(x, xMin, xMax, yMin, yMax [, curve])`,
`crossfade(a, b, pos)`, `toString(x)`. Present in both engines identically (Lua re-exposes
these even though `math.*` is also available, for script portability).

### `input.*` (CV inputs on the module, 1-based)
- `input.enable(i)` — activate input `i` so it appears on the panel.
- `input.getVoltage(i [, ch])`, `input.isHigh(i [, ch])`, `input.isLow(i [, ch])`
  (channel defaults to 1; high/low threshold is 0.7V).
- Override `input.getName(i)` to customize the panel label.

### `trig.*` (dedicated trigger/gate ports)
- `trig.getTicks(i)` — clock tick counter for trigger input `i`.
- `trig.isHigh(i [, ch])`, `trig.isLow(i [, ch])`.
- `trig.setHigh(i [, ch])`, `trig.setLow(i [, ch])`, `trig.setTrigger(i [, ch])`
  (momentary trigger), `trig.setGate(i [, ch], durationMs)`.

### `param.*` (panel knobs)
- `param.enable(i)` — activate param `i`.
- `param.getValue(i)` — normalized 0..1 value.
- Override `param.getName(i)` and `param.getValueFormat(i)` for panel display.

### `midi.*` — message construction/inspection
Messages are opaque handles (indices into an internal store, max 32 live per
callback) created with `midi.create()` or `midi.createNRPN()`; `rack.onMidiMessage`
also receives the incoming message as handle `0`/implicit first arg (Lua:
index `0`, QuickJs: same convention).

- `midi.create()` → new empty message handle.
- `midi.clone(msg)` → new message handle carrying an independent copy of
  `msg`'s MIDI payload. The clone starts as a fresh, unsent message (its own
  store slot), so it can be modified and sent without affecting the source.
  This is the canonical way to "send a modified copy of the incoming message",
  e.g. `let copy = midi.clone(msg); midi.setChannel(copy, 5); midiOut.send(copy);`
  Note: NRPN state is not copied — a clone of an NRPN handle is a single plain
  message, not a chained quad.
- `midi.createNRPN()` → 4 chained handles (param LSB/MSB + value LSB/MSB),
  used only with `midi.setNRPN`.
- Getters: `getChannel(msg)` (1-based; `-1` for realtime/SysEx messages —
  clock, start/stop/continue, SysEx framing — which have no channel),
  `getChanPressure(msg)`, `getNote(msg)`,
  `getValue(msg)`, `getLength(msg)`, `getPitchWheel(msg)`, `getProgramChange(msg)`,
  `getSysEx(msg)` (hex string, the payload only — without the `f0`/`f7`
  framing), `getSysExLength(msg)` (payload length in bytes, framing excluded —
  check it before reading the payload with `getSysEx`), `getRaw(msg)`
  (hex string of the message's raw bytes, exactly as sent/received — no
  framing added or removed).
- Type predicates: `isCc`, `isNoteOn`, `isNoteOff`, `isKeyPressure`,
  `isChanPressure`, `isProgramChange`, `isPitchWheel`, `isSysEx`, `isClock`,
  `isStart`, `isContinue`, `isStop`.
- Setters (every `ch` argument below is a MIDI channel and is silently
  clamped to 1-16, e.g. `setNoteOn(msg, -5, ...)` is treated as channel 1):
  `setCc(msg, ch, cc, value)` (`value` is clamped to 0-127),
  `setCc14bit(msgMsb, msgLsb, ch, cc, value)`
  (value is a float, MSB=int part/LSB=fractional*128 — see `nrpn_to_cc.js`/`.lua`
  for the canonical use), `setChannel(msg, ch)`,
  `setChanPressure(msg, ch, value)` (2-byte message; read back with
  `getChanPressure`, not `getValue`),
  `setKeyPressure(msg, ch, note, vel)` (`vel` is clamped to 0-127), `setNote(msg, note)`,
  `setNoteOn(msg, ch, note, vel)` (`vel` is clamped to 0-127),
  `setNoteOff(msg, ch, note [, vel])` (release velocity defaults to 0, clamped to
  0-127; read back with `getValue`), `setNRPN(nrpnHandle, ch, number, value)`
  (number/value are 14-bit, 0-16383),
  `setPitchWheel(msg, ch, value)`, `setProgramChange(msg, ch, program)`,
  `setSysEx(msg, hexString)` (payload only — the `f0`/`f7` framing is added
  automatically, so pass e.g. `"43104c0000"` rather than `"f043104c0000f7"`;
  payload is capped at 256 bytes and every byte must be 7-bit, `00`-`7f`),
  `setRaw(msg, hexString)` (writes the exact bytes with no framing added,
  e.g. `"f11a"` for an MTC quarter-frame — use this for message types with no
  dedicated setter), `setValue(msg, value)`.

### `midiOut.*` — sending

- `midiOut.selectPort(midiPort)` — selects the output port (1-based) that every
  subsequent `midiOut.*` call sends on, until `selectPort` is called again.
  The selection is sticky across `rack.onMidiMessage` invocations, not reset per
  callback. MIDI-KIT currently exposes a single output, so
  `midiOut.selectPort(1)` is a no-op today beyond validating the index — it
  exists so scripts written against a future multi-output engine don't need to
  change their sending code.

The sending functions below take no port argument — the destination is
whatever `midiOut.selectPort()` last selected (port 1 if it was never called):

- `midiOut.send(msg)` — send immediately.
- `midiOut.send(nrpnHandle)` — sending the first handle of an NRPN quad
  automatically flushes all 4 underlying CC messages in order.
- `midiOut.sendAfterMs(msg, ms)` — delayed send, scheduled from the current
  engine frame.
- `midiOut.sendAfterTrigger(msg [, trigPort], ticks)` — send after `ticks`
  clock ticks counted from `trigPort` (1-based, defaults to trig input 1).

**A message can only be sent once per callback.** `midiOut.send(msg)` (and the
`sendAfter*` variants) mark the handle as sent; the actual enqueue happens once
per handle in the post-callback flush, so a second `send` of the *same* handle
within one `rack.onMidiMessage`/`rack.onLoad`/`rack.onUnload` is not a second message — only
one goes out, and if the message body was changed in between, the last change
wins. To send the same bytes twice, build a fresh handle first with
`midi.create()` or `midi.clone(msg)` and send that. Each message sent consumes
one store slot against the 32-handle cap, so one handle per message is the
correct idiom.

## MIDI status/type reference used internally
CC=0xb, NoteOn=0x9, NoteOff=0x8, KeyPressure=0xa, ChanPressure=0xd,
ProgramChange=0xc, PitchWheel=0xe, SysEx=0xf0/0xf7-wrapped. Realtime:
Clock=0xF8, Start=0xFA, Continue=0xFB, Stop=0xFC (encoded as status 0xf with
"channel" nibble 0x8/0xa/0xb/0xc respectively — use the `is*` predicates
rather than decoding this by hand).

## Gotchas
- Message handles are only valid within the `rack.onMidiMessage` call that
  created them — the store resets each callback invocation. Creating a
  message at top level (outside `rack.onMidiMessage`) logs a warning and the
  handle is discarded as soon as the next MIDI message arrives, so build
  messages inside the callback. `rack.onLoad()`/`rack.onUnload()`/`rack.onTrigger()`
  are full callbacks in this sense too — a message created and sent inside
  any of them is delivered normally, and (unlike bare top-level code)
  doesn't warn.
- `midi.setCc14bit`/`setNRPN` split a 14-bit value across two 7-bit CC
  messages (`cc` = MSB, `cc + 32` = LSB per the NRPN/14-bit CC convention);
  see [nrpn_to_cc.js](nrpn_to_cc.js)/[nrpn_to_cc.lua](nrpn_to_cc.lua) for a
  full worked example, and [nrpn_generator.js](nrpn_generator.js)/
  [nrpn_generator.lua](nrpn_generator.lua) for constructing NRPN messages.
- Lua's sandboxed stdlib excludes `io`, `os`, `package`, `debug` — no file
  access, no OS calls, by design.
- Both engines only see `@engine`-matching scripts; loading a QuickJs script
  into what expects `@engine Lua` (or vice versa) fails with an explicit
  "not compatible" log message rather than silently misinterpreting it.
