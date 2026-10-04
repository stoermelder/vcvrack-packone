# MIDI-KIT scripting reference

MIDI-KIT runs a script that reads incoming MIDI, trigger and CV, and sends MIDI, triggers and voltages. The script is written in one of two embedded languages, picked by a versioned `@engine` tag in its header:

- `QuickJs@v1`: JavaScript (a full ES2020 engine)
- `minilua@v1`: Lua 5.4 (sandboxed, via minilua)

Both engines offer the *same* API: `midi`, `midiOut`, `input`, `trig`, `param`, `number` and `rack`. Ports, channels and params are **1-based** everywhere. The module identifies the engine from the header, not from the file extension.

**Contents**

| Part | What is in it |
| --- | --- |
| [Part 1 — Writing a script](#part-1--writing-a-script) | engine choice, file header, script structure and callbacks |
| [Part 2 — Examples](#part-2--examples) | worked scripts from basic pass-through to context menus, assembled NRPN input and messages between modules |
| [Part 3 — API reference](#part-3--api-reference) | every `rack.*`, `input.*`, `trig.*`, `param.*`, `midi.*`, `midiOut.*` and `number.*` function, persistence, messages between modules, sample-accurate timing |
| [Part 4 — Gotchas](#part-4--gotchas) | the mistakes that cost the most time |

## Part 1 — Writing a script

### When to write QuickJs (JavaScript) vs Lua

Both engines handle the common case equally well: reacting to
`midi.onMessage`, building/sending messages. Pick based on these differences:

| | QuickJs (JS) | Lua |
|---|---|---|
| Language completeness | Full JavaScript (ES2020): `while`, `switch`, `try`, `class`, `new`, `this`, `var`/`let`/`const`, function declarations, arrow functions | Full Lua 5.4 syntax; only the *library* is trimmed |
| Data structures | Array literals `[1,2,3]`, object literals `{a:1}` | Only tables (`{}`); no literal array sugar, must use `{ {...}, {...} }` and `#t`/`ipairs` |
| Stdlib | Full JS standard library: `Math`, `JSON`, `String`, `Array`, ... | Real Lua stdlib subset: `math`, `string`, `table` (no `io`, `os`, `package`, `debug` — sandboxed), plus `string.split(s, sep, limit)` (also `s:split(sep)`), which works like JavaScript's `split`: `sep` is a plain string, not a pattern, empty fields are kept (`("a,,b"):split(",")` gives three pieces), an empty `sep` gives single bytes, and the optional `limit` caps the number of pieces; and a `json` table with `json.encode(value)` and `json.decode(string)` (bundled [json.lua](https://github.com/rxi/json.lua) by rxi, MIT). Invalid input raises an error, so wrap `json.decode` in `pcall`; JSON `null` decodes to `nil` |
| String formatting | JS auto-coerces numbers in `+` concatenation; `number.toString()` helper available | Lua auto-coerces numbers in `..` concatenation; `string.format` available |
| Familiarity | Preferred if the user/preset is JS-oriented or ports logic from another JS script | Preferred if the script needs `string.format`, `table.sort`, pattern matching, or other real stdlib features |
| Performance/footprint | QuickJS is a full embeddable JS engine with a 1 MiB memory limit | minilua is a stripped full Lua VM; similarly small footprint |

Default guidance: **match whatever sibling/companion scripts in the same
preset pair already use**, so users can read them side by side. Otherwise,
prefer Lua for string formatting or table sorting, and QuickJs for
array/object literal syntax or scripts adapted from existing JS examples.

### Required file header

The script starts with a comment block of `@key value` tags. Only this leading block is scanned for tags; nothing after it is.

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

| Tag | Required | Meaning |
| --- | --- | --- |
| `@engine` | yes | `QuickJs@v1` or `minilua@v1`, exactly. Anything else rejects the script |
| `@author`, `@description` | no | echoed to the module log on load |
| `@target` | no | conventional, not checked |
| `@requires` | no | what the script needs from the module, see below |

The `@v1` suffix pins the script to an engine protocol revision, so a future breaking change can become `@v2` without old scripts silently misbehaving. The engine is found by a plain substring search for `@engine <name>@vN` in the header block, so keep the tag inside the leading comment.

#### `@requires`

The tag sits in the header block next to `@engine`. Several keys can be combined: `@requires params=4 messages=512`. An unknown key or a malformed value refuses the script.

**`params=N`** declares that the script needs at least `N` panel params, for example `params=4` for a script that reads params 3 and 4. A module with fewer params (MIDI-µKIT has 2) refuses to load the script and logs "Script not loaded: it requires 4 params, this module has 2", instead of failing later when the script touches a param that isn't there. The **Examples** menus grey out such scripts on a variant that can't run them and show "needs N params" next to the name.

A script can adapt instead of refusing, by checking `param.count` or by giving `param.getValue` a fallback (see [Module variants](#module-variants)).

**`messages=N`** asks for a message store of at least `N` handles (see [`midi.*`](#midi--message-constructioninspection)).

- The default is 32 and the maximum 512. `N` is a minimum: `messages=16` keeps 32, `messages=64` gives 64.
- A value above 512 refuses the script ("Script not loaded: @requires messages=5000 exceeds the maximum of 512").
- The store is sized once, before the top-level code runs, and every load sets it again, so a following script without the tag gets 32.
- A larger store lets a callback hold more distinct messages *at once*. It does not raise how much a callback can *send*: everything goes through the output queue (2048 messages, handed on 128 at a time, one batch every 8 samples). Reusing one handle does as well for most scripts.

### Script structure

A script is a single text file with two kinds of code:

- **Top-level code** runs once, synchronously, when the script (re)loads. Use it to set up `config`, define helpers, register context-menu items and enable inputs and ports (`param.enable()`, `trig.enableIn()`, `midi.enableNrpnIn()`, ...).
- **Callbacks** run in response to events. There is no per-sample or per-frame callback: logic only runs when something happens, namely a MIDI message arrives, a trigger fires, a Tipsy message decodes, a broadcast arrives, or the script loads or unloads.

| Callback | Runs ... | Needs |
| --- | --- | --- |
| `midi.onMessage(midiPort, msg)` | on every incoming MIDI message | nothing |
| `midi.onNrpn(midiPort, msg)`, `midi.onRpn(...)`, `midi.onCc14bit(...)` | on every completed NRPN, RPN or 14-bit CC parameter change | `midi.enableNrpnIn()`, `enableRpnIn()`, `enableCc14bitIn()` |
| `trig.onTrigger(trigPort, channel)` | on every rising edge of an *enabled* trigger channel | `trig.enableIn()` |
| `trig.onTipsyMessage(data, mimeType)` | on every complete [Tipsy](#tipsy) message decoded from trigger input 1 | `trig.enableTipsyIn()` |
| `rack.onBroadcast(value, topic)` | when another MIDI-KIT module broadcasts, see [Messages between modules](#messages-between-modules) | nothing |
| `rack.onLoad()`, `rack.onUnload()` | on script load and teardown, see [Persistence](#persistence) | nothing |
| `input.onTooltip(i)`, `param.onTooltip(i)`, `param.onValueText(i)` | when a panel tooltip is shown. Looked up live, so they may be reassigned at runtime | nothing |

Rules for callbacks:

- Assign each hook once, at the top level, as a plain field on its object: `midi.onMessage = function(midiPort, msg) {...}` in JS, `function midi.onMessage(midiPort, msg) ... end` in Lua. See [Hooks and predefined objects are resolved once, at load time](#hooks-and-predefined-objects-are-resolved-once-at-load-time) for why.
- A script without `midi.onMessage` loads but ignores all MIDI (logged once at load). No other hook warns when missing.
- `trig.onTrigger` needs `trig.enableIn(trigPort, [channel])`. Until then that port and channel is not processed at all: no ticks counted, no `sendAfterTrigger` messages drained, no callback.
- **The return value of `midi.onMessage` is ignored and reserved.** Nothing is dropped, consumed or forwarded because of it. A future version may give it a meaning (for example "consumed"), so don't return something by accident, such as `return midiOut.send(msg)` or an implicit return from a helper. End the callback with a bare `return` or no `return`. Messages are passed on only through the `midiOut.send*` calls.

## Part 2 — Examples

**Conventions in the examples**

- Channels are 1..16. Parameter and input indices are 1..4 (1..2 on MIDI-µKIT, see [Module variants](#module-variants)). Trigger input and output indices are 1..2.
- The main entry point is `midi.onMessage(midiPort, msg)`, where `midiPort` is the 1-based MIDI input the message arrived on.
- Only MIDI input and output 1 are enabled by default, see [Enabling MIDI ports](#enabling-midi-ports).

The examples build up from simplest to most involved:

1. basic pass-through and filtering
2. message construction (NRPN, 14-bit CC, SysEx, raw)
3. lifecycle, trigger and Tipsy
4. UI (context menu), assembled input and messages between modules

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
The script handles MIDI messages like the previous example, but MIDI-KIT provides additional programming interface for user interface configuration: `param.onTooltip` configures the text "MIDI Channel" for the tooltip of the first panel parameter, the display value is scaled to the integer range 1..16 by `param.onValueText`.

JavaScript:
```js
param.enable(1);

param.onTooltip = function(port) {
    if (port === 1) return "MIDI Channel";
    return "";
};

param.onValueText = function(port) {
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

param.onTooltip = function(port)
    if port == 1 then return "MIDI Channel" end
    return ""
end

param.onValueText = function(port)
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
      midi.setCc14bit(cc14, 1, 1, 12864);  // 100 * 128 + 64: CC 1 = 100 (MSB), CC 33 = 64 (LSB)
      midiOut.send(cc14);
   }
};
```

Lua:
```lua
midi.onMessage = function(midiPort, msg)
   if midi.isNoteOn(msg) then
      local cc14 = midi.createCc14bit()
      midi.setCc14bit(cc14, 1, 1, 12864)   -- 100 * 128 + 64: CC 1 = 100 (MSB), CC 33 = 64 (LSB)
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

`rack.onUnload()` runs right before the script's state is torn down — the script is being replaced, the module is reset, or the module is removed from the patch. It's the only reliable place to clean up notes a script left sounding, since nothing runs afterward to release them. It never runs on a plain patch save — a save is not a lifecycle event at all (see [Persistence](#persistence): a save just writes out whatever `rack.setConfig()` last published). Note the JavaScript version assigns it to the `rack` object — `rack.onUnload = function() {...}` — like the other hooks (see [Hooks and predefined objects are resolved once, at load time](#hooks-and-predefined-objects-are-resolved-once-at-load-time)).

When the script is replaced or the module is reset, `rack.onUnload()` may only send MIDI right away with `midiOut.send()`; that output always goes out. Everything else it does that would outlive the script is ignored: messages scheduled for later (`sendAfterMs`, `sendAtFrame`, `sendAfterTrigger`), `midiOut.cancel()`, trigger output writes and `trig.sendTipsy()`. Whatever the script scheduled earlier and is still waiting is dropped with it, and the trigger outputs go back to 0 V.

The example sends CC 123 (All Notes Off) on all 16 channels with one reused handle, instead of one note-off per note. With `midiOut.enableTiming()` a note-on sent just before may still be waiting in Rack's output queue; the module holds what `rack.onUnload()` sends behind it, so the all-notes-off can't overtake a note-on and leave a note stuck (see [Enabling sample-accurate timing](#enabling-sample-accurate-timing)).

JavaScript:
```js
midi.onMessage = function(midiPort, msg) {
   if (midi.isNoteOn(msg)) {
      midiOut.send(msg);
   }
};

rack.onUnload = function() {
   let off = midi.create();
   for (let ch = 1; ch <= 16; ch++) {
      midi.setCc(off, ch, 123, 0);
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
   local off = midi.create()
   for ch = 1, 16 do
      midi.setCc(off, ch, 123, 0)
      midiOut.send(off)
   end
end
```

### Send a MIDI clock message on each trigger

`trig.onTrigger(trigPort, channel)` is the entry point for logic driven by the CV trigger inputs rather than by incoming MIDI — for example, forwarding an external clock as MIDI clock messages. `trigPort` (1 or 2) is the trigger input that fired and `channel` (1-based) is its polyphonic channel. **The callback is not used until `trig.enableIn(trigPort, [channel])` is called** — here `trig.enableIn(1)` clocks it from channel 1 of trigger input 1.

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

### Multiply a clock into MIDI clock, sample-accurately

A Rack clock usually ticks once per beat, MIDI clock needs 24 pulses per beat. Hardware that syncs to MIDI clock exposes any timing jitter at once, so this is a case for [sample-accurate timing](#enabling-sample-accurate-timing): `midiOut.enableTiming()` makes every pulse leave on its own frame. The pulse for the input tick goes out on the tick's frame, the other 23 are spread over the next period with `midiOut.sendAtFrame()`, using the previous period as the prediction (as every clock multiplier has to). `rack.getEventFrame()` is the frame of the tick being handled.

JavaScript:
```js
let lastEdge = -1;
let period = 0;

rack.onLoad = function() {
   midiOut.enableTiming();
   trig.enableIn(1);
};

function pulse() {
   let clock = midi.create();
   midi.setRaw(clock, "f8");
   return clock;
}

trig.onTrigger = function(trigPort, channel) {
   let edge = rack.getEventFrame();
   if (lastEdge >= 0) period = edge - lastEdge;
   lastEdge = edge;

   midiOut.send(pulse());   // on the frame of the tick
   if (period > 0) {
      for (let k = 1; k < 24; k++) {
         midiOut.sendAtFrame(pulse(), edge + Math.round(k * period / 24));
      }
   }
};
```

Lua:
```lua
local lastEdge = -1
local period = 0

rack.onLoad = function()
   midiOut.enableTiming()
   trig.enableIn(1)
end

local function pulse()
   local clock = midi.create()
   midi.setRaw(clock, "f8")
   return clock
end

trig.onTrigger = function(trigPort, channel)
   local edge = rack.getEventFrame()
   if lastEdge >= 0 then period = edge - lastEdge end
   lastEdge = edge

   midiOut.send(pulse())   -- on the frame of the tick
   if period > 0 then
      for k = 1, 23 do
         midiOut.sendAtFrame(pulse(), edge + math.floor(k * period / 24 + 0.5))
      end
   end
end
```

The shipped `Clock multiplier` preset adds a multiplier menu (for clocks that tick on 8th or 16th notes) and treats a long gap, when the clock was stopped, as a restart instead of stretching the pulses over the gap.

### Tipsy protocol — send and receive over CV

Tipsy is a protocol for exchanging arbitrary data between modules as a stream
of CV voltages on the trigger input/output. Sending encodes a payload as
voltages on trigger output 1 (`trig.sendTipsy`); receiving routes
trigger input 1 into MIDI-KIT's Tipsy decoder (`trig.enableTipsyIn`) and
delivers each complete message to `trig.onTipsyMessage`. The reference for
all three functions is [Tipsy under `trig.*`](#tipsy).

**Sending — `trig.sendTipsy`**

`trig.sendTipsy(data, [mimeType = "text/plain"])` encodes binary `data` using the Tipsy protocol and outputs it as CV voltages on trigger output 1. This is useful for communicating with modules that understand the Tipsy protocol, such as Transit for preset snapshots.

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
   
   -- Or send JSON data with an explicit mime type
   local config = json.encode({ channel = 1, mode = "auto" })
   trig.sendTipsy(config, "application/json")
end
```

**Note:** The Tipsy-encoded data is output sequentially as CV voltages on trigger output 1, one voltage per sample. The receiving module must understand the Tipsy protocol to decode the data correctly. When no Tipsy message is being sent, the trigger output is driven by the script's `trig.*` functions; a `trig.sendTipsy` call temporarily takes over trigger output 1 while its encoded stream is transmitted.

**Receiving — `trig.enableTipsyIn` / `trig.onTipsyMessage`**

`trig.enableTipsyIn()` routes **trigger input 1** into MIDI-KIT's Tipsy decoder. Every complete message that arrives is delivered to `trig.onTipsyMessage(data, mimeType)`. Pass `false` to release the trigger input again. Tipsy is only supported on the first trigger input and output, so — like `trig.sendTipsy()` — there is no port argument.

Note that while trigger input 1 is claimed for Tipsy, its channel 1 no longer behaves as a trigger — `trig.onTrigger` doesn't fire and `trig.getTicks()` doesn't advance there, since the encoded voltages swing across the trigger threshold constantly and would otherwise fire on nearly every sample. Other channels and trigger input 2 are unaffected.

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

   if mimeType == "application/json" then
      local ok, config = pcall(json.decode, data)
      if ok then
         -- ... use config
      end
   end
end
```

**Note:** While trigger input 1 is claimed for Tipsy, its channel 1 no longer behaves as a trigger — `trig.onTrigger` doesn't fire and `trig.getTicks()` doesn't advance there, and `trig.isHigh()`/`trig.isLow()` on channel 1 read `0` (other channels and trigger input 2 are unaffected). Releasing it with `trig.enableTipsyIn(false)` restores normal trigger behavior. Payloads are capped at 256 bytes, and `data` may contain arbitrary bytes including NULs. A malformed or interrupted stream is reported once in the module log and the decoder resynchronizes automatically on the next message.

### Add items to the module's context menu

`rack.registerContextMenu()` adds items to the module's right-click context menu — a boolean toggle (a menu line with a checkmark), an options submenu (one entry per option, checkmark on the current selection), an action (a plain entry that calls your function) or a file entry (opens a file dialog and hands the file's text to your function). Items appear in registration order and can be used to change `config` values live instead of editing the script. To persist a change (so it survives a patch save/reload), call `rack.setConfig()` in the item's `onChange` — see [Persistence](#persistence).

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
   midi.setCc14bit(cc14, config.ccChannel, ccNumber, nrpnValue);
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
   midi.setCc14bit(cc14, config.ccChannel, ccNumber, nrpnValue)
   midiOut.send(cc14)
end
```

**Note:** The shipped preset `NRPN to CC (assembled)` is this script with a context-menu channel selector added — see [Assembled extended input](#assembled-extended-input-nrpn--rpn--14-bit-cc) for the full rules.


### Broadcast a transport to other modules

`rack.sendBroadcast(value)` hands a value to every other MIDI-KIT module whose script defines `rack.onBroadcast`, with no cable. Here one module turns the clock on its trigger input 1 into broadcasts, and sends start and stop from a "Running" item in its context menu. Another module turns them into MIDI clock, start and stop. Every broadcast has a topic, so a receiver can ignore what it does not know. See [Messages between modules](#messages-between-modules) for the rules, and the `Transport broadcaster` and `Transport follower` presets for the full scripts.

Broadcaster, JavaScript:
```js
let running = false;

function setRunning(value) {
   running = value;
   let state = running ? "start" : "stop";
   let n = rack.sendBroadcast({ state: state }, "transport");
   rack.log("Transport " + state + " sent to " + number.toString(n) + " module(s)");
}

rack.onLoad = function() {
   trig.enableIn(1);
   rack.registerContextMenu({
      type: "boolean",
      label: "Running",
      onGetValue: function() { return running; },
      onChange: function(checked) { setRunning(checked); }
   });
};
rack.onUnload = function() {
   if (running) setRunning(false);
};
trig.onTrigger = function(trigPort, channel) {
   rack.sendBroadcast({}, "clock");
};
```

Broadcaster, Lua:
```lua
local running = false

local function setRunning(value)
   running = value
   local state = running and "start" or "stop"
   local n = rack.sendBroadcast({ state = state }, "transport")
   rack.log("Transport " .. state .. " sent to " .. number.toString(n) .. " module(s)")
end

function rack.onLoad()
   trig.enableIn(1)
   rack.registerContextMenu({
      type = "boolean",
      label = "Running",
      onGetValue = function() return running end,
      onChange = function(checked) setRunning(checked) end
   })
end
function rack.onUnload()
   if running then setRunning(false) end
end
function trig.onTrigger(trigPort, channel)
   rack.sendBroadcast({}, "clock")
end
```

Follower, JavaScript:
```js
function sendRaw(hex) {
   let msg = midi.create();
   midi.setRaw(msg, hex);
   midiOut.send(msg);
}

rack.onBroadcast = function(msg, topic) {
   if (topic === "clock") sendRaw("f8");
   else if (topic === "transport") {
      if (msg.state === "start") {
         rack.log("Transport start");
         sendRaw("fa");
      }
      else if (msg.state === "stop") {
         rack.log("Transport stop");
         sendRaw("fc");
      }
   }
};
```

Follower, Lua:
```lua
local function sendRaw(hex)
    local msg = midi.create()
    midi.setRaw(msg, hex)
    midiOut.send(msg)
end

function rack.onBroadcast(msg, topic)
    if topic == "clock" then sendRaw("f8")
    elseif topic == "transport" then
        if msg.state == "start" then
            rack.log("Transport start")
            sendRaw("fa")
        elseif msg.state == "stop" then
            rack.log("Transport stop")
            sendRaw("fc")
        end
    end
end
```

## Part 3 — API reference

### Module variants

MIDI-µKIT is the compact variant of the module with **2** CV inputs and **2** knobs instead of 4; the trigger ports and the four MIDI inputs and outputs are the same. Scripts run unchanged on both, but a script that uses param or input indices 3 and 4 has to adapt. Read the read-only counts `param.count`, `input.count`, `trig.inCount`, `trig.outCount`, `midi.portCount` and `midiOut.portCount`; they are set when the script loads:

```js
for (let i = 1; i <= param.count && i <= 4; i++) param.enable(i);
let length = param.getValue(3, 0.5);   // 0.5 on µKIT, where param 3 doesn't exist
```

```lua
for i = 1, math.min(param.count, 4) do param.enable(i) end
local length = param.getValue(3, 0.5)   -- 0.5 on µKIT, where param 3 doesn't exist
```

### Hooks and predefined objects are resolved once, at load time

Hooks are read from their object **exactly once**, right after the script's top-level code has run:

| Object | Hooks read once |
| --- | --- |
| `midi` | `onMessage`, `onNrpn`, `onRpn`, `onCc14bit` |
| `rack` | `onLoad`, `onUnload`, `onBroadcast` |
| `trig` | `onTrigger`, `onTipsyMessage` |

**Reassigning a hook afterwards, from a callback or anywhere else, has no effect.** The function present at load time keeps running for the script's lifetime. Defining a hook *late* does not work either: a script that assigns `midi.onMessage` from inside `trig.onTrigger` never has it called, and the "no `midi.onMessage` defined" warning logged at load is the last word on it.

Write every hook once, at the top level, during the initial load. Every shipped preset does, and it is the only supported pattern. This is a deliberate, permanent choice: not looking hooks up by name on every MIDI message or trigger tick is what keeps dispatch fast.

**What is live instead**

- `trig.enableIn()`, `param.enable()` and the other `enable*` calls are ordinary API calls. Calling them at any time takes effect for later events.
- `rack.getConfig()` and `rack.setConfig()` are live too, so they can be called from any callback, any number of times (see [Persistence](#persistence)).
- The tooltip functions `input.onTooltip`, `param.onTooltip` and `param.onValueText` are looked up each time a tooltip is shown. A script can reassign them at runtime, for example from `midi.onMessage`, to change a tooltip as its state changes. The cost is one lookup per tooltip, which is negligible.

**Do not clobber the predefined globals.** Reassigning `rack`, `midi`, `midiOut`, `trig`, `input`, `param` or `number` (for example `rack = 42`) is unsupported. Neither engine crashes. Expect the assignment to be ignored (for a hook) or a logged script error on the next statement that uses the clobbered value. The exact behavior is undefined and its wording differs between QuickJs and Lua. Treat it as a bug in the script.

### `rack.*`

| Function | Effect |
| --- | --- |
| `rack.log(value [, value ...])` | write a line to the module's log, see [Log formatting](#log-formatting) |
| `rack.overlay(s1 [, s2 [, s3]])` | show up to 3 lines in the on-panel overlay |
| `rack.getEventFrame()` | the engine frame (sample counter) of the event being handled, `-1` outside an event. See [Event frames](#event-frames) |
| `rack.msToFrames(ms)` | frames in `ms` milliseconds at the current sample rate, rounded to a whole frame (`rack.msToFrames(10)` is 441 at 44.1 kHz). Use it to place messages relative to `rack.getEventFrame()` |
| `rack.framesToMs(frames)` | the inverse: milliseconds in `frames` frames, not rounded. A measured clock period in frames becomes a time, and a BPM |
| `rack.random()` | a random number in [0, 1) from a generator of the module itself. Its seed is stored in the patch, and every script load (also a reload or a patch load) restarts the sequence from it, so the same script produces the same values each time. Another MIDI-KIT module has a different seed |
| `rack.setRandomSeed(seed)` | restarts the `rack.random()` sequence from `seed` at once. Any finite number is accepted (truncated, wrapped into 32 bits); NaN and infinity raise an error. It changes only the running script's generator, not the seed stored in the patch: the next script load starts from the stored seed again, so call it in `rack.onLoad` for a fixed sequence of your own |
| `rack.getConfig(key [, default])` | read a persisted value, or `default` (`undefined`/`nil` if omitted) when `key` is unset. See [Persistence](#persistence) |
| `rack.setConfig(key, value)` | persist `value` under `key`, or remove the key if `value` is `undefined`/`nil`. See [Persistence](#persistence) |
| `rack.sendBroadcast(value [, topic])` | send `value` to the other MIDI-KIT modules, returns how many received it. See [Messages between modules](#messages-between-modules) |
| `rack.registerContextMenu(options)`, `rack.unregisterContextMenu(label)` | add or remove right-click menu items, see [Context menu](#context-menu--rackregistercontextmenu) |

The hooks `rack.onLoad`, `rack.onUnload` and `rack.onBroadcast` are described under [Script structure](#script-structure), [Persistence](#persistence) and [Messages between modules](#messages-between-modules).

#### Log formatting

`rack.log()` concatenates any number of arguments, with no separator, into one line. Each value is formatted as it would be alone:

| Value | Logged as |
| --- | --- |
| string | verbatim, no added quotes |
| number | like `number.toString()`: `rack.log(1 / 3)` prints `0.333333`, and whole numbers print every digit however large (`rack.log(rack.getEventFrame())`) |
| boolean | `true` / `false` |
| `null` / `undefined` (QuickJs), `nil` (Lua) | `null` / `undefined` |
| object, array, table, function | each engine's own stringification |

Scalars format identically in both engines. The log's context menu sets whether lines start with seconds, the engine frame or nothing.

#### Event frames

`rack.getEventFrame()` returns the frame of the event being handled:

| Callback | Frame |
| --- | --- |
| `midi.onMessage` | the arrival frame (of the last message, for assembled NRPN/RPN/14-bit events) |
| `trig.onTrigger` | the frame of the edge |
| `trig.onTipsyMessage` | the frame the message completed on |
| `rack.onBroadcast` | the frame of the sender's event |
| top level, `rack.onLoad`, `rack.onUnload`, context-menu callbacks | `-1` |

Use it with `midiOut.sendAtFrame()`, see [Enabling sample-accurate timing](#enabling-sample-accurate-timing).

#### Context menu — `rack.registerContextMenu`

`rack.registerContextMenu(options)` adds one item to the module's right-click context menu. Items appear in registration order, and any number is allowed. It returns `true`, or throws (the load fails) if `options` is malformed. There are four variants.

*Boolean toggle*, a single menu line with a checkmark:
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
*Options submenu*, one entry per option with a checkmark on the current selection:
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
*Action*, a plain menu line that calls `onChange` on every click, without arguments:
```js
rack.registerContextMenu({
   type: "action",
   label: "Send all notes off",
   onChange: function() {
      // no arguments
   }
});
```
*File*, a menu line that opens the file dialog. If a file is chosen, `onChange` is called with its content as a string and its name (without the folder). Cancelling the dialog calls nothing:
```js
rack.registerContextMenu({
   type: "file",
   label: "Import scale…",
   onChange: function(content, fileName) {
      // content: string, at most 2048 bytes; fileName: e.g. "just.scl"
      rack.log("read " + content.length + " bytes from " + fileName);
   }
});
```
Lua uses an equivalent table: `{ type = "boolean", label = "...", onGetValue = function() return config.emitTrigger end, onChange = function(checked) ... end }`, and likewise `type = "action"` and `type = "file"`.

**Fields**

| Field | Required | Rule |
| --- | --- | --- |
| `type` | yes | `"boolean"`, `"options"`, `"action"` or `"file"` |
| `label` | yes | non-empty string |
| `options` | for `"options"` | non-empty array of strings |
| `onChange` | yes | function |
| `onGetValue` | no | function returning the current value: a boolean, or an index for `"options"`. Defaults to `false` / `0` when absent. Ignored for `"action"` and `"file"`, which have no value |

**Files** (`"file"` items)

- The file is read as it is, so `content` holds the raw bytes, including line breaks as stored (`\r\n` for a file saved on Windows). Binary data is passed on unchanged in Lua; in JavaScript the string is decoded as UTF-8.
- A file larger than **2048 bytes** is refused: the user gets a message, and `onChange` is not called. The same applies to a file that cannot be read.
- The dialog has no file type filter, and no starting folder is chosen.
- If the script is replaced while the dialog is open, the chosen file is dropped.

**`onGetValue`** is evaluated on the worker thread every time the menu is opened, so the checkmark always reflects the script's live state, including config restored by `onLoad()` on a patch reload. It runs while the menu is built and must not send anything.

**`onChange`**

- Runs on the worker thread, when the item is clicked, and may call any other `rack.*` function. An exception inside it is logged as `Context menu callback error: ...` and does not crash anything.
- It can send. Like `rack.onLoad` it is a callback without an event: MIDI built with `midi.create()` and sent with `midiOut.send()` (or any other `midiOut.*` sender) goes out when `onChange` returns, and trigger, voltage and Tipsy outputs work as usual. Timing is "as soon as possible", and `rack.getEventFrame()` is `-1`.
- The checkmark or selection is updated as soon as the item is clicked, before the callback has run, so the menu reflects the change immediately.
- Arguments by type: `"boolean"` gets `(checked)`, `"options"` gets `(selectedIndex, selectedLabel)`, `"action"` gets none and `"file"` gets `(content, fileName)`.

**Changing items at runtime**

- Registering an item whose `label` already exists **replaces** it. It keeps its position, and the new `type`, `options`, `onGetValue` and `onChange` take over. That is how a script changes a menu, for example re-registering "Active input" with a different number of options when a setting changes.
- `rack.unregisterContextMenu(label)` removes the item and returns `true`, or returns `false` if there was none. Registering the label again afterwards adds a new item at the end.
- All items are cleared when the script is reloaded or cleared.

### Persistence

`rack.getConfig(key [, default])` and `rack.setConfig(key, value)` persist a script's settings across a patch save and reload. They are a plain key/value store and not hooks (see [Hooks and predefined objects are resolved once, at load time](#hooks-and-predefined-objects-are-resolved-once-at-load-time)). Call them from anywhere, at any time, as often as you like: top-level code, `rack.onLoad()`, `rack.onUnload()`, `midi.onMessage`, a context-menu `onChange`.

#### Reading and writing

| Call | Effect |
| --- | --- |
| `rack.setConfig(key, value)` | store `value` under `key`, overwriting the previous one |
| `rack.setConfig(key, undefined)` (JS), `rack.setConfig(key, nil)` (Lua) | remove `key` |
| `rack.getConfig(key)` | the stored value, or `undefined` (QuickJs) / `nil` (Lua) if `key` was never set |
| `rack.getConfig(key, default)` | the stored value, or `default` if `key` was never set |

There is no separate "save" step. Call `setConfig()` the moment a setting changes, typically in a context-menu `onChange`: a patch save writes out whatever was last set. `setConfig()` only updates engine-owned state, so it is cheap and cannot time out.

Read every setting with a default at the top level or in `rack.onLoad()`. That needs no read-modify-write step and nothing to get wrong, unlike merging over a defaults object:

```js
let config = {
   channel:     rack.getConfig("channel", 1),
   passThrough: rack.getConfig("passThrough", false)
};
```

```lua
local config = {
   channel     = rack.getConfig("channel", 1),
   passThrough = rack.getConfig("passThrough", false)
}
```

`rack.onLoad()` and `rack.onUnload()` still cover the rest of a script's lifecycle, such as initializing runtime state or sending an all-notes-off on teardown. `getConfig` and `setConfig` only handle persistence.

#### Keys and values

| | Rule |
| --- | --- |
| **Key** | starts with a letter or underscore, then letters, digits and underscores, up to 64 characters (`channel`, `_scale`, `noteLength2`). Anything else, including a dot, is rejected. `.` is reserved |
| **Value** | boolean, number, string, array or plain object, nested up to **4 levels** |
| **Size** | the whole config is capped at **64 KB** once serialized |

An invalid key, a value that is not JSON-serializable (a function), a cyclic or too deeply nested value, or one that would push the config past the size cap is **rejected**. The config keeps its previous contents and one line is written to the module log.

#### Example

A context-menu setting persisted the moment it changes:

```js
let config = {
   channel:     rack.getConfig("channel", 1),
   passThrough: rack.getConfig("passThrough", false)
};

rack.registerContextMenu({
   type: "boolean",
   label: "Pass through",
   onGetValue: function() { return config.passThrough; },
   onChange: function(checked) {
      config.passThrough = checked;
      rack.setConfig("passThrough", checked);
   }
});
```

```lua
local config = {
   channel     = rack.getConfig("channel", 1),
   passThrough = rack.getConfig("passThrough", false)
}

rack.registerContextMenu({
   type = "boolean",
   label = "Pass through",
   onGetValue = function() return config.passThrough end,
   onChange = function(checked)
      config.passThrough = checked
      rack.setConfig("passThrough", checked)
   end
})
```

Loading a different script over the current one starts it with an empty config. Persisted settings belong to the script that wrote them and are not carried over.

### Messages between modules

`rack.sendBroadcast(value [, topic])` sends a value to the other MIDI-KIT modules in the patch, and `rack.onBroadcast(value, topic)` receives one. No cable is needed. See [Broadcast a transport to other modules](#broadcast-a-transport-to-other-modules) for a worked example.

```js
rack.sendBroadcast({ state: "start" }, "transport");   // returns the receiver count

rack.onBroadcast = function(msg, topic) {
   if (topic === "transport") { /* ... */ }
};
```

```lua
rack.sendBroadcast({ state = "start" }, "transport")  -- returns the receiver count

function rack.onBroadcast(msg, topic)
   if topic == "transport" then ... end
end
```

#### Who receives

- Every *other* MIDI-KIT module whose script defines `rack.onBroadcast`. A script never receives its own broadcast.
- A script without the hook is ignored and does not count in the return value.
- Like every hook, `rack.onBroadcast` is read once at load (see [Hooks and predefined objects are resolved once, at load time](#hooks-and-predefined-objects-are-resolved-once-at-load-time)), so defining it later has no effect. A module stops receiving when its script is replaced, cleared or removed.
- A bypassed module still sends and receives.
- All MIDI-KIT modules that share the module worker are one group. Today that is every MIDI-KIT module in the process.

#### What is sent

| | Rule |
| --- | --- |
| **Value** | anything `rack.setConfig()` accepts: booleans, numbers, strings, arrays and plain objects (tables in Lua), nested up to **4 levels**. A Lua table is an array when its keys are exactly 1..n, and an object otherwise |
| **Size** | at most **4 KB** once serialized as JSON |
| **Topic** | optional string of at most **64 bytes**. The receiver gets it as the second argument, or `undefined` (QuickJs) / `nil` (Lua) if there was none |

The receiver gets its own copy of the value, never a reference to the sender's. A value or topic that is rejected (a function, a cyclic or too deeply nested value, a topic that is not a string, anything too large) logs one line, returns `0` and the script keeps running. Calling `rack.sendBroadcast()` with no value is a script error.

#### Routing

There is no subscription. Every receiver gets every broadcast, whatever its topic, and its script ignores the topics it does not know. A receiver cannot tell which module sent a message. Put an id in the value if you need one.

#### Delivery

- **Asynchronous.** A broadcast is handled on the receiver's next processing pass, a few samples later, not before `rack.sendBroadcast()` returns. Messages from one sender arrive in send order. The order across receivers is not defined.
- **Timing.** A receiver sees the frame of the event that caused the broadcast, so `rack.getEventFrame()` and the `midiOut.send*()` placement in [timing mode](#enabling-sample-accurate-timing) refer to the sender's event. Outside an event (`rack.onLoad`, a context-menu callback) there is none, and `rack.getEventFrame()` is `-1`.
- **Queue.** Each receiver queues up to 16 broadcasts. More than that between two passes are dropped, and "Broadcast input queue full" is logged once per episode.
- **No replay.** A module that loads after a broadcast never sees it. A script that needs to catch up on state must ask for it.

#### Load and unload

- Sending from `rack.onLoad()` announces a newly loaded module.
- A script that defines `rack.onBroadcast` can receive other modules' `rack.onLoad()` broadcasts during the same patch load.
- Sending from `rack.onUnload()` is allowed. Receivers handle the message after the sender is gone.

#### Do not reply unconditionally

If two scripts both send from `rack.onBroadcast`, they keep answering each other forever. Each reply is handled on the next pass, so the modules stay responsive, but it never stops. Reply only to requests, never to replies, for example by using a different topic for each.

### `number.*`
`rescale(x, xMin, xMax, yMin, yMax [, curve])`,
`crossfade(a, b, pos)`, `toString(x)`. Present in both engines identically (Lua re-exposes
these even though `math.*` is also available, for script portability).

### `input.*` (CV inputs on the module, 1-based)
- `input.enable(i)` — activate input `i` so it appears on the panel.
- `input.getVoltage(i [, ch])`, `input.isHigh(i [, ch])`, `input.isLow(i [, ch])`
  (channel defaults to 1; high/low threshold is 0.7V).
- Override `input.onTooltip(i)` to customize the panel label.
- `input.count` — number of CV inputs on this module variant (4, or 2 on MIDI-µKIT).

### `trig.*` (dedicated trigger/gate ports)

Trigger inputs and outputs are numbered 1 and 2, and each input is polyphonic: the optional `ch` argument is a 1-based channel that defaults to 1.

#### Inputs

| Function | Effect |
| --- | --- |
| `trig.enableIn(trigPort [, ch])` | enable a trigger input channel for tick counting and `trig.onTrigger`. See below |
| `trig.onTrigger(trigPort, ch)` | callback on every rising edge of an *enabled* (port, channel). Assigned on the `trig` object and resolved once at load, like the `rack` hooks |
| `trig.getTicks(i [, ch])` | clock tick counter of input `i`. Each port and channel counts on its own |
| `trig.isHigh(i [, ch])`, `trig.isLow(i [, ch])` | current state of input `i` |
| `trig.inCount`, `trig.outCount` | number of trigger inputs and outputs (2 and 2 on both variants) |

**A trigger input does nothing until `trig.enableIn()` is called.** A channel that was never enabled counts no ticks (`trig.getTicks()` stays 0), drains no tick-scheduled (`sendAfterTrigger`) messages and never fires `trig.onTrigger`. Call it once per (port, channel) the script wants to hear. A polyphonic clock is enabled per channel, for example `trig.enableIn(1, 1)` plus `trig.enableIn(1, 2)`.

#### Outputs

| Function | Effect |
| --- | --- |
| `trig.setHigh(i [, ch])`, `trig.setLow(i [, ch])` | set output `i` high or low |
| `trig.setTrigger(i [, ch])` | momentary trigger |
| `trig.setGate(i [, ch], durationMs)` | gate of a given length |

An index beyond the module's two ports is a script error.

Without `midiOut.enableTiming()` an output changes when the script runs on the worker thread, so it jitters by the worker latency (at least one process divider, often an audio block). With `enableTiming()`, a write made while handling an event is stamped with the event's frame plus one audio block and applied on exactly that frame, the same offset as the MIDI sent for the same event, so the two stay together. See [Enabling sample-accurate timing](#enabling-sample-accurate-timing).

#### Tipsy

[Tipsy](https://github.com/baconpaul/tipsy-encoder) streams arbitrary data between modules as CV voltages. Sending uses trigger output 1, and receiving uses trigger input 1. Tipsy is only supported on the first input and output, so there is no port argument. A worked example is in [Tipsy protocol — send and receive over CV](#tipsy-protocol--send-and-receive-over-cv).

| Function | Effect |
| --- | --- |
| `trig.sendTipsy(data [, mimeType])` | encode `data` (a string) and stream it out trigger output 1 |
| `trig.enableTipsyIn([enabled])` | decode an incoming stream from trigger input 1 into `trig.onTipsyMessage`. `false` releases the input |
| `trig.onTipsyMessage(data, mimeType)` | callback for each complete message, resolved once at load |

**`trig.sendTipsy`**

- `mimeType` is a string and defaults to `"text/plain"`. The payload is capped at 256 bytes.
- It sends one voltage per sample until the message is complete, and temporarily takes over trigger output 1. The stream is meant for modules that understand Tipsy, such as [TRANSIT](../../transit/Transit.md).
- Unlike the `midiOut.*` senders it sends no MIDI: it ignores `midiOut.selectPort()` and uses no message-handle slot.

```js
trig.enableIn(1);
trig.onTrigger = function(trigPort, channel) {
   trig.sendTipsy("Hello Tipsy!");                              // mime defaults to "text/plain"
   trig.sendTipsy('{"label":"My snapshot","value":42}', "application/json");
};
```

**`trig.enableTipsyIn`**

While trigger input 1 is claimed it stops being a trigger on channel 1: `trig.onTrigger` doesn't fire, `trig.getTicks()` doesn't advance, and `trig.isHigh()`/`trig.isLow()` read `0`. The encoded voltages are protocol, not a gate, and would otherwise fire `trig.onTrigger` continuously. Other channels and trigger input 2 are unaffected.

```js
rack.onLoad = function() {
   trig.enableTipsyIn();        // decode from the trigger input
};
```

**`trig.onTipsyMessage`** is called once for every complete message. `data` and `mimeType` are strings. `data` may contain arbitrary bytes, including NULs, and is capped at 256 bytes.

### `param.*` (panel knobs)
- `param.enable(i)` — activate param `i`.
- `param.getValue(i [, fallback])` — normalized 0..1 value. If `i` is above `param.count` (e.g. param 3 on MIDI-µKIT) and a `fallback` is given, the fallback is returned instead of raising an error.
- Override `param.onTooltip(i)` and `param.onValueText(i)` for panel display.
- `param.count` — number of panel knobs on this module variant (4, or 2 on MIDI-µKIT). An index above it is a script error: check `param.count` before `param.enable(i)`, and pass a fallback to `param.getValue(i, fallback)`.

### `midi.*` — message construction/inspection

Messages are opaque **handles** into an internal message store. Create one with `midi.create()`, `midi.createNRPN()`, `midi.createRPN()` or `midi.createCc14bit()`. `midi.onMessage` also receives the incoming message as a handle (its `msg` argument).

**Handles**

- Treat a handle as opaque: it is not a small number.
- A handle is valid only inside the callback that got or created it. Using one in a later callback, or outside any callback, is a script error, rather than silently reading whatever message that callback built.
- **Reuse a handle instead of creating one per message.** Sending copies the message, so a handle can be changed and sent again. A loop over a chord, a clock burst or an all-notes-off needs one `midi.create()` and then `midi.setNoteOff(m, ...)` / `midiOut.send(m)` per message. That is the normal way to send many messages.

**The message store**

- It holds **32 live handles per callback** by default. Slot 0 of the incoming-MIDI callbacks is the incoming message. A message received in `midi.onNrpn` or `midi.onRpn` is a group handle and takes 4 slots, one in `midi.onCc14bit` takes 2, so 28 or 30 of the default 32 are left for the script.
- A script that really needs more *distinct* messages at once asks for them with `@requires messages=N` in its header, up to 512.
- When the store is full, `midi.create()`, `midi.clone()`, `midi.createNRPN()`, `midi.createRPN()` and `midi.createCc14bit()` raise a script error that aborts the rest of the callback: "midi.create: message store full (32 handles; reuse a handle or raise it with @requires messages=N)".
- Messages sent before the error have already gone out, so a multi-message sequence (an NRPN pair, a wide chord release) can be emitted partially. A message created but never sent is dropped.

#### Entry points

- `midi.onMessage(midiPort, msg)` — the incoming-MIDI entry point (see
  [Script structure](#script-structure)): called with each incoming message
  that nothing else claimed (see
  [Assembled extended input](#assembled-extended-input-nrpn--rpn--14-bit-cc)
  for the callbacks that receive assembled parameter changes instead). Its
  return value is ignored and reserved, see
  [Script structure](#script-structure).
- `midi.onNrpn(midiPort, msg)` / `midi.onRpn(midiPort, msg)` /
  `midi.onCc14bit(midiPort, msg)` — called with an assembled NRPN/RPN
  parameter change or 14-bit controller change, a group handle like the ones
  `midi.createNRPN()` and the other constructors return (see
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
  Cloning an NRPN, RPN or 14-bit CC handle clones the whole group (it takes
  as many store slots as the group has messages), and the clone is a group
  handle again. For a plain message only the MIDI payload is copied: a received
  plain message has no decode result to carry.
- `midi.createNRPN()` → 4 chained handles (param LSB/MSB + value LSB/MSB), the
  same group handle `midi.onNrpn` receives, set with `midi.setNRPN` (and `midi.setChannel`); other setters raise an error.
- `midi.createRPN()` → the same 4-handle chain for a *registered* parameter
  (CC 101/100 select it), set with `midi.setRPN` (and `midi.setChannel`); other
  setters raise an error. Sending RPN 0 sets a synth's pitch-bend range.
- `midi.createCc14bit()` → 2 chained handles (value MSB at CC `cc`, value LSB
  at CC `cc + 32`), set with `midi.setCc14bit` (and `midi.setChannel`); other
  setters raise an error. The pair is sent atomically — a receiver never sees
  the MSB without its LSB.

#### Getters

| Function | Returns |
| --- | --- |
| `getChannel(msg)` | 1-based channel; `-1` for realtime/SysEx messages (clock, start/stop/continue, SysEx framing), which have no channel |
| `getChanPressure(msg)` | channel-pressure value |
| `getControl(msg)` | see [Assembled extended input](#assembled-extended-input-nrpn--rpn--14-bit-cc) for the type-aware behavior on group handles |
| `getNote(msg)` | note number (or, on a plain CC, the controller number — the older spelling of `getControl`). On a group handle: the lead message's controller (CC 99, CC 101 for an RPN, or the MSB controller of a 14-bit CC) |
| `getValue(msg)` | type-aware: raw 7-bit data byte, or the combined 14-bit value (0-16383) on an NRPN/RPN/14-bit CC group handle. `setCc14bit`, `setNRPN` and `setValue` take the same 0-16383 |
| `getLength(msg)` | size of the message in bytes (a SysEx message counts its `f0`/`f7` framing; compare `getSysExLength`) |
| `getPitchWheel(msg)` | pitch-wheel value, 0-16383 (centre 8192) |
| `getProgramChange(msg)` | program number |
| `getSysEx(msg)` | hex string, payload only — without the `f0`/`f7` framing |
| `getSysExLength(msg)` | payload length in bytes, framing excluded — check before reading with `getSysEx` |
| `getRaw(msg)` | hex string of the message's raw bytes, exactly as sent/received — no framing added or removed |

#### Type predicates

`isCc`, `isNoteOn`, `isNoteOff`, `isKeyPressure`, `isChanPressure`,
`isProgramChange`, `isPitchWheel`, `isSysEx`, `isClock`, `isStart`,
`isContinue`, `isStop` — all `is*(msg)`. Plus `isNrpn`, `isRpn`, `isCc14bit`,
true for a received NRPN, RPN or 14-bit CC (see
[Assembled extended input](#assembled-extended-input-nrpn--rpn--14-bit-cc)) and
for handles from `midi.createNRPN()`, `midi.createRPN()` and
`midi.createCc14bit()`, from the moment they are created.

#### Setters

Setter arguments are never wrapped. A number is rounded to the nearest integer
and clamped to the field's range: channels to 1-16, 7-bit fields (`cc`, `note`,
`value`, `vel`, `program`, pressure) to 0-127, the `cc` of `setCc14bit` to 0-31 (its LSB is `cc + 32`), and 14-bit fields (`setNRPN`
number/value, `setPitchWheel` value) to 0-16383. So `setNote(msg, 132)` gives
note 127, not note 4, and `setNote(msg, 60.5)` gives note 61 in both Lua and
JavaScript. `NaN` clamps to the lower bound.

| Function | Notes |
| --- | --- |
| `setCc(msg, ch, cc, value)` | |
| `setCc14bit(msgMsb, msgLsb, ch, cc, value)` | fills two independent handles, sent as two separate messages with no atomicity |
| `setCc14bit(cc14, ch, cc, value)` | `cc14` is the first handle of a `midi.createCc14bit()` pair; both CCs sent atomically as a unit |
| `setChannel(msg, ch)` | on an NRPN, RPN or 14-bit CC handle: every message of the group |
| `setChanPressure(msg, ch, value)` | 2-byte message; read back with `getChanPressure`, not `getValue` |
| `setKeyPressure(msg, ch, note, vel)` | |
| `setNote(msg, note)` | |
| `setNoteOn(msg, ch, note, vel)` | |
| `setNoteOff(msg, ch, note [, vel])` | release velocity defaults to 0; read back with `getValue` |
| `setNRPN(nrpnHandle, ch, number, value)` | `number`/`value` are 14-bit, 0-16383 |
| `setRPN(rpnHandle, ch, number, value)` | like `setNRPN` but for a handle from `midi.createRPN()`; e.g. `setRPN(h, 1, 0, 12 << 7)` sets a 12-semitone bend range (RPN 0: MSB = semitones, LSB = cents) |
| `setPitchWheel(msg, ch, value)` | `value` is 14-bit, 0-16383; 8192 is the centre (no bend) |
| `setProgramChange(msg, ch, program)` | |
| `setSysEx(msg, hexString)` | payload only — `f0`/`f7` framing added automatically, so pass e.g. `"43104c0000"` rather than `"f043104c0000f7"`; capped at 256 bytes, every byte must be 7-bit (`00`-`7f`) |
| `setRaw(msg, hexString)` | writes the exact bytes with no framing added, e.g. `"f11a"` for an MTC quarter-frame — use for message types with no dedicated setter |
| `setValue(msg, value)` | on an NRPN, RPN or 14-bit CC handle whose setter has run: the combined 14-bit value, 0-16383, keeping its channel and number (the mirror of `getValue`). On such a handle that has not been set yet it raises an error |

**Group handles.** A handle from `midi.createNRPN()`, `midi.createRPN()` or `midi.createCc14bit()`, and the `msg` of `midi.onNrpn`, `midi.onRpn` and `midi.onCc14bit`, stands for a whole group of messages. It answers `midi.isNrpn()`, `isRpn()` or `isCc14bit()` from the moment it is created. After its setter has run (`setNRPN`, `setRPN`, `setCc14bit`), `midi.getControl()` returns the parameter number (the MSB controller for a 14-bit CC) and `midi.getValue()` the combined 14-bit value (0-16383, the same range `setCc14bit` and `setValue` take); before that both return -1. Besides its own setter, `setChannel` sets the channel of every message in the group and `setValue` sets the combined value. Any other setter writes just one message of the group and would leave a broken group on the wire, so it raises a script error and leaves the handle unchanged, for example `midi.setNote: message is an NRPN; use midi.setNRPN()` (an RPN names `midi.setRPN()`, a 14-bit CC `midi.setCc14bit()`). The same goes for the five-argument `setCc14bit` with a group handle as either message.

Both `setCc14bit` forms take `value` as one 14-bit number, 0-16383 (MSB = `value >> 7`,
LSB = `value & 127`), rounded and clamped like `setNRPN`'s value, so a received 14-bit value
can be passed straight on — see the `NRPN to CC` preset
([JavaScript](../../presets/MidiKit/JavaScript/NRPN%20to%20CC.js),
[Lua](../../presets/MidiKit/Lua/NRPN%20to%20CC.lua)) for the canonical use.

#### Assembled extended input (NRPN / RPN / 14-bit CC)

`midi.onMessage` sees the MIDI stream as it arrives, including the raw CCs that make up an NRPN/RPN parameter change (a select handshake on CC 99/98, data entry on 6/38) or a 14-bit CC pair (MSB/LSB on CC `n`/`n + 32`). To get *parameter changes* instead of raw CCs, enable assembly and the engine does the bookkeeping. It is the mirror image of `midi.setNRPN()` and `midi.setCc14bit()` on the way out.

**Enabling and callbacks**

| Function | Effect |
| --- | --- |
| `midi.enableNrpnIn(midiPort [, channel])` | assemble NRPN (kind 0) parameter changes on `midiPort` into `midi.onNrpn` calls. `channel` is 1-based (default: all) |
| `midi.enableRpnIn(midiPort [, channel])` | same, for RPN (kind 1) into `midi.onRpn` |
| `midi.enableCc14bitIn(midiPort [, cc] [, channel])` | assemble 14-bit CC pairs on `midiPort` into `midi.onCc14bit` calls. `cc` is the MSB controller number 0-31 (its LSB is implicitly `cc + 32`); omit it to enable every 14-bit CC |
| `midi.onNrpn(midiPort, msg)` / `midi.onRpn(midiPort, msg)` / `midi.onCc14bit(midiPort, msg)` | called once per completed, enabled parameter change, with `msg` a group handle (see [Group handles](#setters)) read through the usual accessors |
| `midi.isNrpn(msg)` / `midi.isRpn(msg)` / `midi.isCc14bit(msg)` | true for a received message of that kind, and for a handle created with `midi.createNRPN()`, `createRPN()` or `createCc14bit()`; useful when a handle is passed to a helper or inspected later — redundant inside the matching callback, but makes the handle self-describing |

**Enabling a kind without defining its callback is a mistake.** The message then reaches nothing at all, and its component CCs are withheld from `midi.onMessage` (see Consumption below), so the script sees strictly less MIDI than before.

**Reading a received group**

- `midi.getControl(msg)` — "which controller is this?", for every
  controller-ish message: the controller number of a plain CC (0-127), the
  MSB controller of an assembled 14-bit CC (0-31), the parameter number of an
  assembled NRPN/RPN (0-16383), and `-1` for anything that addresses none
  (notes, pitch bend, clock, ...). **This is the preferred way to read a
  controller number**; on a plain CC `midi.getNote(msg)` returns the same
  byte and still works, but it is the older spelling. A group handle
  carries all three alongside each other: `getControl()` = the parameter,
  `getValue()` = the combined 14-bit value, `getNote()` = the lead message's
  controller: CC 99 (101 for an RPN) or the MSB controller of a 14-bit CC.
  `getChannel()` is the group's channel. A received group reads exactly like
  one built with `midi.createNRPN()` and `midi.setNRPN()`.
- `midi.getValue(msg)` is **type-aware**: on an assembled NRPN/RPN/14-bit CC
  it returns the combined 14-bit value (0-16383); on everything else the raw
  7-bit data byte exactly as before.

**Consumption**

Once a kind is enabled, the CCs it is built from stop reaching `midi.onMessage`. A script that asked for assembled events should not also have to filter the parts they were built from. This mirrors `trig.enableTipsyIn()`, which stops `trig.onTrigger` while the trigger input is claimed. The raw CCs are swallowed, not released: if a device drops a message mid-quad, the consumed components are gone. The rules:

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
- **Sending a received group forwards the whole group**, rebuilt from its
  number and value: `midiOut.send(msg)` in `midi.onNrpn` sends CC 99, 98, 6, 38
  (101, 100, 6, 38 for an RPN, the MSB and LSB for a 14-bit CC), whatever the
  device sent. A data entry that came as CC 38 alone goes out with CC 6 = 0.
  `midi.clone(msg)` clones the whole group, `midi.setChannel(msg, ch)` moves it,
  `midi.setValue(msg, v)` sets the combined value, and `midiOut.cancel(msg)`
  cancels the scheduled group with that number. Any other setter raises the
  group-handle error described under [Setters](#setters).
- **A received group takes store slots** (4 for an NRPN or RPN, 2 for a 14-bit CC),
  see "The message store" under Message handles.

**Changes** for scripts written against the earlier behaviour, where a received
group was a single message, the one that completed it:

- `midiOut.send(msg)` in `midi.onNrpn` / `onRpn` / `onCc14bit` sends the whole
  group, not only the completing CC.
- `midi.getNote(msg)` and `midi.getRaw(msg)` return the lead (for example 99), not
  the completing CC (38).
- `midi.isNrpn()`, `isRpn()` and `isCc14bit()` are also true for created handles, and
  `midi.getControl()` / `getValue()` on a created handle that was set return its
  number and combined value.
- Single-message setters on `msg` (`setNote`, `setCc`, ...) raise an error.

### Enabling MIDI ports

MIDI-KIT has four MIDI inputs and four MIDI outputs, but only input 1 and output 1 are enabled by default. A script that wants another port calls, at top level (like `param.enable()` and `trig.enableIn()`):

```js
midi.enablePorts(3);      // deliver messages from MIDI inputs 1-3 to midi.onMessage
midiOut.enablePorts(2);   // allow sending on MIDI outputs 1-2
```

- `midi.enablePorts(n)` and `midiOut.enablePorts(n)` enable the first `n` ports. `n` is 1..4. `1` does nothing, and anything out of range is an error.
- Until an input is enabled, its messages never reach the script. Until an output is enabled, messages sent to it are dropped (logged once per output).
- Enabled ports are forgotten when the script is reloaded or cleared, or the module is reset, so they always reflect what the loaded script asked for.

### `midiOut.*` — sending

`midiOut.portCount` is the number of MIDI output ports (4). `midi.portCount` is the same for inputs.

#### Choosing the output

| Function | Effect |
| --- | --- |
| `midiOut.enablePorts(count)` | enable MIDI outputs 1..`count`. Output 1 is always enabled. A message sent to any other output is dropped (logged once) until the script enables it. See [Enabling MIDI ports](#enabling-midi-ports) |
| `midiOut.selectPort(midiPort)` | select the output (1-based) that every following `midiOut.*` call sends on, until it is called again. The selection is sticky across callbacks. An out-of-range index is an error |
| `midiOut.enableTiming([reportLate])` | opt in to sample-accurate output. With `true`, messages that arrive too late are logged. See [Enabling sample-accurate timing](#enabling-sample-accurate-timing) |

The sending functions take no port argument: the destination is whatever `midiOut.selectPort()` last selected (port 1 if it was never called).

#### Sending

| Function | Sends |
| --- | --- |
| `midiOut.send(msg)` | immediately. With [`midiOut.enableTiming()`](#enabling-sample-accurate-timing), on the frame of the event being handled |
| `midiOut.send(nrpnHandle)`, `midiOut.send(cc14Handle)` | the first handle of an NRPN quad (4 messages) or a 14-bit CC pair (2 messages) sends the whole group, in order |
| `midiOut.sendAfterMs(msg, ms)` | delayed. The delay counts from the latest frame the module had processed when the script ran, or, with `enableTiming()`, from the frame of the event being handled. `-1` instead of a time means "after Rack's output queue": two audio blocks and a frame |
| `midiOut.sendAtFrame(msg, frame)` | at an absolute engine frame, held until then. A negative frame means "now". Frames come from `rack.getEventFrame()` |
| `midiOut.sendAfterTrigger(msg, ticks [, trigPort [, channel]])` | after `ticks` clock ticks counted from `trigPort` (1-based, default trig input 1) on `channel` (default 1) |
| `midiOut.cancel([msg])` | nothing: withdraws messages scheduled with the three calls above, see [Cancelling scheduled messages](#cancelling-scheduled-messages) |

**Every send call sends the message as it is at that moment.** It copies the message and sends the copy with its own schedule.

- Calling it again sends again, and a later change to the handle doesn't affect what was already sent.
- `sendAfterTrigger(msg, 5)` followed by `send(msg)` sends two messages.
- To send the same bytes on several ports, change `midiOut.selectPort()` between the calls.
- Messages reach the output queue while the callback runs, in call order. A very long callback can have its first messages on the wire before it returns.

#### Queues and limits

| Queue | Capacity | When full |
| --- | --- | --- |
| output | 2048 messages, sent at up to 128 every 8 samples (about 2.7 ms for 2048 at 48 kHz) | the message is dropped and logged |
| `sendAfterMs()` / `sendAtFrame()` | 256 per output | the message is sent at once, logged once per script |
| `sendAfterTrigger()` | 32 per trigger input channel | the message is sent at once, logged once per script |

A delayed message that finds its queue full is sent at once instead of being dropped, because a dropped Note-Off would leave a note hanging. Release a long tail of delayed notes in steps, or keep the number of pending messages below these limits.

#### Cancelling scheduled messages

`midiOut.cancel()` withdraws messages that `midiOut.sendAfterMs()`, `midiOut.sendAtFrame()` or `midiOut.sendAfterTrigger()` are still holding. Like the send calls it works on the output last selected with `midiOut.selectPort()`.

```js
midiOut.cancel();       // everything scheduled on the selected output
midiOut.cancel(msg);    // the scheduled messages with the same address as msg
```

```lua
midiOut.cancel()
midiOut.cancel(msg)
```

With a message, only its *address* is compared, never its value:

| `msg` | A scheduled message matches if it has the same … |
| --- | --- |
| Note-Off, or Note-On with velocity 0 | Note-Off (either form), channel and note |
| Note-On (velocity above 0) | Note-On (velocity above 0), channel and note |
| Poly aftertouch | type, channel and note |
| Control change | type, channel and controller number |
| Program change, channel pressure, pitch bend | type and channel |
| SysEx | any scheduled SysEx |
| Other system messages (clock, start, stop, MTC …) | status byte |
| NRPN or RPN handle | the whole NRPN/RPN with the same channel and parameter number |
| 14-bit CC handle | the whole 14-bit CC with the same channel and MSB controller |

- Note-On and Note-Off are different addresses: cancelling both takes two calls. A Note-On with velocity 0 counts as a Note-Off here, as in the MIDI specification, but `midi.isNoteOff()` does not: it only checks the Note-Off status.
- A message received in `midi.onNrpn`, `midi.onRpn` or `midi.onCc14bit` is a group handle, so it cancels the scheduled group with the same channel and number: `midi.onNrpn = function(port, msg) { midiOut.cancel(msg); ... }`.
- A group is never split. `midiOut.cancel(cc)` with a plain CC 99 leaves a scheduled NRPN whole, and only a handle of the same NRPN removes it. Without an argument, groups are removed whole too.
- It never touches `midiOut.send()`, not even with `midiOut.enableTiming()`, and nothing that has already left.
- A pattern that matches nothing is not an error. A message without a status byte (a fresh `midi.create()`), an NRPN or 14-bit CC handle that was never set, a second argument and an argument that is not a message handle are errors.
- Calls apply in order: `sendAfterMs(a, 10); cancel(); sendAfterMs(b, 10)` sends only `b`.
- **It is asynchronous.** The cancel takes effect when the audio thread drains its queue (every 8 samples), not at the frame of the event: a message that is due before that still goes out.
- Stuck notes are up to the script. Cancelling a Note-Off whose Note-On has already gone out leaves the note hanging.
- In `rack.onUnload()` the call is ignored: the script's scheduled messages are dropped anyway.

Retriggering a note whose scheduled Note-Off is still pending, so that the old release doesn't end the new note early:

```js
let off = midi.create();
midi.setNoteOff(off, 1, 60);
midiOut.cancel(off);               // the old scheduled release
midiOut.send(off);                 // release now, then play the note again
```

```lua
local off = midi.create()
midi.setNoteOff(off, 1, 60)
midiOut.cancel(off)
midiOut.send(off)
```

### Enabling sample-accurate timing

By default MIDI-KIT writes a message to the MIDI output as soon as it has it, from the audio thread, at the first audio block boundary after the script ran. That is the lowest latency, but the moment a message leaves jitters by up to one audio block (5.3 ms at 256 samples and 48 kHz). That is fine for a filter, a merge or a panic button, and audible in a clock, an arpeggiator or a sequencer.

#### Enabling it

A script that needs better calls `midiOut.enableTiming()` once, in `rack.onLoad` or at top level:

```js
rack.onLoad = function() {
   midiOut.enableTiming();
};
```

- Every message then leaves with an *engine frame* (Rack's sample counter), and Rack's MIDI output thread transmits it at exactly that frame, to within about 100 µs, instead of MIDI-KIT writing it at a block boundary.
- A script that does not call it behaves exactly as before.
- It applies to messages sent after the call. There is no way to switch it off again.
- Like the port enables, it is forgotten when the script is reloaded, cleared or the module is reset.

#### Trigger outputs

With `enableTiming()`, `trig.setTrigger`, `setGate`, `setHigh` and `setLow` are stamped too when called inside an event (`midi.onMessage`, `trig.onTrigger`, `trig.onTipsyMessage` and the other event callbacks). The module applies the write on the audio thread at the event's frame plus one audio block, the delay Rack puts on framed MIDI. A "MIDI note to trigger" script therefore produces trigger and note together, without the worker's jitter.

- Outside an event (`rack.onLoad`, context-menu callbacks) a write happens when the script runs, as without timing. In `rack.onUnload` it is ignored.
- A write whose frame has already passed (a slow script) is applied at once.
- Up to 64 stamped writes can be pending. Beyond that a write is applied immediately.

#### What it costs

Rack delays framed output by one audio block (5.3 ms at 256 samples and 48 kHz).

- Every message is delayed by the same amount, so a clock or a sequence keeps its shape.
- A script that answers a MIDI message answers about one block later than without timing.
- Do not use it where the lowest possible latency matters more than a steady rhythm.

#### Which frame a message gets

The calls below differ only in how they choose
the frame. Messages for the same frame leave in the order they were sent; a
message for an earlier frame leaves before one for a later frame, whatever the
order of the calls.

| Call | Without `enableTiming()` | With `enableTiming()` |
| --- | --- | --- |
| `midiOut.send(msg)` | immediately | on the frame of the event being handled: the arrival frame of the MIDI message in `midi.onMessage`, the frame of the edge in `trig.onTrigger`, the frame the last byte arrived on in `trig.onTipsyMessage`. Anywhere else (`rack.onLoad`, context-menu callbacks) as soon as possible, on the current frame. In `rack.onUnload`, behind everything Rack's output queue still holds (two blocks and a frame) |
| `midiOut.sendAfterMs(msg, ms)` | `ms` after the latest frame the module had processed when the script ran | `ms` after the frame of the event being handled (after the latest frame processed, outside an event) |
| `midiOut.sendAtFrame(msg, frame)` | held until `frame`, then sent immediately | on `frame` |
| `midiOut.sendAfterTrigger(msg, ticks, ...)` | when the tick is reached, immediately | on the frame of the trigger edge that reaches the tick |

`rack.getEventFrame()` returns the frame of the event being handled, so a script
can place messages relative to it with `midiOut.sendAtFrame()` — for example a
clock multiplier measures the distance between two trigger edges and spreads
pulses over it (see
[Multiply a clock into MIDI clock](#multiply-a-clock-into-midi-clock-sample-accurately)). A frame is a
sample count: one second is as many frames as the sample rate.

#### Converting between milliseconds and frames

Frames depend on the sample rate
(441 frames are 10 ms at 44.1 kHz, 480 at 48 kHz), so a script should not hard-code
them. Two functions convert at the sample rate the module currently runs at:

- `rack.msToFrames(ms)` returns a whole number of frames for `ms` milliseconds
  (rounded to the nearest frame; negative values stay negative).
- `rack.framesToMs(frames)` returns the milliseconds for `frames` frames, as a
  float (not rounded).

Neither needs an event or `midiOut.enableTiming()`. They only read the sample rate, so they also work in `rack.onLoad`.

- Use them whenever a script mixes time with frame numbers.
- `midiOut.sendAfterMs(msg, ms)` already does "event plus *N* ms" by itself. Reach for `msToFrames` when the offset is combined with other frame arithmetic, as in the swing example below, and for `framesToMs` when a measured distance has to become a time.

#### Example: show the tempo of a clock

The script measures
the distance between two trigger edges in frames, turns it into a tempo (the
input is a clock with one tick per beat) and treats a gap of more than two
seconds as a stopped clock:

JavaScript:
```js
let lastEdge = -1;

rack.onLoad = function() {
   trig.enableIn(1);
};

trig.onTrigger = function(trigPort, channel) {
   let edge = rack.getEventFrame();
   if (lastEdge >= 0) {
      let ms = rack.framesToMs(edge - lastEdge);
      if (ms > 2000) {
         rack.overlay("Clock restarted");
      } else {
         rack.overlay("Tempo", (60000 / ms).toFixed(1) + " BPM");
      }
   }
   lastEdge = edge;
};
```

Lua:
```lua
local lastEdge = -1

rack.onLoad = function()
   trig.enableIn(1)
end

trig.onTrigger = function(trigPort, channel)
   local edge = rack.getEventFrame()
   if lastEdge >= 0 then
      local ms = rack.framesToMs(edge - lastEdge)
      if ms > 2000 then
         rack.overlay("Clock restarted")
      else
         rack.overlay("Tempo", string.format("%.1f BPM", 60000 / ms))
      end
   end
   lastEdge = edge
end
```

#### Example: swing

In the [clock multiplier](#multiply-a-clock-into-midi-clock-sample-accurately)
above, delay every second pulse by a fixed time. The loop already works in
frames (the period is measured in frames), so the time offset is converted once
and added:

```js
let swing = rack.msToFrames(8);   // 8 ms, whatever the sample rate
for (let k = 1; k < 24; k++) {
   let offset = Math.round(k * period / 24) + (k % 2 == 0 ? swing : 0);
   midiOut.sendAtFrame(pulse(), edge + offset);
}
```

```lua
local swing = rack.msToFrames(8)   -- 8 ms, whatever the sample rate
for k = 1, 23 do
   local offset = math.floor(k * period / 24 + 0.5) + (k % 2 == 0 and swing or 0)
   midiOut.sendAtFrame(pulse(), edge + offset)
end
```

#### Unloading

A note-on sent just before a reload may still be waiting in Rack's
output queue, up to one audio block. A note-off sent at once would overtake it and
leave the note stuck, so the module holds what `rack.onUnload` sends with
`midiOut.send()` for two audio blocks and a frame, whatever the block size, which
puts it behind everything Rack still holds. A script that plays notes just sends
its note-offs and all-notes-off from `rack.onUnload`, as the `Arpeggiator` and
`Euclidean rhythm generator` presets do. When the module is removed the messages
go out at once: Rack's output queue goes away with the device.

#### Finding out when it does not hold

Rack can only place a message that reaches
it in time, one audio block after its frame at the latest. A script that is too
slow, or a busy worker thread that runs all MIDI-KIT scripts in the patch, makes
messages arrive late; Rack then sends them at once, which is the timing you had
without `enableTiming()`, and nothing tells you. `midiOut.enableTiming(true)`
logs such messages, at most one line per second:

```
Timing: message(s) reached the output too late
```

The report is off by default and costs nothing when off. A message that is only
a few samples behind its frame is not late: waiting for the script is normal and
Rack's block of delay absorbs it.

#### Order

Messages sent on the same frame are moved one sample apart, in the
order they were sent, so an NRPN, a 14-bit CC pair or a note-off followed by a
note-on of the same note always arrives in order. Messages that are only a few
samples apart can swap places if Rack hands them to its output thread in
different audio blocks; Rack's own MIDI-CV and CV-MIDI have the same limit. The
order is kept per MIDI output of the module: two outputs, or two modules, sending
to the same device are not ordered against each other.

#### Output devices

Only drivers that honour a message's frame place it; a driver
that ignores it sends the message when MIDI-KIT hands it over, which is the same
as not using timing.

### MIDI status/type reference used internally

| Message | Status |
| --- | --- |
| NoteOff, NoteOn, KeyPressure | `0x8`, `0x9`, `0xa` |
| CC, ProgramChange, ChanPressure, PitchWheel | `0xb`, `0xc`, `0xd`, `0xe` |
| SysEx | `0xf0` ... `0xf7` |
| Clock, Start, Continue, Stop | `0xF8`, `0xFA`, `0xFB`, `0xFC` |

Realtime messages are encoded as status `0xf` with a "channel" nibble of `0x8`, `0xa`, `0xb` and `0xc`. Use the `is*` predicates instead of decoding this by hand.

## Part 4 — Gotchas

**Message handles**

- A handle is valid only within the callback that got or created it. The store resets on every callback.
- Creating a message at top level, or in `param.onTooltip`, `input.onTooltip` or `onGetValue`, is an error (at top level the load fails with the script line). Using a handle from an earlier callback is an error too. Build messages inside the callback that sends them.
- `rack.onLoad()`, `rack.onUnload()`, `trig.onTrigger()` and a context-menu `onChange` are full callbacks in this sense: a message created and sent inside any of them is delivered normally.

**14-bit values and NRPN**

- `midi.setCc14bit` and `midi.setNRPN` split a 14-bit value across two 7-bit CC messages (`cc` = MSB, `cc + 32` = LSB, per the NRPN and 14-bit CC convention).
- For a 14-bit CC pair that must land atomically, use `midi.createCc14bit()` with the 4-argument `setCc14bit`. The two-handle form sends two independent messages.
- Worked examples: the `NRPN to CC` preset ([JS](../../presets/MidiKit/JavaScript/NRPN%20to%20CC.js), [Lua](../../presets/MidiKit/Lua/NRPN%20to%20CC.lua)) and the `NRPN Generator` preset ([JS](../../presets/MidiKit/JavaScript/NRPN%20Generator.js), [Lua](../../presets/MidiKit/Lua/NRPN%20Generator.lua)) for constructing NRPN messages.

**Engines**

- Lua's sandboxed standard library excludes `io`, `os`, `package` and `debug`: no file access and no OS calls, by design.
- A script is only run by the engine its `@engine` tag names. Loading a QuickJs script into a module that expects `@engine minilua@v1` (or the reverse) fails with an explicit "not compatible" log message instead of being silently misinterpreted.
