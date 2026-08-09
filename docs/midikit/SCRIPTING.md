# MIDI-KIT scripting reference

MIDI-KIT scripts run in one of two embedded engines, chosen by a versioned
`@engine` header tag: `QuickJs@v1` (a full JavaScript engine) or `minilua@v1`
(a sandboxed Lua 5.4 via minilua). Both engines expose the *same* API (`midi`, `midiOut`, `input`,
`trig`, `param`, `number`, `rack`) with 1-based indices for ports,
channels, and params. Pick the engine per script; the module identifies it
from the header, not the file extension.

**Contents:**
- [Part 1 — Writing a script](#part-1--writing-a-script): engine choice,
  file header, script structure and lifecycle
- [Part 2 — Examples](#part-2--examples): worked scripts from basic
  pass-through to context menus and assembled NRPN input
- [Part 3 — API reference](#part-3--api-reference): every `rack.*` /
  `input.*` / `trig.*` / `param.*` / `midi.*` / `midiOut.*` / `number.*`
  function, persistence, and the MIDI status/type reference
- [Part 4 — Gotchas](#part-4--gotchas)

## Part 1 — Writing a script

### When to write QuickJs (JavaScript) vs Lua

Both engines handle the common case equally well: reacting to
`midi.onMessage`, building/sending messages. Pick based on these differences:

| | QuickJs (JS) | Lua |
|---|---|---|
| Language completeness | Full JavaScript (ES2020): `while`, `switch`, `try`, `class`, `new`, `this`, `var`/`let`/`const`, function declarations, arrow functions | Full Lua 5.4 syntax; only the *library* is trimmed |
| Data structures | Array literals `[1,2,3]`, object literals `{a:1}` | Only tables (`{}`); no literal array sugar, must use `{ {...}, {...} }` and `#t`/`ipairs` |
| Stdlib | Full JS standard library: `Math`, `JSON`, `String`, `Array`, ... | Real Lua stdlib subset: `math`, `string`, `table` (no `io`, `os`, `package`, `debug` — sandboxed) |
| String formatting | JS auto-coerces numbers in `+` concatenation; `number.toString()` helper available | Lua auto-coerces numbers in `..` concatenation; `string.format` available |
| Familiarity | Preferred if the user/preset is JS-oriented or ports logic from another JS script | Preferred if the script needs `string.format`, `table.sort`, pattern matching, or other real stdlib features |
| Performance/footprint | QuickJS is a full embeddable JS engine with a 1 MiB memory limit | minilua is a stripped full Lua VM; similarly small footprint |

Default guidance: **match whatever sibling/companion scripts in the same
preset pair already use**, so users can read them side by side. Otherwise,
prefer Lua for string formatting or table sorting, and QuickJs for
array/object literal syntax or scripts adapted from existing JS examples.

### Required file header

The engine is selected by parsing `@key value` tags out of the leading
comment block only — nothing after the block is scanned.

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

`@engine` is mandatory and must exactly match `QuickJs@v1` or `minilua@v1`, or
the script is rejected. The `@v1` suffix pins the script to an engine protocol
revision, so a future breaking change can bump it (`@v2`) without silently
misbehaving old scripts. `@author`/`@description` are optional and get echoed
to the module's log on load. `@target` is conventional but not checked.

Engine selection is a plain substring search for `@engine <name>@vN` in the
header comment block — not the file extension, and not scanned past the
block. Keep the `@engine` tag inside the leading comment, or the script can be
misrouted.

### Script structure

A MIDI-KIT script is a single text file: top-level code that runs once when
the script (re)loads, plus optional callbacks that run in response to events.
There is no per-sample or per-frame callback — logic only runs when something
happens: a MIDI message arrives, a trigger fires, a Tipsy message decodes, or
the script loads/unloads/saves.

**Top-level code** runs once, synchronously, at (re)load. It's where you set
up `config`, define helpers, register context-menu items, and enable things
(`param.enable()`, `trig.enableIn()`, `midi.enableNrpnIn()`, …).

The callbacks a script can define, and when each runs:

| Callback | Runs … | Needs |
| --- | --- | --- |
| `midi.onMessage(midiPort, msg)` | on every incoming MIDI message | — |
| `trig.onTrigger(trigPort, channel)` | on every rising edge of an *enabled* trigger channel | `trig.enableIn()` |
| `trig.onTipsyMessage(data, mimeType)` | on every complete [Tipsy](#tipsy) message decoded from the trigger input | `trig.enableTipsyIn()` |
| `rack.onLoad(persistedConfig)` / `rack.onUnload()` / `rack.onSave()` | script [lifecycle](#persistence) — load, teardown, patch save | — |
| `input.getName(i)` / `param.getName(i)` / `param.getValueFormat(i)` | when a panel tooltip is shown | — |

All hooks are assigned as plain fields on their object (`midi.onMessage =
function(midiPort, msg) {...}` in JS, `function(...) end` in Lua) and must be
assigned exactly once, at the top level — see [Hooks and predefined objects
are resolved once, at load
time](#hooks-and-predefined-objects-are-resolved-once-at-load-time) for why.
A script that never defines `midi.onMessage` loads fine but silently ignores
all MIDI (logged once at load); the other hooks warn only for `midi.onMessage`
— omitting the rest is silent.

`trig.onTrigger` additionally needs `trig.enableIn(trigPort, [channel])` — the
trigger input is otherwise not processed at all on that channel: no ticks
counted, no `sendAfterTrigger` messages drained, no callback dispatched.

## Part 2 — Examples

**Note:** Channels are 1..16, parameter and input indices are 1..4. The main entry point is `midi.onMessage(midiPort, msg)`; in this version `midiPort` is always *1*.

Examples build up roughly from simplest to most involved: basic pass-through
and filtering first, then message construction (NRPN, 14-bit CC, SysEx, raw),
then lifecycle/trigger/Tipsy examples, and finally UI (context menu) and
assembled-input examples.

### Basic pass-through
The script passes all incoming MIDI messages to the default MIDI output port.

JavaScript:
```js
midi.onMessage = function(midiPort, msg) {
   midiOut.send(msg);
};
```

Lua:
```lua
midi.onMessage = function(midiPort, msg)
   midiOut.send(msg)
end
```

### Simple MIDI filter
The script drops all incoming MIDI messages except for MIDI channel 2. Messages without a channel field (like MIDI clock) will also be dropped.

JavaScript:
```js
midi.onMessage = function(midiPort, msg) {
   if (midi.getChannel(msg) === 2) {
      midiOut.send(msg);
   }
};
```

Lua:
```lua
midi.onMessage = function(midiPort, msg)
   if midi.getChannel(msg) == 2 then
      midiOut.send(msg)
   end
end
```

### MIDI channel routing for CC messages
The script routes incoming CC messages on MIDI channel 2 to MIDI channel 3. All other messages are passed-through unchanged.

JavaScript:
```js
midi.onMessage = function(midiPort, msg) {
   if (midi.isCc(msg) && midi.getChannel(msg) === 2) {
      midi.setChannel(msg, 3);
   }
   midiOut.send(msg);
};
```

Lua:
```lua
midi.onMessage = function(midiPort, msg)
   if midi.isCc(msg) and midi.getChannel(msg) == 2 then
      midi.setChannel(msg, 3)
   end
   midiOut.send(msg)
end
```

### Dynamic MIDI channel routing for CC messages by knob (1)
The script routes incoming CC messages on MIDI channel 2 to a MIDI channel set by parameter 1 on the panel. All other messages are passed-through unchanged.

JavaScript:
```js
param.enable(1);

midi.onMessage = function(midiPort, msg) {
   if (midi.isCc(msg) && midi.getChannel(msg) === 2) {
      let ch = Math.ceil(param.getValue(1) * 16);
      midi.setChannel(msg, ch);
   }
   midiOut.send(msg);
};
```

Lua:
```lua
param.enable(1)

midi.onMessage = function(midiPort, msg)
   if midi.isCc(msg) and midi.getChannel(msg) == 2 then
      local ch = math.ceil(param.getValue(1) * 16)
      midi.setChannel(msg, ch)
   end
   midiOut.send(msg)
end
```

### Dynamic MIDI channel routing for CC messages by knob (2)
The script handles MIDI messages like the previous example, but MIDI-KIT provides additional programming interface for user interface configuration: `param.getName` configures the text "MIDI Channel" for the tooltip of the first panel parameter, the display value is scaled to the integer range 1..16 by `param.getValueFormat`.

JavaScript:
```js
param.enable(1);

param.getName = function(port) {
    if (port === 1) return "MIDI Channel";
    return "";
};

param.getValueFormat = function(port) {
    if (port === 1) return number.toString(Math.ceil(param.getValue(1) * 16));
    return number.toString(param.getValue(port));
};

midi.onMessage = function(midiPort, msg) {
   if (midi.isCc(msg) && midi.getChannel(msg) === 2) {
      let ch = Math.ceil(param.getValue(1) * 16);
      midi.setChannel(msg, ch);
   }
   midiOut.send(msg);
};
```
![Dynamic MIDI channel routing for CC](./MidiKit-ex1.png)

Lua:
```lua
param.enable(1)

param.getName = function(port)
    if port == 1 then return "MIDI Channel" end
    return ""
end

param.getValueFormat = function(port)
    if port == 1 then return number.toString(math.ceil(param.getValue(1) * 16)) end
    return number.toString(param.getValue(port))
end

midi.onMessage = function(midiPort, msg)
   if midi.isCc(msg) and midi.getChannel(msg) == 2 then
      local ch = math.ceil(param.getValue(1) * 16)
      midi.setChannel(msg, ch)
   end
   midiOut.send(msg)
end
```

### Send NRPN message

JavaScript:
```js
midi.onMessage = function(midiPort, msg) {
   if (midi.isNoteOn(msg)) {
      let nrpn1 = midi.createNRPN();
      midi.setNRPN(nrpn1, 1, 12345, 13456);
      midiOut.send(nrpn1);
   }
};
```

Lua:
```lua
midi.onMessage = function(midiPort, msg)
   if midi.isNoteOn(msg) then
      local nrpn1 = midi.createNRPN()
      midi.setNRPN(nrpn1, 1, 12345, 13456)
      midiOut.send(nrpn1)
   end
end
```

### Send 14-bit CC message

A 14-bit CC value spans two CC messages (CC `cc` = value MSB, CC `cc + 32` =
value LSB). `midi.createCc14bit()` chains the two into one atomic pair, so a
receiver never sees the MSB without its LSB.

JavaScript:
```js
midi.onMessage = function(midiPort, msg) {
   if (midi.isNoteOn(msg)) {
      let cc14 = midi.createCc14bit();
      midi.setCc14bit(cc14, 1, 1, 100.5);  // CC 1 = 100 (MSB), CC 33 = 64 (LSB)
      midiOut.send(cc14);
   }
};
```

Lua:
```lua
midi.onMessage = function(midiPort, msg)
   if midi.isNoteOn(msg) then
      local cc14 = midi.createCc14bit()
      midi.setCc14bit(cc14, 1, 1, 100.5)   -- CC 1 = 100 (MSB), CC 33 = 64 (LSB)
      midiOut.send(cc14)
   end
end
```

### Send SysEx message

JavaScript:
```js
midi.onMessage = function(midiPort, msg) {
   if (midi.isNoteOn(msg)) {
      let sysex = midi.create();
      midi.setSysEx(sysex, "ab33010001");
      midiOut.send(sysex);
   }
};
```

Lua:
```lua
midi.onMessage = function(midiPort, msg)
   if midi.isNoteOn(msg) then
      local sysex = midi.create()
      midi.setSysEx(sysex, "ab33010001")
      midiOut.send(sysex)
   end
end
```

### Send a raw MIDI message

Use `midi.setRaw()` for message types with no dedicated setter, such as an MTC quarter-frame message (status `0xf1`).

JavaScript:
```js
midi.onMessage = function(midiPort, msg) {
   if (midi.isNoteOn(msg)) {
      let mtc = midi.create();
      midi.setRaw(mtc, "f11a");
      midiOut.send(mtc);
   }
};
```

Lua:
```lua
midi.onMessage = function(midiPort, msg)
   if midi.isNoteOn(msg) then
      local mtc = midi.create()
      midi.setRaw(mtc, "f11a")
      midiOut.send(mtc)
   end
end
```

### Send an all-notes-off when the script unloads

`rack.onUnload()` runs right before the script's state is torn down — the script is being replaced, the module is reset, or the module is removed from the patch. It's the only reliable place to clean up notes a script left sounding, since nothing runs afterward to release them. It never runs on a plain patch save (see [Persistence](#persistence) for the hook that does: `rack.onSave()`). Note the JavaScript version assigns it to the `rack` object — `rack.onUnload = function() {...}` — like the other hooks (see [Hooks and predefined objects are resolved once, at load time](#hooks-and-predefined-objects-are-resolved-once-at-load-time)).

JavaScript:
```js
midi.onMessage = function(midiPort, msg) {
   if (midi.isNoteOn(msg)) {
      midiOut.send(msg);
   }
};

rack.onUnload = function() {
   for (let note = 0; note < 128; note++) {
      let off = midi.create();
      midi.setNoteOff(off, 1, note);
      midiOut.send(off);
   }
};
```

Lua:
```lua
midi.onMessage = function(midiPort, msg)
   if midi.isNoteOn(msg) then
      midiOut.send(msg)
   end
end

rack.onUnload = function()
   for note = 0, 127 do
      local off = midi.create()
      midi.setNoteOff(off, 1, note)
      midiOut.send(off)
   end
end
```

### Send a MIDI clock message on each trigger

`trig.onTrigger(trigPort, channel)` is the entry point for logic driven by the CV trigger input rather than by incoming MIDI — for example, forwarding an external clock as MIDI clock messages. `channel` (1-based) is the polyphonic channel of the trigger input that fired. **The callback is not used until `trig.enableIn(trigPort, [channel])` is called** — here `trig.enableIn(1)` clocks it from channel 1 of the trigger input.

JavaScript:
```js
trig.enableIn(1);

trig.onTrigger = function(trigPort, channel) {
   let clock = midi.create();
   midi.setRaw(clock, "f8");
   midiOut.send(clock);
};
```

Lua:
```lua
trig.enableIn(1)

trig.onTrigger = function(trigPort, channel)
   local clock = midi.create()
   midi.setRaw(clock, "f8")
   midiOut.send(clock)
end
```

### Tipsy protocol — send and receive over CV

Tipsy is a protocol for exchanging arbitrary data between modules as a stream
of CV voltages on the trigger input/output. Sending encodes a payload as
voltages on the trigger output (`trig.sendTipsy`); receiving routes the
trigger input into MIDI-KIT's Tipsy decoder (`trig.enableTipsyIn`) and
delivers each complete message to `trig.onTipsyMessage`. The reference for
all three functions is [Tipsy under `trig.*`](#tipsy).

**Sending — `trig.sendTipsy`**

`trig.sendTipsy(data, [mimeType = "text/plain"])` encodes binary `data` using the Tipsy protocol and outputs it as CV voltages on the trigger output. This is useful for communicating with modules that understand the Tipsy protocol, such as Transit for preset snapshots.

JavaScript:
```js
midi.onMessage = function(midiPort, msg) {
   // Send a text message via Tipsy protocol (mime defaults to text/plain)
   trig.sendTipsy("Preset changed!");
   
   // Or send JSON data with an explicit mime type
   let config = JSON.stringify({ channel: 1, mode: "auto" });
   trig.sendTipsy(config, "application/json");
};
```

Lua:
```lua
midi.onMessage = function(midiPort, msg)
   -- Send a text message via Tipsy protocol (mime defaults to text/plain)
   trig.sendTipsy("Preset changed!")
   
   -- Or send JSON-like data with an explicit mime type
   local config = '{"channel":1,"mode":"auto"}'
   trig.sendTipsy(config, "application/json")
end
```

**Note:** The Tipsy-encoded data is output sequentially as CV voltages on the trigger output, one voltage per sample. The receiving module must understand the Tipsy protocol to decode the data correctly. When no Tipsy message is being sent, the trigger output is driven by the script's `trig.*` functions; a `trig.sendTipsy` call temporarily takes over the trigger output while its encoded stream is transmitted.

**Receiving — `trig.enableTipsyIn` / `trig.onTipsyMessage`**

`trig.enableTipsyIn()` routes the **trigger input** (`TRIG`) into MIDI-KIT's Tipsy decoder. Every complete message that arrives is delivered to `trig.onTipsyMessage(data, mimeType)`. Pass `false` to release the trigger input again. Tipsy input is only supported on the first trigger input, so — like `trig.sendTipsy()` — there is no port argument.

Note that while the trigger input is claimed for Tipsy, channel 1 no longer behaves as a trigger — `trig.onTrigger` doesn't fire and `trig.getTicks()` doesn't advance there, since the encoded voltages swing across the trigger threshold constantly and would otherwise fire on nearly every sample. Other channels are unaffected.

JavaScript:
```js
rack.onLoad = function() {
   trig.enableTipsyIn();        // decode a Tipsy stream from the trigger input
};

trig.onTipsyMessage = function(data, mimeType) {
   rack.log("received " + mimeType + ": " + data);

   if (mimeType === "application/json") {
      let config = JSON.parse(data);
      // ... use config
   }
};
```

Lua:
```lua
rack.onLoad = function()
   trig.enableTipsyIn()         -- decode a Tipsy stream from the trigger input
end

trig.onTipsyMessage = function(data, mimeType)
   rack.log("received " .. mimeType .. ": " .. data)
end
```

**Note:** While the trigger input is claimed for Tipsy, channel 1 no longer behaves as a trigger — `trig.onTrigger` doesn't fire and `trig.getTicks()` doesn't advance there, and `trig.isHigh()`/`trig.isLow()` on channel 1 read `0` (other channels are unaffected). Releasing it with `trig.enableTipsyIn(false)` restores normal trigger behavior. Payloads are capped at 256 bytes, and `data` may contain arbitrary bytes including NULs. A malformed or interrupted stream is reported once in the module log and the decoder resynchronizes automatically on the next message.

### Add items to the module's context menu

`rack.registerContextMenu()` adds items to the module's right-click context menu — a boolean toggle (a menu line with a checkmark) or an options submenu (one entry per option, checkmark on the current selection). Items appear in registration order and can be used to change `config` values live instead of editing the script.

The checkmark/selection state is read **lazily** — each time the menu is opened, the engine calls the item's `onGetValue` callback (if provided) to determine the current value. This means the menu always reflects the live state of the script, even if it was changed programmatically. If `onGetValue` is omitted, the item defaults to `false` (boolean) or `0` (options, i.e. the first option).

JavaScript:
```js
config.channel = 1;

rack.registerContextMenu({
    type: "options",
    label: "MIDI channel",
    options: ["1", "2", "3"],
    onGetValue: function() {
        return config.channel - 1;
    },
    onChange: function(idx) {
        config.channel = idx + 1;
    }
});

rack.registerContextMenu({
    type: "boolean",
    label: "Pass through",
    onGetValue: function() {
        return config.passThrough;
    },
    onChange: function(checked) {
        config.passThrough = checked;
    }
});
```

Lua:
```lua
config.channel = 1

rack.registerContextMenu({
    type = "options",
    label = "MIDI channel",
    options = { "1", "2", "3" },
    onGetValue = function()
        return config.channel - 1
    end,
    onChange = function(idx)
        config.channel = idx + 1
    end
})

rack.registerContextMenu({
    type = "boolean",
    label = "Pass through",
    onGetValue = function()
        return config.passThrough
    end,
    onChange = function(checked)
        config.passThrough = checked
    end
})
```

### Assemble NRPN input

This is the assembled-input alternative to the manual "Send NRPN message"-style examples: instead of constructing an NRPN from parts, the module reassembles a spec-compliant NRPN write (CC 99/98 = parameter select, then CC 6/38 = data entry) into a single parameter change and delivers it to `midi.onNrpn`. Enable it with `midi.enableNrpnIn(midiPort [, channel])`. While NRPN input is enabled, the component CCs it is assembled from no longer reach `midi.onMessage` — they are consumed by the assembler. The example reads the NRPN number with `midi.getControl(msg)` and the combined 14-bit value with `midi.getValue(msg)`, looks the number up in a small `config.map`, and forwards the change as an atomic 14-bit CC pair with `midi.createCc14bit()` + `midi.setCc14bit()` + `midiOut.send()`.

JavaScript:
```js
let config = {
    map: [
        { nrpnNumber: 0, ccNumber: 0 },
        { nrpnNumber: 1, ccNumber: 1 },
        { nrpnNumber: 2, ccNumber: 2 }
    ],
    ccChannel: 1
};

function findCcNumber(nrpnNumber) {
    let ccNumber = -1;
    for (let i = 0; i < config.map.length; i++) {
        if (config.map[i].nrpnNumber === nrpnNumber) {
            ccNumber = config.map[i].ccNumber;
            break;
        }
    }
    return ccNumber;
}

midi.enableNrpnIn(1);

midi.onNrpn = function(midiPort, msg) {
    let nrpnNumber = midi.getControl(msg);
    let nrpnValue = midi.getValue(msg);

    let ccNumber = findCcNumber(nrpnNumber);
    if (ccNumber < 0) return;   // not in config.map — ignore

    let cc14 = midi.createCc14bit();
    midi.setCc14bit(cc14, config.ccChannel, ccNumber, nrpnValue / 128);
    midiOut.send(cc14);
};
```

Lua:
```lua
local config = {
    map = {
        { nrpnNumber = 0, ccNumber = 0 },
        { nrpnNumber = 1, ccNumber = 1 },
        { nrpnNumber = 2, ccNumber = 2 }
    },
    ccChannel = 1
}

local function findCcNumber(nrpnNumber)
    local ccNumber = -1
    for i = 1, #config.map do
        if config.map[i].nrpnNumber == nrpnNumber then
            ccNumber = config.map[i].ccNumber
            break
        end
    end
    return ccNumber
end

midi.enableNrpnIn(1)

midi.onNrpn = function(midiPort, msg)
    local nrpnNumber = midi.getControl(msg)
    local nrpnValue = midi.getValue(msg)

    local ccNumber = findCcNumber(nrpnNumber)
    if ccNumber < 0 then return end   -- not in config.map, ignore

    local cc14 = midi.createCc14bit()
    midi.setCc14bit(cc14, config.ccChannel, ccNumber, nrpnValue / 128)
    midiOut.send(cc14)
end
```

**Note:** The shipped preset `NRPN to CC (assembled)` is this script with a context-menu channel selector added — see [Assembled extended input](#assembled-extended-input-nrpn--rpn--14-bit-cc) for the full rules.


## Part 3 — API reference

### Hooks and predefined objects are resolved once, at load time

`midi.onMessage` is read from the `midi` object **exactly once**;
`rack.onLoad`, `rack.onUnload`, and `rack.onSave` from the `rack` object; and
`trig.onTrigger`/`trig.onTipsyMessage` from the `trig` object — all right
after the script's top-level code finishes running. **Reassigning any of them
afterward, from inside a callback or anywhere else, has no effect.** The
function present at load time keeps running for the script's lifetime; a
later assignment is simply never looked at again. The same applies to
defining a hook **late**: a script that never defines `midi.onMessage` at
load time but assigns it later (e.g. from inside `trig.onTrigger`) never has
that later definition called — the "no `midi.onMessage` defined" warning
logged at load time is the last word on it. (`trig.enableIn()` is not a hook —
it's a live API call like `param.enable()`, so calling it at any time takes
effect for subsequent trigger dispatch.)

This is a deliberate, permanent design choice: resolving hooks once, rather
than looking them up by name on every incoming MIDI message or trigger tick,
is what keeps message dispatch fast. Write scripts so every hook is assigned
exactly once, at the top level, during initial load — this is what every
shipped preset does and the only supported pattern.

Reassigning (or otherwise clobbering — e.g. `rack = 42`) one of the
predefined globals (`rack`, `midi`, `midiOut`, `trig`, `input`, `param`,
`number`) at runtime is likewise unsupported. Both engines degrade gracefully
rather than crash if a script does this anyway — expect either the
reassignment to be silently ignored (for a hook function, per above) or a
caught, logged script error on the next statement that uses the clobbered
value — but the resulting behavior is undefined and may differ in wording
between QuickJs and Lua. Treat this as a bug in the script, not a supported
technique.

### `rack.*`

| Function | Effect |
| --- | --- |
| `rack.log(value [, value ...])` | write a line to the module's log/console. Any number of arguments are concatenated (no separator) into one line, each coerced the same way as a single value: strings logged verbatim (no added quotes), numbers formatted like `number.toString()` (so `rack.log(1 / 3)` prints `0.333333`), booleans as `true`/`false`, `null`/`undefined` (QuickJs) / `nil` (Lua) as `null`/`undefined`. Other values (objects, arrays, tables, functions) use each engine's own stringification — scalars are guaranteed to format identically in both engines |
| `rack.overlay(s1 [, s2 [, s3]])` | show up to 3 lines in the on-panel overlay |
| `rack.getFrame()` | the current engine frame number (`APP->engine->getFrame()`) |
| `rack.random()` | a random number in [0, 1), drawn from Rack's own RNG (`rack::random::uniform()`), so it shares the patch's seed/determinism |

`rack.onLoad`/`rack.onUnload`/`rack.onSave` (script lifecycle) and
`rack.registerContextMenu` (below) are documented in their own subsections.

#### Context menu — `rack.registerContextMenu`

`rack.registerContextMenu(options)` adds one item to the module's right-click
context menu, in registration order (multiple items are allowed). Returns
`true` on success; throws (load fails) if `options` is malformed. Two
variants:

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
Lua uses an equivalent table: `{ type = "boolean", label = "...",
onGetValue = function() return config.emitTrigger end,
onChange = function(checked) ... end }`.

Notes:
- `label` must be a non-empty string; `options` must be a non-empty array of
  strings; `onChange` must be a function.
- `onGetValue` is optional and, when present, must be a function returning
  the item's current value: a boolean for `type: "boolean"`, an index number
  for `type: "options"`. It is evaluated just-in-time on the worker thread
  every time the context menu is opened, so the checkmark/selection always
  reflects the script's live state — including config restored by
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

### Persistence

`rack.onLoad`, `rack.onUnload`, and `rack.onSave` cover a script's whole
lifecycle. All three may call `midi.create()`/`midiOut.send()` like
`midi.onMessage` can; messages sent from any of them are flushed the same way,
except `onSave()`'s (see below).

- **`rack.onLoad([persistedConfig])`** runs once, right after top-level code,
  when the script has parsed and loaded successfully. If a config was
  persisted by a previous save, it is deserialized from JSON and passed as
  `persistedConfig` (a JavaScript object in QuickJs, a Lua table in Lua);
  otherwise the argument is `undefined` (QuickJs) / `nil` (Lua) and the script
  initializes from its own defaults.
- **`rack.onUnload()`** runs every time the *current* script's state is torn
  down for real — it's about to be replaced by another script, the module was
  reset, or the module is being removed from the patch. This is the only
  place a script can reliably clean up: sending an all-notes-off for anything
  it left sounding is the main use case, since nothing else ever gets a
  chance to release those notes once the script's state is gone. **Its return
  value is ignored** — it is teardown-only and not part of the persistence
  contract. Scripts written before `rack.onSave()` existed that only returned
  their config from `onUnload()` will persist nothing until migrated; there is
  no automatic fallback.
- **`rack.onSave()`** is the config-bearing hook: if it returns a value, that
  value is serialized to JSON and stored with the module's patch data. It
  **must be side-effect-free** — it may be called repeatedly (e.g. on every
  explicit patch save) without tearing down or otherwise disturbing the
  script's state, so it should only read state, never mutate it or send MIDI
  messages meant to have an audible effect (any it does send are discarded,
  see below).

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
- `trig.enableIn(trigPort [, ch])` — enable trigger input `trigPort` (polyphonic
  channel defaults to 1) for trigger processing and the `trig.onTrigger`
  callback. **The trigger input does nothing until this is called**: a
  channel that is never enabled counts no ticks (`trig.getTicks()` stays 0),
  drains no tick-scheduled (`sendAfterTrigger`) messages, and never fires
  `trig.onTrigger`. Call once per (port, channel) the script wants to hear; a
  polyphonic clock is enabled per channel, e.g. `trig.enableIn(1, 1)` plus
  `trig.enableIn(1, 2)`.
- `trig.onTrigger(trigPort, ch)` — the trigger callback, assigned on the
  `trig` object (resolved once at load, like the `rack` hooks). Called on
  every rising edge of an *enabled* (port, channel); `trigPort` is 1-based
  (always `1`), `ch` is the 1-based polyphonic channel that fired. Without a
  matching `trig.enableIn()` call it is never called.
- `trig.getTicks(i [, ch])` — clock tick counter for trigger input `i`
  (polyphonic channel defaults to 1).
- `trig.isHigh(i [, ch])`, `trig.isLow(i [, ch])`.
- `trig.setHigh(i [, ch])`, `trig.setLow(i [, ch])`, `trig.setTrigger(i [, ch])`
  (momentary trigger), `trig.setGate(i [, ch], durationMs)`.

#### Tipsy

All three Tipsy functions — the output sender, the input decoder, and the
input callback — are described together here; a worked send/receive example
is in [Tipsy protocol — send and receive over CV](#tipsy-protocol--send-and-receive-over-cv).

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
  trig.enableIn(1);
  trig.onTrigger = function(trigPort, channel) {
      trig.sendTipsy("Hello Tipsy!");                              // mime defaults to "text/plain"
      trig.sendTipsy('{"label":"My snapshot","value":42}', "application/json");
  };
  ```
- `trig.enableTipsyIn([enabled])` — decode an incoming Tipsy stream from the
  trigger input, delivering each completed message to `trig.onTipsyMessage`.
  The optional boolean `enabled` defaults to `true`; pass `false` to release
  the trigger input again. Tipsy input is only supported on the first trigger
  input, so — like `trig.sendTipsy()` — there is no port argument.

  While the trigger input is claimed, it stops behaving as a trigger on
  channel 1: `trig.onTrigger` doesn't fire and `trig.getTicks()` doesn't
  advance there, and `trig.isHigh()`/`trig.isLow()` on channel 1 read `0` —
  the encoded voltages are protocol, not a gate a script should act on.
  Other channels are unaffected. A Tipsy stream would otherwise fire
  `trig.onTrigger` continuously as the encoded voltages cross the trigger
  threshold.
  ```js
  rack.onLoad = function() {
      trig.enableTipsyIn();        // decode from the trigger input
  };
  ```
- `trig.onTipsyMessage(data, mimeType)` — the Tipsy input callback, assigned
  on the `trig` object (resolved once at load, like the `rack` hooks). Called
  once for every complete Tipsy message decoded from the trigger input claimed
  with `trig.enableTipsyIn()`; see that entry above. `data` and `mimeType` are
  strings; `data` may contain arbitrary bytes (including NULs) and is capped
  at 256 bytes.

### `param.*` (panel knobs)
- `param.enable(i)` — activate param `i`.
- `param.getValue(i)` — normalized 0..1 value.
- Override `param.getName(i)` and `param.getValueFormat(i)` for panel display.

### `midi.*` — message construction/inspection
Messages are opaque handles (indices into an internal store, max 32 live per
callback) created with `midi.create()`, `midi.createNRPN()`, or
`midi.createCc14bit()`; `midi.onMessage`
also receives the incoming message as handle `0`/implicit first arg (Lua:
index `0`, QuickJs: same convention).

**The store holds at most 32 live handles per callback.** Once it is full,
`midi.create()`, `midi.clone()`, `midi.createNRPN()`, and `midi.createCc14bit()`
raise a script error
that aborts the rest of the callback. Messages already marked for send before
the error are still flushed, so a multi-message sequence (e.g. an NRPN pair,
or a wide chord release) can be emitted partially — a message created but
never sent is dropped. Keep callbacks within the cap, or create/send in
batches (one handle per message, per the send-once rule below).

#### Entry points

- `midi.onMessage(midiPort, msg)` — the incoming-MIDI entry point (see
  [Script structure](#script-structure)): called with each incoming message
  that nothing else claimed (see
  [Assembled extended input](#assembled-extended-input-nrpn--rpn--14-bit-cc)
  for the callbacks that receive assembled parameter changes instead).
- `midi.onNrpn(midiPort, msg)` / `midi.onRpn(midiPort, msg)` /
  `midi.onCc14bit(midiPort, msg)` — called with an assembled NRPN/RPN
  parameter change or 14-bit controller change (see
  [Assembled extended input](#assembled-extended-input-nrpn--rpn--14-bit-cc)).
  Only fire for what the script enabled; `midi.onMessage` does not see the
  component CCs such a change was built from.

#### Constructors

- `midi.create()` → new empty message handle.
- `midi.clone(msg)` → new message handle carrying an independent copy of
  `msg`'s MIDI payload. The clone starts as a fresh, unsent message (its own
  store slot), so it can be modified and sent without affecting the source.
  This is the canonical way to "send a modified copy of the incoming message",
  e.g. `let copy = midi.clone(msg); midi.setChannel(copy, 5); midiOut.send(copy);`
  Note: NRPN/14-bit-CC chain state is not copied — a clone of an NRPN or
  `createCc14bit()` handle is a single plain message, not a chained group.
- `midi.createNRPN()` → 4 chained handles (param LSB/MSB + value LSB/MSB),
  used only with `midi.setNRPN`.
- `midi.createCc14bit()` → 2 chained handles (value MSB at CC `cc`, value LSB
  at CC `cc + 32`), used only with `midi.setCc14bit`; the pair is sent
  atomically — a receiver never sees the MSB without its LSB.

#### Getters

| Function | Returns |
| --- | --- |
| `getChannel(msg)` | 1-based channel; `-1` for realtime/SysEx messages (clock, start/stop/continue, SysEx framing), which have no channel |
| `getChanPressure(msg)` | channel-pressure value |
| `getControl(msg)` | see [Assembled extended input](#assembled-extended-input-nrpn--rpn--14-bit-cc) for the type-aware behavior on assembled messages |
| `getNote(msg)` | note number (or, on a plain CC, the controller number — the older spelling of `getControl`) |
| `getValue(msg)` | type-aware: raw 7-bit data byte, or the combined 14-bit value on an assembled NRPN/RPN/14-bit CC |
| `getLength(msg)` | note length |
| `getPitchWheel(msg)` | pitch-wheel value |
| `getProgramChange(msg)` | program number |
| `getSysEx(msg)` | hex string, payload only — without the `f0`/`f7` framing |
| `getSysExLength(msg)` | payload length in bytes, framing excluded — check before reading with `getSysEx` |
| `getRaw(msg)` | hex string of the message's raw bytes, exactly as sent/received — no framing added or removed |

#### Type predicates

`isCc`, `isNoteOn`, `isNoteOff`, `isKeyPressure`, `isChanPressure`,
`isProgramChange`, `isPitchWheel`, `isSysEx`, `isClock`, `isStart`,
`isContinue`, `isStop` — all `is*(msg)`. Plus `isNrpn`, `isRpn`, `isCc14bit`,
true only for assembled extended messages (see
[Assembled extended input](#assembled-extended-input-nrpn--rpn--14-bit-cc)).

#### Setters

Every `ch` argument below is a MIDI channel and is silently clamped to 1-16
(e.g. `setNoteOn(msg, -5, ...)` is treated as channel 1).

| Function | Notes |
| --- | --- |
| `setCc(msg, ch, cc, value)` | `value` clamped to 0-127 |
| `setCc14bit(msgMsb, msgLsb, ch, cc, value)` | fills two independent handles, sent as two separate messages with no atomicity |
| `setCc14bit(cc14, ch, cc, value)` | `cc14` is the first handle of a `midi.createCc14bit()` pair; both CCs sent atomically as a unit |
| `setChannel(msg, ch)` | |
| `setChanPressure(msg, ch, value)` | 2-byte message; read back with `getChanPressure`, not `getValue` |
| `setKeyPressure(msg, ch, note, vel)` | `vel` clamped to 0-127 |
| `setNote(msg, note)` | |
| `setNoteOn(msg, ch, note, vel)` | `vel` clamped to 0-127 |
| `setNoteOff(msg, ch, note [, vel])` | release velocity defaults to 0, clamped to 0-127; read back with `getValue` |
| `setNRPN(nrpnHandle, ch, number, value)` | `number`/`value` are 14-bit, 0-16383 |
| `setPitchWheel(msg, ch, value)` | |
| `setProgramChange(msg, ch, program)` | |
| `setSysEx(msg, hexString)` | payload only — `f0`/`f7` framing added automatically, so pass e.g. `"43104c0000"` rather than `"f043104c0000f7"`; capped at 256 bytes, every byte must be 7-bit (`00`-`7f`) |
| `setRaw(msg, hexString)` | writes the exact bytes with no framing added, e.g. `"f11a"` for an MTC quarter-frame — use for message types with no dedicated setter |
| `setValue(msg, value)` | |

Both `setCc14bit` forms take `value` as a float (MSB = integer part,
LSB = fractional part × 128) — see `nrpn_to_cc.js`/`.lua` for the canonical
use.

#### Assembled extended input (NRPN / RPN / 14-bit CC)

`midi.onMessage` sees the MIDI stream as it arrives — including the raw CCs
that make up a spec-compliant NRPN/RPN parameter change or a 14-bit CC pair.
Reading those by hand (a select handshake on CC 99/98, data entry on 6/38, an
MSB/LSB pair on CC `n`/`n + 32`) is exactly the boilerplate the old
`NRPN to CC` example shipped. If you want *parameter changes*, not raw CCs,
the engine can assemble them for you — the mirror image of `midi.setNRPN()` /
`midi.setCc14bit()` on the way out.

**Enabling and callbacks**

| Function | Effect |
| --- | --- |
| `midi.enableNrpnIn(midiPort [, channel])` | assemble NRPN (kind 0) parameter changes on `midiPort` into `midi.onNrpn` calls. `channel` is 1-based (default: all) |
| `midi.enableRpnIn(midiPort [, channel])` | same, for RPN (kind 1) into `midi.onRpn` |
| `midi.enableCc14bitIn(midiPort [, cc] [, channel])` | assemble 14-bit CC pairs on `midiPort` into `midi.onCc14bit` calls. `cc` is the MSB controller number 0-31 (its LSB is implicitly `cc + 32`); omit it to enable every 14-bit CC |
| `midi.onNrpn(midiPort, msg)` / `midi.onRpn(midiPort, msg)` / `midi.onCc14bit(midiPort, msg)` | called once per completed, enabled parameter change, with `msg` an ordinary handle read through the usual accessors |
| `midi.isNrpn(msg)` / `midi.isRpn(msg)` / `midi.isCc14bit(msg)` | true only for an assembled message of that kind; useful when a handle is passed to a helper or inspected later — redundant inside the matching callback, but makes the handle self-describing |

**Enabling a kind without defining its callback is a mistake**: the message
then reaches nothing at all, and its component CCs are withheld from
`midi.onMessage` (see Consumption below), so the script sees strictly less
MIDI than before.

**Reading an assembled message**

- `midi.getControl(msg)` — "which controller is this?", for every
  controller-ish message: the controller number of a plain CC (0-127), the
  MSB controller of an assembled 14-bit CC (0-31), the parameter number of an
  assembled NRPN/RPN (0-16383), and `-1` for anything that addresses none
  (notes, pitch bend, clock, ...). **This is the preferred way to read a
  controller number**; on a plain CC `midi.getNote(msg)` returns the same
  byte and still works, but it is the older spelling. Assembled messages
  carry all three alongside each other: `getControl()` = the parameter,
  `getValue()` = the combined 14-bit value, `getNote()` = the raw CC that
  completed the message (e.g. 38, the Data Entry LSB).
- `midi.getValue(msg)` is **type-aware**: on an assembled NRPN/RPN/14-bit CC
  it returns the combined 14-bit value (0-16383); on everything else the raw
  7-bit data byte exactly as before.

**Consumption**

Once a kind is enabled, the CCs it is built from stop reaching
`midi.onMessage` — a script that asked for assembled events should not also
have to filter the parts they were assembled from. This mirrors
`trig.enableTipsyIn()`, which stops `trig.onTrigger` while the trigger input
is claimed. The raw CCs are swallowed, not released: if a device drops a
message mid-quad, the consumed components are gone. Rules:

- **Matching-enable only.** `midi.onMessage` keeps its meaning — "a message
  arrived that nothing else claimed". A component CC is withheld only when
  the *kind* of assembly it belongs to is enabled: a script that enabled only
  14-bit CC still sees CC 98/99 (NRPN parameter select) raw, because it did
  not enable NRPN.
- **Parameter select fires nothing.** A parameter *select* (CC 99/98 or
  101/100 without a following data entry) fires no callback; only the
  completed change (data entry CC 6/38) does. The RPN 127/127 reset likewise.
- **CC 6/38 overlap.** CC 0-31 are simultaneously 14-bit MSBs and, for CC 6,
  Data Entry MSB — the spec's ranges overlap. So with blanket 14-bit CC *and*
  NRPN enabled, CC 6/38 are consumed as a 14-bit pair (they *are* one by the
  spec's numbering) and a data entry can fire both `onCc14bit` and `onNrpn`.
  This is accepted behaviour, not a bug; register per-CC with
  `midi.enableCc14bitIn(midiPort, cc)` if you want a specific 14-bit CC
  enabled while leaving 6/38 alone.
- **MSB of 0 needs a prior MSB.** An MSB of value 0 on a controller that was
  never seen is ignored, so no spurious 14-bit event fires after a MIDI
  reset; only a zero MSB on a controller that was already seen produces one.
- **Sending an assembled handle back out emits only its final CC** — a clone
  or `midiOut.send()` of an assembled handle is a single plain message, not a
  reconstructed quad (same rule as the send-side chain handles). Use
  `midi.setNRPN()` / `midi.setCc14bit()` to rebuild the full sequence on the
  way out.

### `midiOut.*` — sending

- `midiOut.selectPort(midiPort)` — selects the output port (1-based) that every
  subsequent `midiOut.*` call sends on, until `selectPort` is called again.
  The selection is sticky across `midi.onMessage` invocations, not reset per
  callback. MIDI-KIT currently exposes a single output, so
  `midiOut.selectPort(1)` is a no-op today beyond validating the index — it
  exists so scripts written against a future multi-output engine don't need to
  change their sending code.

The sending functions below take no port argument — the destination is
whatever `midiOut.selectPort()` last selected (port 1 if it was never called):

- `midiOut.send(msg)` — send immediately.
- `midiOut.send(nrpnHandle)` / `midiOut.send(cc14Handle)` — sending the first
  handle of an NRPN quad (4 messages) or a 14-bit CC pair (2 messages)
  automatically flushes the whole group in order.
- `midiOut.sendAfterMs(msg, ms)` — delayed send, scheduled from the current
  engine frame.
- `midiOut.sendAfterTrigger(msg, ticks [, trigPort [, channel]])` — send
  after `ticks` clock ticks counted from `trigPort` (1-based, defaults to
  trig input 1) on polyphonic `channel` (defaults to 1).

**A message can only be sent once per callback.** `midiOut.send(msg)` (and the
`sendAfter*` variants) mark the handle as sent; the actual enqueue happens once
per handle in the post-callback flush, so a second `send` of the *same* handle
within one `midi.onMessage`/`rack.onLoad`/`rack.onUnload`/`rack.onSave` is not a second message — only
one goes out, and if the message body was changed in between, the last change
wins. To send the same bytes twice, build a fresh handle first with
`midi.create()` or `midi.clone(msg)` and send that. Each message sent consumes
one store slot against the 32-handle cap, so one handle per message is the
correct idiom.

### MIDI status/type reference used internally
CC=0xb, NoteOn=0x9, NoteOff=0x8, KeyPressure=0xa, ChanPressure=0xd,
ProgramChange=0xc, PitchWheel=0xe, SysEx=0xf0/0xf7-wrapped. Realtime:
Clock=0xF8, Start=0xFA, Continue=0xFB, Stop=0xFC (encoded as status 0xf with
"channel" nibble 0x8/0xa/0xb/0xc respectively — use the `is*` predicates
rather than decoding this by hand).

## Part 4 — Gotchas
- Message handles are only valid within the `midi.onMessage` call that
  created them — the store resets each callback invocation. Creating a
  message at top level (outside `midi.onMessage`) logs a warning and the
  handle is discarded as soon as the next MIDI message arrives, so build
  messages inside the callback. `rack.onLoad()`/`rack.onUnload()`/`rack.onSave()`/`trig.onTrigger()`
  are full callbacks in this sense too — a message created and sent inside
  any of them is delivered normally (except `onSave()`'s, which are always
  discarded — see [Persistence](#persistence)), and (unlike bare top-level
  code) doesn't warn.
- `midi.setCc14bit`/`setNRPN` split a 14-bit value across two 7-bit CC
  messages (`cc` = MSB, `cc + 32` = LSB per the NRPN/14-bit CC convention);
  see [nrpn_to_cc.js](nrpn_to_cc.js)/[nrpn_to_cc.lua](nrpn_to_cc.lua) for a
  full worked example, and [nrpn_generator.js](nrpn_generator.js)/
  [nrpn_generator.lua](nrpn_generator.lua) for constructing NRPN messages.
  Use `midi.createCc14bit()` + the 4-arg `setCc14bit` for a 14-bit CC pair
  that must land atomically; the two-handle form sends two independent
  messages.
- Lua's sandboxed stdlib excludes `io`, `os`, `package`, `debug` — no file
  access, no OS calls, by design.
- Both engines only see `@engine`-matching scripts; loading a QuickJs script
  into what expects `@engine minilua@v1` (or vice versa) fails with an explicit
  "not compatible" log message rather than silently misinterpreting it.
