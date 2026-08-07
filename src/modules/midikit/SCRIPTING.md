# MIDI-KIT scripting reference

MIDI-KIT scripts run in one of two embedded engines, chosen by a versioned
`@engine` header tag: `QuickJs@v1` (a full JavaScript engine) or `minilua@v1`
(a sandboxed Lua 5.4 via minilua). Both engines expose the *same* API (`midi`, `midiOut`, `input`,
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
 * @engine QuickJs@v1
 * @author yourname
 * @description One-line summary shown in the module log on load
 */
```

Lua (Lua-style `--[[ ... --]]`):
```lua
--[[
@target stoermelder MIDI-KIT
@engine minilua@v1
@author yourname
@description One-line summary shown in the module log on load
--]]
```

`@engine` is mandatory and must exactly match `QuickJs@v1` or `minilua@v1` for
the respective loader, or the script is rejected. The `@v1` suffix pins the
script to a specific engine protocol revision, so a future breaking engine
change can bump it (e.g. `@v2`) and old scripts are cleanly rejected instead
of misbehaving. `@author`/`@description` are optional but get echoed to the
module's log on load. `@target` is conventionally present but not checked by
the loader.

Engine selection is a simple substring match: the module asks each engine's
`testScript()` whether the script is for it (QuickJs matches `@engine QuickJs@v1`,
Lua matches `@engine minilua@v1`) and routes to the engine that says yes — the file
extension is never used. Because the match is a plain `@engine <name>@vN`
substring search, a script that mentions the tag only in a comment or a string
(after the header block) can be misrouted; keep the `@engine` tag in the
leading comment block.

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
- Optional `rack.onTrigger(trigPort, channel)` is called whenever the module's
  trigger/gate input (`trigPort`, 1-based — MIDI-KIT currently exposes a
  single trigger input, so this is always `1`) crosses the trigger
  threshold. `channel` (1-based) is the polyphonic channel of the trigger
  input that fired — a polyphonic clock/gate fires the hook once per rising
  edge on each channel. It is the only way to run script logic that isn't
  driven by an incoming MIDI message — e.g. reading
  `trig.getTicks()`/`input.*` and sending a MIDI message in response to an
  external clock/gate, or streaming a
  [Tipsy](#trig-dedicated-triggergate-ports) message with `trig.sendTipsy()`
  on the trigger output. A script
  that never defines it simply never has it called; unlike
  `rack.onMidiMessage`, there is no load-time log warning for omitting it.
  In QuickJs, assign it to the `rack` object —
  `rack.onTrigger = function(trigPort, channel) {...}`. Lua likewise:
  `rack.onTrigger = function(trigPort, channel) end` or
  `function rack.onTrigger(trigPort, channel) end`.
- Optional `rack.onTipsyMessage(data, mimeType)` is called once for every
  complete [Tipsy](#trig-dedicated-triggergate-ports) message decoded from
  the trigger input claimed with `trig.enableTipsyIn()`. `data` and `mimeType` are
  both strings; `data` may contain arbitrary bytes, including NULs, and is
  capped at 256 bytes. A script that never claims a port, or never defines
  the hook, simply never has it called — there is no load-time warning.
  In QuickJs, assign it to the `rack` object —
  `rack.onTipsyMessage = function(data, mimeType) {...}`. Lua likewise:
  `rack.onTipsyMessage = function(data, mimeType) end`.
- There is no per-sample or per-frame callback — logic only runs in
  response to incoming MIDI messages (including clock 0xF8 realtime bytes),
  trigger-input ticks via `rack.onTrigger`, or decoded Tipsy messages via
  `rack.onTipsyMessage`.
- Optional `rack.onLoad()`, `rack.onUnload()`, and `rack.onSave()` hooks:
  - `rack.onLoad([persistedConfig])` runs once, right after top-level code,
    when the script has parsed and loaded successfully. If a config was
    persisted by a previous save (see [Persistence](#persistence)), it is
    passed as `persistedConfig` (a JavaScript object in QuickJs, a Lua table
    in Lua); otherwise the argument is `undefined` (QuickJs) / `nil` (Lua).
  - `rack.onUnload()` runs every time the *current* script's state is torn
    down for real — because it's about to be replaced by another script, the
    module was reset, or the module is being removed from the patch. This is
    the only place a script can reliably clean up: sending an all-notes-off
    for anything it left sounding is the main use case, since nothing else
    will ever get a chance to release those notes once the script's own
    state is gone. **Its return value is ignored** — `onUnload()` is
    teardown-only; use `rack.onSave()` to persist config.
  - `rack.onSave()` is the config-bearing hook: if it returns a value, that
    value is serialized to JSON and stored with the module's patch data (see
    [Persistence](#persistence)). It **must be side-effect-free** — it may be
    called repeatedly (e.g. on every explicit patch save) without tearing
    down or otherwise disturbing the script's state, so it should only read
    state, never mutate it or send MIDI messages meant to have an audible
    effect (any it does send are discarded, see
    [Persistence](#persistence)).
  - All three can call `midi.create()`/`midiOut.send()` like
    `rack.onMidiMessage` can; messages sent from any of them are flushed the
    same way, except `onSave()`'s are always discarded (see above).
  - In QuickJs, assign them to the `rack` object —
    `rack.onLoad = function(...) {...}` / `rack.onUnload = function() {...}`
    / `rack.onSave = function() {...}`. Lua likewise:
    `rack.onLoad = function(...) ... end` / `rack.onUnload = function() ... end`
    / `rack.onSave = function() ... end`.

### Hooks and predefined objects are resolved once, at load time

`rack.onMidiMessage`, `rack.onTrigger`, `rack.onTipsyMessage`, `rack.onLoad`,
`rack.onUnload`, and
`rack.onSave` are read from the `rack` object **exactly once**, right after the script's
top-level code finishes running. **Reassigning any of them afterward — from
inside a callback or anywhere else in the script — has no effect.** The
function that was present at load time keeps running for the lifetime of the
script; a later assignment to `rack.onMidiMessage` (or the others) is simply
never looked at again. The same applies to defining a hook **late**: a
script that never defines `rack.onMidiMessage` at load time but assigns it
later (e.g. from inside `rack.onTrigger`) will never have that later
definition called — the "no `onMidiMessage` defined" warning logged at load
time is the last word on it.

This is a deliberate, permanent design choice, not a limitation to be worked
around: resolving the hooks once (rather than looking them up by name on
every incoming MIDI message or trigger tick) is what keeps message dispatch
fast. Write scripts so every hook is assigned exactly once, at the top
level, during initial load — this is what every shipped preset already does
and the only supported pattern.

Reassigning (or otherwise clobbering — e.g. `rack = 42`) one of the
predefined globals (`rack`, `midi`, `midiOut`, `trig`, `input`, `param`,
`number`) at runtime is likewise unsupported. Both engines are tested to
degrade gracefully rather than crash if a script does this anyway — expect
either the reassignment to be silently ignored (for a hook function, per
above) or a caught, logged script error on the next statement that tries to
use the clobbered value (for everything else) — but the resulting behavior
is undefined and may differ in wording between QuickJs and Lua. Treat any of
this as a bug in the script, not a supported technique.

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

### Persistence

Scripts can persist their configuration across patch saves and reloads via a
pair of hooks:

- **`rack.onSave()`** — called to snapshot the script's current config for
  persistence. If it returns a value, that value is serialized to JSON and
  stored with the module's patch data. It **must be side-effect-free**: it
  may be called repeatedly (e.g. on every explicit save), so it should only
  read state, never mutate it.
- **`rack.onLoad(persistedConfig)`** — called when the script loads. If a
  config was persisted, it is deserialized from JSON and passed as
  `persistedConfig` (a JavaScript object in QuickJs, a Lua table in Lua). If
  no config was persisted, `persistedConfig` is `undefined` (QuickJs) / `nil`
  (Lua) and the script initializes from its defaults.

`rack.onUnload()` is a separate, teardown-only hook (see above) — **its
return value is ignored** and it is not part of the persistence contract.
Scripts written before `rack.onSave()` existed that only returned their
config from `onUnload()` will persist nothing until migrated to
`rack.onSave()`; there is no automatic fallback.

The persisted value must be a plain JSON-serializable object (booleans,
numbers, strings, arrays, and nested objects); functions and other
non-serializable values are silently dropped. If `onSave()` returns nothing
(or a non-serializable value), no config is persisted and `onLoad()` receives
`undefined`/`nil`.

A typical pattern:

```js
let config = { channel: 1, passThrough: false };

rack.onLoad = function(persistedConfig) {
    if (persistedConfig) {
        config = Object.assign({}, config, persistedConfig);
    }
};

rack.onSave = function() {
    return config;
};
```

```lua
config = { channel = 1, passThrough = false }

rack.onLoad = function(persistedConfig)
    if persistedConfig then
        for k, v in pairs(persistedConfig) do
            config[k] = v
        end
    end
end

rack.onSave = function()
    return config
end
```

Timing notes:

- On a patch save, the module snapshots the config by running `onSave()`
  synchronously on the GUI thread, without touching `onUnload()` or tearing
  the script down — the script keeps running and the return value reflects
  live state (e.g. the latest context-menu setting). Because `onSave()` must
  be side-effect-free, it may be called on every save (including the
  periodic autosave) with no audible effect; any messages it sends anyway are
  **discarded**.
- When the script is actually replaced, the module reset, or the module
  removed, `rack.onUnload()` runs as a real teardown and its messages (e.g.
  an all-notes-off) **are** delivered — but its return value, even if
  present, is never persisted. `onUnload()` can run more than once over a
  script's lifetime (e.g. on module reset followed by removal), so keep any
  side effects idempotent rather than assuming it runs exactly once.
- The persisted config is only written when Rack serializes the module
  (`dataToJson()`, i.e. on patch save); replacing/removing the module or
  script does not by itself save it.

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
- `trig.getTicks(i [, ch])` — clock tick counter for trigger input `i`
  (polyphonic channel defaults to 1).
- `trig.isHigh(i [, ch])`, `trig.isLow(i [, ch])`.
- `trig.setHigh(i [, ch])`, `trig.setLow(i [, ch])`, `trig.setTrigger(i [, ch])`
  (momentary trigger), `trig.setGate(i [, ch], durationMs)`.
- `trig.sendTipsy(data [, mimeType])` — encode `data` (a string) with the
  [Tipsy protocol](https://github.com/baconpaul/tipsy-encoder) and stream it
  out the trigger output as CV voltages, one voltage per sample until the
  message is complete. The optional `mimeType` (a string) specifies the
  content type and defaults to `"text/plain"`; the payload is capped at
  256 bytes. The stream is meant for modules that understand the Tipsy
  protocol (such as [TRANSIT](../../transit/Transit.md)) and temporarily
  takes over the trigger output while it is being transmitted. Unlike the
  `midiOut.*` senders, `sendTipsy` sends no MIDI: it is not routed through
  `midiOut.selectPort()`, does not consume a message-handle slot, and is not
  subject to the "sent once per callback" rule.
  ```js
  rack.onTrigger = function(trigPort, channel) {
      trig.sendTipsy("Hello Tipsy!");                              // mime defaults to "text/plain"
      trig.sendTipsy('{"label":"My snapshot","value":42}', "application/json");
  };
  ```
- `trig.enableTipsyIn([enabled])` — decode an incoming Tipsy stream from the
  trigger input, delivering each completed message to `rack.onTipsyMessage`.
  The optional boolean `enabled` defaults to `true`; pass `false` to release
  the trigger input again. Tipsy input is only supported on the first trigger
  input, so — like `trig.sendTipsy()` — there is no port argument.

  While the trigger input is claimed, it stops behaving as a trigger on
  channel 1: `rack.onTrigger` doesn't fire and `trig.getTicks()` doesn't
  advance there, and `trig.isHigh()`/`trig.isLow()` on channel 1 read `0` —
  the encoded voltages are protocol, not a gate a script should act on.
  Other channels are unaffected. A Tipsy stream would otherwise fire
  `rack.onTrigger` continuously as the encoded voltages cross the trigger
  threshold.
  ```js
  rack.onLoad = function() {
      trig.enableTipsyIn();        // decode from the trigger input
  };
  ```

### `param.*` (panel knobs)
- `param.enable(i)` — activate param `i`.
- `param.getValue(i)` — normalized 0..1 value.
- Override `param.getName(i)` and `param.getValueFormat(i)` for panel display.

### `midi.*` — message construction/inspection
Messages are opaque handles (indices into an internal store, max 32 live per
callback) created with `midi.create()` or `midi.createNRPN()`; `rack.onMidiMessage`
also receives the incoming message as handle `0`/implicit first arg (Lua:
index `0`, QuickJs: same convention).

**The store holds at most 32 live handles per callback.** Once it is full,
`midi.create()`, `midi.clone()`, and `midi.createNRPN()` raise a script error
that aborts the rest of the callback. Messages already marked for send before
the error are still flushed, so a multi-message sequence (e.g. an NRPN pair,
or a wide chord release) can be emitted partially — a message created but
never sent is dropped. Keep callbacks within the cap, or create/send in
batches (one handle per message, per the send-once rule below).

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
- `midiOut.sendAfterTrigger(msg, ticks [, trigPort [, channel]])` — send
  after `ticks` clock ticks counted from `trigPort` (1-based, defaults to
  trig input 1) on polyphonic `channel` (defaults to 1).

**A message can only be sent once per callback.** `midiOut.send(msg)` (and the
`sendAfter*` variants) mark the handle as sent; the actual enqueue happens once
per handle in the post-callback flush, so a second `send` of the *same* handle
within one `rack.onMidiMessage`/`rack.onLoad`/`rack.onUnload`/`rack.onSave` is not a second message — only
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
  messages inside the callback. `rack.onLoad()`/`rack.onUnload()`/`rack.onSave()`/`rack.onTrigger()`
  are full callbacks in this sense too — a message created and sent inside
  any of them is delivered normally (except `onSave()`'s, which are always
  discarded — see [Persistence](#persistence)), and (unlike bare top-level
  code) doesn't warn.
- `midi.setCc14bit`/`setNRPN` split a 14-bit value across two 7-bit CC
  messages (`cc` = MSB, `cc + 32` = LSB per the NRPN/14-bit CC convention);
  see [nrpn_to_cc.js](nrpn_to_cc.js)/[nrpn_to_cc.lua](nrpn_to_cc.lua) for a
  full worked example, and [nrpn_generator.js](nrpn_generator.js)/
  [nrpn_generator.lua](nrpn_generator.lua) for constructing NRPN messages.
- Lua's sandboxed stdlib excludes `io`, `os`, `package`, `debug` — no file
  access, no OS calls, by design.
- Both engines only see `@engine`-matching scripts; loading a QuickJs script
  into what expects `@engine minilua@v1` (or vice versa) fails with an explicit
  "not compatible" log message rather than silently misinterpreting it.
