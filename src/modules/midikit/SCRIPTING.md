# MIDI-KIT scripting reference

MIDI-KIT scripts run in one of two embedded engines, chosen by the `@engine`
header tag: **Elk** (a JavaScript subset) or **Lua** (a sandboxed Lua 5.4 via
minilua). Both engines expose the *same* API (`midi`, `midiOut`, `input`,
`trig`, `param`, `number`, `log`, `overlay`) with 1-based indices for ports,
channels, and params. Pick the engine per script; the module identifies it
from the header, not the file extension.

Implementation: [MidiScriptEngineElk.h](MidiScriptEngineElk.h) (uses
[elk.h](elk.h)/[elk.c](elk.c)) and [MidiScriptEngineLua.h](MidiScriptEngineLua.h)
(uses [minilua.h](minilua.h)/[minilua.c](minilua.c)). Shared interface:
[MidiScriptEngine.h](MidiScriptEngine.h).

## When to write Elk (JavaScript) vs Lua

Both engines are similarly capable for the common case (reacting to
`processMidi`, building/sending messages). Pick based on these differences:

| | Elk (JS) | Lua |
|---|---|---|
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
syntax or is adapted from existing JS example scripts.

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
- `processMidi(midiPort, msg)` is called for every incoming MIDI message
  (`midiPort` is 1-based). Define it as a global function — it's the only
  callback the engine looks for per message.
- Optional `input.getName(i)`, `param.getName(i)`, `param.getValueFormat(i)`
  functions may be overridden to customize panel/input labeling; both
  engines seed defaults (`"Port " .. i` / `"Param " .. i`) that scripts can
  replace by reassigning the table field.
- There is no per-sample or per-frame callback — logic only runs in
  response to incoming MIDI messages (including clock 0xF8 realtime bytes).

## API surface

### Global functions
- `log(string)` — write a line to the module's log/console.
- `overlay(s1 [, s2 [, s3]])` — show up to 3 lines in the on-panel overlay.

### `number.*`
`abs`, `ceil`, `floor`, `max(a,b)`, `min(a,b)`, `random()` (0..1 uniform),
`rescale(x, xMin, xMax, yMin, yMax [, curve])`, `crossfade(a, b, pos)`,
`toString(x)`. Present in both engines identically (Lua re-exposes these
even though `math.*` is also available, for script portability).

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
callback) created with `midi.create()` or `midi.createNRPN()`; `processMidi`
also receives the incoming message as handle `0`/implicit first arg (Lua:
index `0`, Elk: same convention).

- `midi.create()` → new empty message handle.
- `midi.createNRPN()` → 4 chained handles (param LSB/MSB + value LSB/MSB),
  used only with `midi.setNRPN`.
- Getters: `getChannel(msg)` (1-based), `getNote(msg)`, `getValue(msg)`,
  `getLength(msg)`, `getPitchWheel(msg)`, `getSysExData(msg)` (hex string).
- Type predicates: `isCc`, `isNoteOn`, `isNoteOff`, `isKeyPressure`,
  `isChanPressure`, `isProgramChange`, `isPitchWheel`, `isSysEx`, `isClock`,
  `isStart`, `isContinue`, `isStop`.
- Setters: `setCc(msg, ch, cc, value)`, `setCc14bit(msgMsb, msgLsb, ch, cc, value)`
  (value is a float, MSB=int part/LSB=fractional*128 — see `nrpn_to_cc.js`/`.lua`
  for the canonical use), `setChannel(msg, ch)`, `setChanPressure(msg, ch, value)`,
  `setKeyPressure(msg, ch, note, vel)`, `setNote(msg, note)`,
  `setNoteOn(msg, ch, note, vel)`, `setNoteOff(msg, ch, note)`,
  `setNRPN(nrpnHandle, ch, number, value)` (number/value are 14-bit, 0-16383),
  `setPitchWheel(msg, ch, value)`, `setProgramChange(msg, ch, program)`,
  `setSysEx(msg, hexString)`, `setValue(msg, value)`.

### `midiOut.*` — sending
All variants accept an optional leading `midiPort` (1-based; omit for the
default/first output):
- `midiOut.send([midiPort,] msg)` — send immediately.
- `midiOut.send([midiPort,] nrpnHandle)` — sending the first handle of an
  NRPN quad automatically flushes all 4 underlying CC messages in order.
- `midiOut.sendAfterMs([midiPort,] msg, ms)` — delayed send, scheduled from
  the current engine frame.
- `midiOut.sendAfterTrigger([midiPort,] msg [, trigPort], ticks)` — send
  after `ticks` clock ticks counted from `trigPort` (defaults to trig
  input 1).

## MIDI status/type reference used internally
CC=0xb, NoteOn=0x9, NoteOff=0x8, KeyPressure=0xa, ChanPressure=0xd,
ProgramChange=0xc, PitchWheel=0xe, SysEx=0xf0/0xf7-wrapped. Realtime:
Clock=0xF8, Start=0xFA, Continue=0xFB, Stop=0xFC (encoded as status 0xf with
"channel" nibble 0x8/0xa/0xb/0xc respectively — use the `is*` predicates
rather than decoding this by hand).

## Gotchas
- Message handles are only valid within the `processMidi` call that created
  them — the store resets each callback invocation.
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
