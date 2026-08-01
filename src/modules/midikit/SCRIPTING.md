# MIDI-KIT scripting reference

MIDI-KIT scripts run in one of two embedded engines, chosen by the `@engine`
header tag: **Elk** (a JavaScript subset) or **Lua** (a sandboxed Lua 5.4 via
minilua). Both engines expose the *same* API (`midi`, `midiOut`, `input`,
`trig`, `param`, `number`, `rack`) with 1-based indices for ports,
channels, and params. Pick the engine per script; the module identifies it
from the header, not the file extension.

Implementation: [MidiScriptEngineElk.h](MidiScriptEngineElk.h) (uses
[elk.h](elk.h)/[elk.c](elk.c)) and [MidiScriptEngineLua.h](MidiScriptEngineLua.h)
(uses [minilua.h](minilua.h)/[minilua.c](minilua.c)). Shared interface:
[MidiScriptEngine.h](MidiScriptEngine.h).

## When to write Elk (JavaScript) vs Lua

Both engines are similarly capable for the common case (reacting to
`onMidiMessage`, building/sending messages). Pick based on these differences:

| | Elk (JS) | Lua |
|---|---|---|
| Language completeness | A JS **subset**: no `while`, `var`/`const`, `switch`, `try`, `class`, `new`, `this`, and functions must be expressions (`let f = function(){}`) — see [Elk language limitations](#elk-language-limitations) | Full Lua 5.4 syntax; only the *library* is trimmed |
| Data structures | Array literals `[1,2,3]`, object literals `{a:1}` (see [elk_array.md](elk_array.md)) | Only tables (`{}`); no literal array sugar, must use `{ {...}, {...} }` and `#t`/`ipairs` |
| Stdlib | Minimal subset of JS, no regex/JSON/closures over complex types guaranteed | Real Lua stdlib subset: `math`, `string`, `table` (no `io`, `os`, `package`, `debug` — sandboxed) |
| String formatting | `number.toString(x)` helper needed for numeric concatenation | Lua auto-coerces numbers in `..` concatenation; `string.format` available |
| Familiarity | Preferred if the user/preset is JS-oriented or ports logic from another Elk script | Preferred if the script needs `string.format`, `table.sort`, pattern matching, or other real stdlib features |
| Performance/footprint | `elk.c` is a tiny embeddable interpreter, fixed memory arena | minilua is a stripped full Lua VM; similarly small footprint |

Default guidance: **match whatever engine sibling/companion scripts in the
same preset pair already use** (e.g. a generator + converter pair should
both be Lua or both be Elk, so users can read them side by side). If there's
no existing convention, prefer Lua for anything doing string formatting or
table sorting, and Elk for anything that benefits from array/object literal
syntax or is adapted from existing JS example scripts. For anything with
non-trivial control flow, Lua is the safer default — Elk's missing `while`,
`switch` and `try` tend to surface only once the script is already written.

## Required file header

The engine is selected by parsing `@key value` tags out of the leading
comment block only — nothing after the block is scanned for tags.

Elk (JS-style `/** ... */`):
```javascript
/**
 * @target stoermelder MIDI-KIT
 * @engine Elk
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

`@engine` is mandatory and must exactly match `Elk` or `Lua` for the
respective loader, or the script is rejected. `@author`/`@description` are
optional but get echoed to the module's log on load. `@target` is
conventionally present but not checked by the loader.

## Script structure

- Top-level code runs once, synchronously, when the script is (re)loaded.
- `onMidiMessage(midiPort, msg)` is called for every incoming MIDI message
  (`midiPort` is 1-based). Define it as a global function — it's the only
  callback the engine looks for per message; a script that never defines it
  loads fine but silently ignores all incoming MIDI (logged once at load
  time). **In Elk it must be defined with plain assignment, NOT `let`:**
  `onMidiMessage = function(...) {...};` — same reason as `onLoad`/`onUnload`
  below: the name already exists as a pre-registered no-op before your
  script runs, and `let onMidiMessage = ...` collides with it and fails to
  parse (`'onMidiMessage' already declared`). The `function onMidiMessage(...)
  {}` declaration form is *also* a parse error in Elk, independent of that —
  see [Elk language limitations](#elk-language-limitations). Lua has neither
  restriction: `onMidiMessage = function(...) end` or
  `function onMidiMessage(...) end` both work.
- Optional `input.getName(i)`, `param.getName(i)`, `param.getValueFormat(i)`
  functions may be overridden to customize panel/input labeling; both
  engines seed defaults (`"Port " .. i` / `"Param " .. i`) that scripts can
  replace by reassigning the table field.
- There is no per-sample or per-frame callback — logic only runs in
  response to incoming MIDI messages (including clock 0xF8 realtime bytes).
- Optional `onLoad()` and `onUnload()` hooks run once each:
  - `onLoad()` runs once, right after top-level code, when the script has
    parsed and loaded successfully.
  - `onUnload()` runs once, right before the *current* script's state is
    torn down — because it's about to be replaced by another script, the
    module was reset, or the module is being removed from the patch. This
    is the only place a script can reliably clean up: sending an
    all-notes-off for anything it left sounding is the main use case, since
    nothing else will ever get a chance to release those notes once the
    script's own state is gone.
  - Both can call `midi.create()`/`midiOut.send()` like `onMidiMessage` can;
    messages sent from either are flushed the same way.
  - **Elk-specific:** define these with **plain assignment, not `let`** —
    `onLoad = function() { ... };` / `onUnload = function() { ... };`. Both
    names already exist as no-op globals before your script runs (so the
    engine always has something to call even if you don't define one), and
    Elk raises a parse error on `let onLoad = ...` re-declaring an existing
    global (`'onLoad' already declared`). Lua has no such restriction —
    `function onLoad() ... end`/`onLoad = function() ... end` both work.

## Elk language limitations

Elk is a small JavaScript *subset*, not a JS engine — a lot of ordinary
JavaScript does not parse. This trips up most first-time scripts, and the
error message is a bare `ERROR: parse error` with no line number, so the
list below is worth reading before writing Elk. (Lua scripts are unaffected;
minilua is a real Lua 5.4 VM with only the standard library trimmed.)

**Not supported — these fail to load:**

| Feature | Error | Use instead |
|---|---|---|
| `function f() {}` declaration | `parse error` | `let f = function() {};` |
| `while (...) {}` | `'while' not implemented` | `for (;cond;) {}` |
| `do {} while (...)` | `'do' not implemented` | `for (;;) { ...; if (!cond) break; }` |
| `var x` | `'var' not implemented` | `let x` |
| `const x` | `'const' not implemented` | `let x` |
| `switch` | `'switch' not implemented` | `if` / `else if` chain |
| `try` / `catch` / `throw` | `'try' not implemented` | — no exception handling |
| `class` | `'class' not implemented` | plain object literals |
| `new X()` | `bad expr` | object literals `{}` |
| `this` | `bad expr` | — no method receivers |
| `delete o.k` | `'delete' not found` | assign `null` |
| `x instanceof Y` | `parse error` | — |

**Supported:** `let`, `if` / `else if` / `else`, three-clause `for`
(`for (let i = 0; i < n; i++)`), `for (;;)` with `break` / `continue`,
`return`, function *expressions* (including nested and recursive via a `let`
binding), array literals and indexing with `.length` (see
[elk_array.md](elk_array.md)), object literals and property access, the
ternary `?:`, string concatenation with `+`, and `typeof`.

Booleans can be stored, passed and tested directly (`if (flag)`, `flag = !flag`,
`cond ? a : b`). Two booleans can also be compared with `===`/`!==` — they
compare as their `0`/`1` values, so `flag === true`, `flag !== false` and
`true === true` work. Mixed comparisons stay type errors, consistent with the
engine's rejection of other mixed operand types: `flag === 1` (boolean vs
number) and `flag === 'true'` (boolean vs string) both fail with
`type mismatch` — keep a numeric flag as `0`/`1` and test `flag === 1` for that
case, as in
[Scale quantiser.js](../../../presets/MidiKit/JavaScript/Scale%20quantiser.js).

The authoritative list is elk's own parser
([elk.c](elk.c), `js_stmt`) and its test suite
([elk_unit_test.c](elk_unit_test.c)); the table above was verified against the
engine as built here.

Because there is no `while`, the common "loop until done" shape becomes:

```javascript
/**
 * @engine Elk
 * @description Example of the for-loop idiom
 */
onMidiMessage = function(midiPort, msg) {
    for (let i = 0; i < 4; i++) {
        if (input.isLow(i + 1)) continue;
        rack.log("input ", i + 1, " is high");
    }
};
```

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
  `null`/`undefined` (Elk) / `nil` (Lua) log as `null`/`undefined`. Other
  values (objects, arrays, tables, functions) use each engine's own
  stringification — scalars are guaranteed to format identically in both
  engines.
- `rack.overlay(s1 [, s2 [, s3]])` — show up to 3 lines in the on-panel overlay.
- `rack.getFrame()` — the current engine frame number (`APP->engine->getFrame()`).

### `number.*`
`abs`, `ceil`, `floor`, `max(a,b)`, `min(a,b)`, `random()` (0..1 uniform),
`rescale(x, xMin, xMax, yMin, yMax [, curve])`, `crossfade(a, b, pos)`,
`toString(x)`, `toFixed(x, digits)` (fixed-precision string, `digits` 0-20).
Present in both engines identically (Lua re-exposes these even though
`math.*` is also available, for script portability).

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
callback) created with `midi.create()` or `midi.createNRPN()`; `onMidiMessage`
also receives the incoming message as handle `0`/implicit first arg (Lua:
index `0`, Elk: same convention).

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
  The selection is sticky across `onMidiMessage` invocations, not reset per
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

## MIDI status/type reference used internally
CC=0xb, NoteOn=0x9, NoteOff=0x8, KeyPressure=0xa, ChanPressure=0xd,
ProgramChange=0xc, PitchWheel=0xe, SysEx=0xf0/0xf7-wrapped. Realtime:
Clock=0xF8, Start=0xFA, Continue=0xFB, Stop=0xFC (encoded as status 0xf with
"channel" nibble 0x8/0xa/0xb/0xc respectively — use the `is*` predicates
rather than decoding this by hand).

## Gotchas
- Message handles are only valid within the `onMidiMessage` call that created
  them — the store resets each callback invocation. Creating a message at top
  level (outside `onMidiMessage`) logs a warning and the handle is discarded as
  soon as the next MIDI message arrives, so build messages inside the callback.
  `onLoad()`/`onUnload()` are full callbacks in this sense too — a message
  created and sent inside either is delivered normally, and (unlike bare
  top-level code) doesn't warn.
- `midi.setCc14bit`/`setNRPN` split a 14-bit value across two 7-bit CC
  messages (`cc` = MSB, `cc + 32` = LSB per the NRPN/14-bit CC convention);
  see [nrpn_to_cc.js](nrpn_to_cc.js)/[nrpn_to_cc.lua](nrpn_to_cc.lua) for a
  full worked example, and [nrpn_generator.js](nrpn_generator.js)/
  [nrpn_generator.lua](nrpn_generator.lua) for constructing NRPN messages.
- Lua's sandboxed stdlib excludes `io`, `os`, `package`, `debug` — no file
  access, no OS calls, by design.
- Both engines only see `@engine`-matching scripts; loading an Elk script
  into what expects `@engine Lua` (or vice versa) fails with an explicit
  "not compatible" log message rather than silently misinterpreting it.
