# MIDI-KIT scripting manual

MIDI-KIT runs a small script that listens to incoming MIDI, triggers and CV, and answers by sending MIDI, triggers and voltages. With a few lines you can filter, route, transform, delay or generate MIDI. You can also build clock multipliers, arpeggiators, NRPN translators and other tools that would otherwise take a dedicated module.

You write scripts in **JavaScript** or **Lua**. Both languages get exactly the same set of functions, so everything in this manual applies to both. Examples are shown in both languages.

This manual assumes you know your way around VCV Rack and have written some code before, in any language. You don't need to be a programmer.

## Contents

1. [Getting started](#getting-started): your first script, the file header, choosing a language, reading errors
2. [How a script runs](#how-a-script-runs): top-level code and callbacks, enabling what you use, the script's lifetime
3. [Cookbook](#cookbook): complete, working examples, from a one-line pass-through to modules talking to each other
4. [MIDI messages](#midi-messages): reading, building and changing messages, NRPN, RPN and 14-bit CC
5. [Sending MIDI](#sending-midi): output ports, delayed and scheduled sending, cancelling, panic
6. [Sample-accurate timing](#sample-accurate-timing): jitter-free output for clocks and sequencers
7. [Knobs, CV and triggers](#knobs-cv-and-triggers): panel parameters, CV inputs, trigger inputs and outputs, Tipsy
8. [Module services](#module-services): log, overlay, context menu, saved settings, messages between modules, random numbers, number helpers
9. [Language notes](#language-notes): what differs between JavaScript and Lua
10. [Troubleshooting](#troubleshooting): the mistakes that cost the most time
11. [Limits](#limits): every size, time and count limit in one table
12. [API index](#api-index): every function, with a link to where it is explained

---

## Getting started

### Your first script

The simplest useful script forwards every incoming MIDI message to the output:

```js
/**
 * @engine QuickJs@v1
 */

midi.onMessage = function(midiPort, msg, msgType) {
   midiOut.send(msg);
};
```

```lua
--[[
@engine minilua@v1
--]]

midi.onMessage = function(midiPort, msg, msgType)
   midiOut.send(msg)
end
```

Two things to notice:

- **Nothing passes through by default.** MIDI-KIT only sends what the script sends. A script that does nothing blocks all MIDI.
- **`midi.onMessage` is a callback.** You don't call it. MIDI-KIT calls it for every message that arrives, and your script decides what to do with it. It receives three arguments: `midiPort`, the MIDI input the message arrived on (1 to 4), `msg`, the message itself, and `msgType`, what kind of message it is (`midi.NOTE_ON`, `midi.CC`, ..., see [Message types](#message-types)).

**Getting a script into the module**

- Type or paste it into the module's script editor.
- Use the **Script** submenu in the module's context menu: **Examples (JavaScript)** and **Examples (Lua)** hold the shipped scripts. **Load**, **Paste from clipboard**, **Reload** and **Save as** work with your own files.
- Drag a `.js` or `.lua` file onto the module.

The shipped examples are plain files in `presets/MidiKit/` and are a good starting point for your own scripts.

**Using MIDI-KIT as an insert effect:** with VCV Rack's built-in **MIDI Loopback** driver, MIDI-KIT can process messages before they reach other MIDI modules such as MIDI-CC, MIDI-CV, MIDI-MAP or MIDI-CAT, or process what those modules send.

### The script header

Every script starts with a comment block of `@key value` tags. The `@engine` tag is required: it tells MIDI-KIT which language the script is written in.

JavaScript:

```javascript
/**
 * @target stoermelder MIDI-KIT
 * @engine QuickJs@v1
 * @author yourname
 * @description One-line summary shown in the module log on load
 */
```

Lua:

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
| `@engine` | yes | `QuickJs@v1` or `minilua@v1`, written exactly like this. Anything else and the script is not loaded |
| `@author`, `@description` | no | shown in the module log when the script loads |
| `@target` | no | a convention for readers, not checked |
| `@requires` | no | what the script needs from the module, see below |

Keep in mind:

- Only the **leading** comment block is scanned for tags. A tag further down in the file is ignored.
- The file extension doesn't matter: the language comes from `@engine` alone. A script whose tag names the other language fails with a clear "not compatible" message in the log.
- The `@v1` suffix pins the script to the current version of the scripting interface. A future incompatible change would become `@v2`, so existing scripts keep working as they do today.

The script editor's context menu can insert both headers for you.

#### `@requires`

`@requires` declares what the script needs. Several keys can share one line, for example `@requires params=4 messages=512`. An unknown key or a malformed value stops the script from loading.

**`params=N`**: the script needs at least `N` panel knobs. MIDI-µKIT has only 2 knobs, so a script with `@requires params=4` refuses to load there and logs "Script not loaded: it requires 4 params, this module has 2". The alternative would be an error later, when the script touches a knob that isn't there. The **Examples** menus grey out such scripts on MIDI-µKIT and show "needs N params" next to the name. A script can also adapt instead of refusing, see [Module variants](#module-variants).

**`messages=N`**: the script needs to hold more than 32 MIDI messages at the same time, see [Message handles](#message-handles).

- The default is 32 and the maximum is 512. `N` is a minimum: `messages=16` keeps 32, `messages=64` gives 64.
- A value above 512 refuses the script ("Script not loaded: @requires messages=5000 exceeds the maximum of 512").
- The setting applies to this script only. A script loaded afterwards without the tag gets 32 again.
- This does not let a script *send* more. Everything sent goes through the same output queue (see [Queues](#queues)). Most scripts are better off reusing one message.

### JavaScript or Lua?

Both languages handle the common tasks equally well. Pick the one you are more comfortable with. If you adapt a shipped example, stay with its language so you can compare the two side by side.

| | JavaScript | Lua |
| --- | --- | --- |
| Engine | [QuickJS](https://bellard.org/quickjs/), full ES2020 | [MiniLua](https://github.com/edubart/minilua), full Lua 5.5 |
| Lists and records | array literals `[1, 2, 3]`, object literals `{a: 1}` | tables only: `{1, 2, 3}`, `{a = 1}`, `#t` for the length |
| Standard library | complete: `Math`, `JSON`, `String`, `Array`, `RegExp`, ... | `math`, `string`, `table`, plus `string.split` and `json` added by MIDI-KIT. No file or OS access |
| Text formatting | `+` joins numbers into strings, `toFixed()` | `..` joins numbers into strings, `string.format` |
| Good for | scripts adapted from JavaScript examples, heavy use of lists and objects | `string.format`, `table.sort`, Lua's pattern matching |

Neither engine is built for raw speed, but MIDI is sparse compared to audio, and both are fast enough for MIDI work. The details of each language are in [Language notes](#language-notes).

### Errors and the log

The module's log shows everything the script writes with `rack.log()`, as well as load messages and errors. An error message names the script line it happened on. In the script editor you can click such a line in the log to jump to the place in the script, and **Ctrl+G** (**Cmd+G** on macOS) goes to any line by number.

- An error in the **top-level code** stops the script from loading.
- An error in a **callback** aborts that one call. The script keeps running, and the next callback runs normally.
- Repeated identical log lines are collapsed into one "… repeated N×" line, so an error on every clock tick doesn't flood the log.

Each log line starts with the seconds since the script was loaded. The log's context menu (**Timestamp**) switches to the engine frame, or to no timestamp at all.

---

## How a script runs

### Top-level code and callbacks

A script contains two kinds of code:

- **Top-level code** runs once, when the script is loaded. Use it to set things up: define variables and helper functions, read saved settings, register context-menu items, enable the inputs and ports the script uses.
- **Callbacks** are functions MIDI-KIT calls when something happens. There is no callback that runs on every sample or at a fixed rate: a script only runs when an event occurs.

| Callback | Called when ... | Needs |
| --- | --- | --- |
| `midi.onMessage(midiPort, msg, msgType)` | a MIDI message arrives. `midiPort` is the MIDI input (1 to 4), `msg` the message, `msgType` its [type](#message-types) | nothing for input 1, see [Enabling MIDI ports](#enabling-midi-ports) for others |
| `midi.onNrpn(midiPort, msg)`, `midi.onRpn(...)`, `midi.onCc14bit(...)` | a complete NRPN, RPN or 14-bit CC change arrives | `midi.enableNrpnIn()`, `enableRpnIn()`, `enableCc14bitIn()`, see [Receiving NRPN, RPN and 14-bit CC](#receiving-nrpn-rpn-and-14-bit-cc) |
| `trig.onTrigger(trigPort, channel)` | a trigger input rises | `trig.enableIn()`, see [Trigger inputs](#trigger-inputs) |
| `trig.onTipsyMessage(data, mimeType)` | a complete Tipsy message arrives on trigger input 1 | `trig.enableTipsyIn()`, see [Tipsy](#tipsy) |
| `rack.onBroadcast(value, topic)` | another MIDI-KIT module sends a broadcast | nothing, see [Messages between modules](#messages-between-modules) |
| `rack.onLoad()` | right after the top-level code, when the script is loaded | nothing, see [The script's lifetime](#the-scripts-lifetime) |
| `rack.onUnload()` | right before the script is replaced, the module is reset or removed | nothing, see [The script's lifetime](#the-scripts-lifetime) |
| `param.onTooltip(i)`, `param.onValueText(i)`, `input.onTooltip(i)` | a panel tooltip is shown | nothing, see [Tooltips](#tooltips-and-value-display) |

`msgType` is the same value `midi.getType(msg)` returns. It is passed in because most scripts branch on it. A script that doesn't need it may leave the parameter out: `function(midiPort, msg)` works just as well.

A script without `midi.onMessage` loads, but ignores all incoming MIDI. This is logged once at load: "No midi.onMessage(midiPort, msg, msgType) function defined — incoming MIDI is ignored". No other callback warns when it is missing.

**Don't return a value from `midi.onMessage`.** The return value is currently ignored and reserved for a future meaning, such as "message consumed". End the callback with a bare `return`, or none at all. Watch out for accidental returns like `return midiOut.send(msg)`. Messages only go out through the `midiOut.send*` functions.

### Assign each callback once, at the top level

MIDI-KIT looks up the following callbacks **exactly once**, right after the top-level code has run:

| Object | Callbacks looked up once |
| --- | --- |
| `midi` | `onMessage`, `onNrpn`, `onRpn`, `onCc14bit` |
| `rack` | `onLoad`, `onUnload`, `onBroadcast` |
| `trig` | `onTrigger`, `onTipsyMessage` |

So:

- Assign each of them once, at the top level, as a field of its object: `midi.onMessage = function(midiPort, msg, msgType) {...}` in JavaScript, `midi.onMessage = function(midiPort, msg, msgType) ... end` or the equivalent `function midi.onMessage(midiPort, msg, msgType) ... end` in Lua.
- **Reassigning one later has no effect.** The function present at load time stays in use for the script's lifetime.
- **Assigning one late doesn't work either.** A script that sets `midi.onMessage` from inside `trig.onTrigger` never receives MIDI.

To change behavior at runtime, keep the callback and switch on a variable inside it. This is a deliberate design choice: looking the function up on every MIDI message or trigger would slow everything down.

**What *can* change at any time:**

- The `enable*` functions (`trig.enableIn()`, `param.enable()`, `midi.enableNrpnIn()`, ...) are ordinary function calls and take effect for the following events.
- `rack.getConfig()` and `rack.setConfig()` can be called from anywhere, any number of times.
- The tooltip functions `input.onTooltip`, `param.onTooltip` and `param.onValueText` are looked up every time a tooltip is shown, so a script may reassign them, for example to change a label when a mode changes.

**Don't overwrite the predefined objects.** Assigning something to `rack`, `midi`, `midiOut`, `trig`, `input`, `param` or `number` (`rack = 42`) is not supported. It won't crash, but expect the assignment to be ignored, or an error on the next line that uses the object.

### Enable what you use

Almost everything is off until the script switches it on. This keeps an unused input from costing time and keeps one script's leftovers from affecting the next.

| To ... | Call | Off by default? |
| --- | --- | --- |
| receive MIDI on inputs 2 to 4 | `midi.enablePorts(n)` | yes, input 1 is always on |
| send MIDI on outputs 2 to 4 | `midiOut.enablePorts(n)` | yes, output 1 is always on |
| show a panel knob | `param.enable(i)` | yes |
| show a CV input | `input.enable(i)` | yes |
| receive triggers | `trig.enableIn(trigPort [, channel])` | yes: **a trigger input does nothing until enabled** |
| receive Tipsy | `trig.enableTipsyIn()` | yes |
| receive whole NRPN, RPN or 14-bit CC changes | `midi.enableNrpnIn()`, `enableRpnIn()`, `enableCc14bitIn()` | yes |
| send with sample-accurate timing | `midiOut.enableTiming()` | yes |

Call these at the top level or in `rack.onLoad()`. Everything that was enabled is forgotten when the script is reloaded or cleared, or the module is reset, so the module always reflects what the loaded script asked for.

### Numbering

Everything a script counts with starts at **1**: MIDI ports, MIDI channels (1 to 16), knobs, CV inputs, trigger ports and polyphonic channels. Only values inside MIDI messages (note numbers, controller numbers, values) use the usual MIDI ranges starting at 0.

### Module variants

MIDI-µKIT is the compact version of MIDI-KIT, with **2** CV inputs and **2** knobs instead of 4. The trigger ports and the four MIDI inputs and outputs are the same. Scripts run unchanged on both. A script that uses knob or input 3 or 4 has three options:

- declare `@requires params=4` and refuse to run on MIDI-µKIT (see [`@requires`](#requires)),
- read the counts below and adapt,
- or give `param.getValue()` a fallback value for the missing knobs:

```js
for (let i = 1; i <= param.count && i <= 4; i++) param.enable(i);
let length = param.getValue(3, 0.5);   // 0.5 on µKIT, where knob 3 doesn't exist
```

```lua
for i = 1, math.min(param.count, 4) do param.enable(i) end
local length = param.getValue(3, 0.5)   -- 0.5 on µKIT, where knob 3 doesn't exist
```

| Count | MIDI-KIT | MIDI-µKIT |
| --- | --- | --- |
| `param.count` | 4 | 2 |
| `input.count` | 4 | 2 |
| `trig.inCount`, `trig.outCount` | 2, 2 | 2, 2 |
| `midi.portCount`, `midiOut.portCount` | 4, 4 | 4, 4 |

The counts are read-only and set when the script loads.

### The script's lifetime

**Loading.** The top-level code runs, then `rack.onLoad()`. After that the callbacks start receiving events. A script is loaded when you load or paste it, choose an example, apply a change in the editor, use **Reload**, and when a patch is opened.

**Unloading.** `rack.onUnload()` runs right before the script's state is thrown away: when another script replaces it, the script is edited or reloaded, the module is reset, or the module is removed from the patch. Saving a patch is *not* an unload. Nothing runs on save, see [Saving settings](#saving-settings).

`rack.onUnload()` is the place to silence what the script left sounding. It is limited to what can still go out after the script is gone:

- MIDI sent right away with `midiOut.send()` (and `midiOut.panic()`) always goes out. It is held back just long enough not to overtake notes that are still on their way, see [Unloading and stuck notes](#unloading-and-stuck-notes).
- Everything that would outlive the script is ignored: scheduled sends (`sendAfterMs`, `sendAtFrame`, `sendAfterTrigger`), `midiOut.cancel()`, trigger output writes and `trig.sendTipsy()`.
- Messages the script scheduled earlier and that are still waiting are dropped, and the trigger outputs go back to 0 V.

The simplest clean-up is `midiOut.panic()`, see [Panic](#panic).

---

## Cookbook

Complete scripts, from simple to involved. They leave out the header block to save space. Add it before you use one.

Conventions: channels are 1 to 16, knobs and CV inputs are 1 to 4 (1 to 2 on MIDI-µKIT), trigger inputs and outputs are 1 and 2. Only MIDI input and output 1 are on by default.

### Filtering and routing

#### Filter: keep only channel 2

Drops everything except messages on MIDI channel 2. Messages without a channel, such as MIDI clock, are dropped too.

```js
midi.onMessage = function(midiPort, msg, msgType) {
   if (midi.getChannel(msg) === 2) {
      midiOut.send(msg);
   }
};
```

```lua
midi.onMessage = function(midiPort, msg, msgType)
   if midi.getChannel(msg) == 2 then
      midiOut.send(msg)
   end
end
```

#### Move CCs from channel 2 to channel 3

Everything else passes unchanged.

```js
midi.onMessage = function(midiPort, msg, msgType) {
   if (msgType === midi.CC && midi.getChannel(msg) === 2) {
      midi.setChannel(msg, 3);
   }
   midiOut.send(msg);
};
```

```lua
midi.onMessage = function(midiPort, msg, msgType)
   if msgType == midi.CC and midi.getChannel(msg) == 2 then
      midi.setChannel(msg, 3)
   end
   midiOut.send(msg)
end
```

#### Choose the target channel with a knob

Knob 1 picks the channel the CCs are moved to. `param.getValue()` returns 0 to 1, scaled here to 1 to 16.

```js
param.enable(1);

midi.onMessage = function(midiPort, msg, msgType) {
   if (msgType === midi.CC && midi.getChannel(msg) === 2) {
      let ch = Math.ceil(param.getValue(1) * 16);
      midi.setChannel(msg, ch);
   }
   midiOut.send(msg);
};
```

```lua
param.enable(1)

midi.onMessage = function(midiPort, msg, msgType)
   if msgType == midi.CC and midi.getChannel(msg) == 2 then
      local ch = math.ceil(param.getValue(1) * 16)
      midi.setChannel(msg, ch)
   end
   midiOut.send(msg)
end
```

#### Same, with a proper knob label

The same script, but the knob's tooltip says "MIDI Channel" and shows the channel number 1 to 16 instead of 0 to 1. See [Tooltips and value display](#tooltips-and-value-display).

```js
param.enable(1);

param.onTooltip = function(i) {
   if (i === 1) return "MIDI Channel";
   return "";
};

param.onValueText = function(i) {
   if (i === 1) return number.toString(Math.ceil(param.getValue(1) * 16));
   return number.toString(param.getValue(i));
};

midi.onMessage = function(midiPort, msg, msgType) {
   if (msgType === midi.CC && midi.getChannel(msg) === 2) {
      let ch = Math.ceil(param.getValue(1) * 16);
      midi.setChannel(msg, ch);
   }
   midiOut.send(msg);
};
```

![Dynamic MIDI channel routing for CC](./MidiKit-ex1.png)

```lua
param.enable(1)

param.onTooltip = function(i)
   if i == 1 then return "MIDI Channel" end
   return ""
end

param.onValueText = function(i)
   if i == 1 then return number.toString(math.ceil(param.getValue(1) * 16)) end
   return number.toString(param.getValue(i))
end

midi.onMessage = function(midiPort, msg, msgType)
   if msgType == midi.CC and midi.getChannel(msg) == 2 then
      local ch = math.ceil(param.getValue(1) * 16)
      midi.setChannel(msg, ch)
   end
   midiOut.send(msg)
end
```

### Creating new messages

Each of these sends a new message whenever a note-on arrives. See [Building messages](#building-messages) for all setters.

#### Send an NRPN

```js
midi.onMessage = function(midiPort, msg, msgType) {
   if (msgType === midi.NOTE_ON) {
      let nrpn = midi.createNRPN();
      midi.setNRPN(nrpn, 1, 12345, 13456);   // channel 1, parameter 12345, value 13456
      midiOut.send(nrpn);
   }
};
```

```lua
midi.onMessage = function(midiPort, msg, msgType)
   if msgType == midi.NOTE_ON then
      local nrpn = midi.createNRPN()
      midi.setNRPN(nrpn, 1, 12345, 13456)    -- channel 1, parameter 12345, value 13456
      midiOut.send(nrpn)
   end
end
```

#### Send a 14-bit CC

A 14-bit CC value is spread over two CCs: CC `n` carries the upper 7 bits (MSB), CC `n + 32` the lower 7 bits (LSB). `midi.createCc14bit()` keeps the two together, so a receiver never sees one without the other.

```js
midi.onMessage = function(midiPort, msg, msgType) {
   if (msgType === midi.NOTE_ON) {
      let cc14 = midi.createCc14bit();
      midi.setCc14bit(cc14, 1, 1, 12864);   // 100 * 128 + 64: CC 1 = 100 (MSB), CC 33 = 64 (LSB)
      midiOut.send(cc14);
   }
};
```

```lua
midi.onMessage = function(midiPort, msg, msgType)
   if msgType == midi.NOTE_ON then
      local cc14 = midi.createCc14bit()
      midi.setCc14bit(cc14, 1, 1, 12864)    -- 100 * 128 + 64: CC 1 = 100 (MSB), CC 33 = 64 (LSB)
      midiOut.send(cc14)
   end
end
```

#### Send SysEx

Write only the payload. MIDI-KIT adds the `f0` / `f7` framing.

```js
midi.onMessage = function(midiPort, msg, msgType) {
   if (msgType === midi.NOTE_ON) {
      let sysex = midi.create();
      midi.setSysEx(sysex, "ab33010001");
      midiOut.send(sysex);
   }
};
```

```lua
midi.onMessage = function(midiPort, msg, msgType)
   if msgType == midi.NOTE_ON then
      local sysex = midi.create()
      midi.setSysEx(sysex, "ab33010001")
      midiOut.send(sysex)
   end
end
```

#### Send raw bytes

`midi.setRaw()` covers message types without their own setter, such as an MTC quarter frame (status `f1`).

`setRaw()` does **no validation** beyond an even number of hex digits and the length limit. It does not check the status byte, the number of data bytes, the `00` to `7f` range of data bytes, or that a SysEx ends with `f7`. The bytes are sent as given, so the script is responsible for building a valid MIDI message. For SysEx use `midi.setSysEx()`, which adds `f0` / `f7` and checks the data bytes.

```js
midi.onMessage = function(midiPort, msg, msgType) {
   if (msgType === midi.NOTE_ON) {
      let mtc = midi.create();
      midi.setRaw(mtc, "f11a");
      midiOut.send(mtc);
   }
};
```

```lua
midi.onMessage = function(midiPort, msg, msgType)
   if msgType == midi.NOTE_ON then
      local mtc = midi.create()
      midi.setRaw(mtc, "f11a")
      midiOut.send(mtc)
   end
end
```

### Clocks and triggers

#### Send a MIDI clock pulse on every trigger

A trigger input does nothing until it is enabled. `trig.enableIn(1)` listens to channel 1 of trigger input 1.

```js
trig.enableIn(1);

trig.onTrigger = function(trigPort, channel) {
   let clock = midi.create();
   midi.setRaw(clock, "f8");
   midiOut.send(clock);
};
```

```lua
trig.enableIn(1)

trig.onTrigger = function(trigPort, channel)
   local clock = midi.create()
   midi.setRaw(clock, "f8")
   midiOut.send(clock)
end
```

#### Clock multiplier: one trigger per beat into 24 MIDI clock pulses

A Rack clock usually ticks once per beat, but MIDI clock needs 24 pulses per beat, and hardware that follows MIDI clock reveals any timing jitter at once. That makes this a case for [sample-accurate timing](#sample-accurate-timing). The first pulse goes out on the frame of the incoming tick. The other 23 are spread over the next beat with `midiOut.sendAtFrame()`, using the length of the previous beat as the prediction, as every clock multiplier has to.

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

The shipped **Clock multiplier** preset adds a menu for clocks that tick on 8th or 16th notes, and treats a long gap (the clock was stopped) as a restart instead of stretching the pulses over the gap.

**Adding swing.** To delay every second pulse by a fixed time, convert the time to frames once (the loop already works in frames) and add it:

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

#### Show the tempo of a clock

Measures the distance between two trigger edges, turns it into BPM (one tick per beat) and shows it on the panel. A gap of more than two seconds counts as a stopped clock.

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

### Cleaning up

#### All notes off when the script is unloaded

`rack.onUnload()` is the only reliable place to silence notes the script left sounding: nothing runs after it. This version sends CC 123 (All Notes Off) on all 16 channels, reusing one message. For a more thorough reset in a single line, use [`midiOut.panic()`](#panic) instead.

```js
midi.onMessage = function(midiPort, msg, msgType) {
   if (msgType === midi.NOTE_ON) {
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

```lua
midi.onMessage = function(midiPort, msg, msgType)
   if msgType == midi.NOTE_ON then
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

### Settings in the context menu

#### A channel menu and a toggle, saved with the patch

Two context-menu items: a channel selector and a pass-through switch. Both read their saved value at load and save every change, so they survive saving and reopening the patch. See [Context menu items](#context-menu-items) and [Saving settings](#saving-settings).

```js
let config = {
   channel:     rack.getConfig("channel", 1),
   passThrough: rack.getConfig("passThrough", false)
};

rack.registerContextMenu({
   type: "options",
   label: "#midichannel",          // a ready-made 1-16 channel menu
   onGetValue: function() { return config.channel; },
   onChange: function(channel) {
      config.channel = channel;
      rack.setConfig("channel", channel);
   }
});

rack.registerContextMenu({
   type: "boolean",
   label: "Pass through",
   onGetValue: function() { return config.passThrough; },
   onChange: function(checked) {
      config.passThrough = checked;
      rack.setConfig("passThrough", checked);
   }
});

midi.onMessage = function(midiPort, msg, msgType) {
   if (config.passThrough || midi.getChannel(msg) === config.channel) {
      midiOut.send(msg);
   }
};
```

```lua
local config = {
   channel     = rack.getConfig("channel", 1),
   passThrough = rack.getConfig("passThrough", false)
}

rack.registerContextMenu({
   type = "options",
   label = "#midichannel",         -- a ready-made 1-16 channel menu
   onGetValue = function() return config.channel end,
   onChange = function(channel)
      config.channel = channel
      rack.setConfig("channel", channel)
   end
})

rack.registerContextMenu({
   type = "boolean",
   label = "Pass through",
   onGetValue = function() return config.passThrough end,
   onChange = function(checked)
      config.passThrough = checked
      rack.setConfig("passThrough", checked)
   end
})

midi.onMessage = function(midiPort, msg, msgType)
   if config.passThrough or midi.getChannel(msg) == config.channel then
      midiOut.send(msg)
   end
end
```

### NRPN input

#### Translate NRPN into 14-bit CC

Instead of following the individual CCs an NRPN is made of, the script lets MIDI-KIT assemble them and receives each complete parameter change in `midi.onNrpn`. It looks the parameter number up in a small table and forwards the value as a 14-bit CC. While NRPN input is enabled, the CCs it is assembled from no longer reach `midi.onMessage`. See [Receiving NRPN, RPN and 14-bit CC](#receiving-nrpn-rpn-and-14-bit-cc).

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
   for (let i = 0; i < config.map.length; i++) {
      if (config.map[i].nrpnNumber === nrpnNumber) return config.map[i].ccNumber;
   }
   return -1;
}

midi.enableNrpnIn(1);

midi.onNrpn = function(midiPort, msg) {
   let ccNumber = findCcNumber(midi.getControl(msg));   // the NRPN parameter number
   if (ccNumber < 0) return;                            // not in config.map: ignore

   let cc14 = midi.createCc14bit();
   midi.setCc14bit(cc14, config.ccChannel, ccNumber, midi.getValue(msg));   // the 14-bit value
   midiOut.send(cc14);
};
```

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
   for i = 1, #config.map do
      if config.map[i].nrpnNumber == nrpnNumber then return config.map[i].ccNumber end
   end
   return -1
end

midi.enableNrpnIn(1)

midi.onNrpn = function(midiPort, msg)
   local ccNumber = findCcNumber(midi.getControl(msg))   -- the NRPN parameter number
   if ccNumber < 0 then return end                       -- not in config.map: ignore

   local cc14 = midi.createCc14bit()
   midi.setCc14bit(cc14, config.ccChannel, ccNumber, midi.getValue(msg))   -- the 14-bit value
   midiOut.send(cc14)
end
```

The shipped **NRPN to CC (assembled)** preset is this script plus a channel menu and a "Device sends 7-bit NRPN" switch, which calls `midi.enableNrpnIn(1, null, on ? "msb" : "lsb")`. The **NRPN to CC** preset does the same without assembly, by following the CCs itself.

### Sending data with Tipsy

[Tipsy](#tipsy) sends text or other data between modules as a stream of voltages over a cable. MIDI-KIT sends on trigger output 1 and receives on trigger input 1.

#### Send

```js
midi.onMessage = function(midiPort, msg, msgType) {
   trig.sendTipsy("Preset changed!");                          // type defaults to text/plain

   let config = JSON.stringify({ channel: 1, mode: "auto" });
   trig.sendTipsy(config, "application/json");
};
```

```lua
midi.onMessage = function(midiPort, msg, msgType)
   trig.sendTipsy("Preset changed!")                           -- type defaults to text/plain

   local config = json.encode({ channel = 1, mode = "auto" })
   trig.sendTipsy(config, "application/json")
end
```

#### Receive

```js
rack.onLoad = function() {
   trig.enableTipsyIn();
};

trig.onTipsyMessage = function(data, mimeType) {
   rack.log("received ", mimeType, ": ", data);

   if (mimeType === "application/json") {
      let config = JSON.parse(data);
      // ... use config
   }
};
```

```lua
rack.onLoad = function()
   trig.enableTipsyIn()
end

trig.onTipsyMessage = function(data, mimeType)
   rack.log("received ", mimeType, ": ", data)

   if mimeType == "application/json" then
      local ok, config = pcall(json.decode, data)
      if ok then
         -- ... use config
      end
   end
end
```

### Modules talking to each other

#### Share a transport between modules

`rack.sendBroadcast()` hands a value to every other MIDI-KIT module, no cable needed. Here one module (the *broadcaster*) turns the clock on its trigger input 1 into broadcasts, and sends start and stop from a "Running" item in its context menu. Any number of other modules (the *followers*) turn them into MIDI clock, start and stop. Each broadcast has a topic, so a follower can tell clock from transport and ignore anything else. The shipped **Transport broadcaster** and **Transport follower** presets are the full versions. See [Messages between modules](#messages-between-modules).

Broadcaster:

```js
let running = false;

function setRunning(value) {
   running = value;
   let state = running ? "start" : "stop";
   let n = rack.sendBroadcast({ state: state }, "transport");
   rack.log("Transport ", state, " sent to ", n, " module(s)");
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

```lua
local running = false

local function setRunning(value)
   running = value
   local state = running and "start" or "stop"
   local n = rack.sendBroadcast({ state = state }, "transport")
   rack.log("Transport ", state, " sent to ", n, " module(s)")
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

Follower:

```js
function sendRaw(hex) {
   let msg = midi.create();
   midi.setRaw(msg, hex);
   midiOut.send(msg);
}

rack.onBroadcast = function(value, topic) {
   if (topic === "clock") {
      sendRaw("f8");
   } else if (topic === "transport") {
      if (value.state === "start") sendRaw("fa");
      else if (value.state === "stop") sendRaw("fc");
   }
};
```

```lua
local function sendRaw(hex)
   local msg = midi.create()
   midi.setRaw(msg, hex)
   midiOut.send(msg)
end

function rack.onBroadcast(value, topic)
   if topic == "clock" then
      sendRaw("f8")
   elseif topic == "transport" then
      if value.state == "start" then sendRaw("fa")
      elseif value.state == "stop" then sendRaw("fc") end
   end
end
```

### Shipped presets

The **Script** menu holds many more scripts, each in a JavaScript and a Lua version. They are well commented and worth reading:

- **Routing and filtering:** Channel router, Port router, Keyboard split, Smart merge, MPE to single channel, Filter Ch2, Rewrite Ch1 to Ch2, Copy Ch1 CC to Ch2
- **Notes and scales:** Arpeggiator, Chord harmonizer, Scale quantiser, Micro scale, Velocity curve, Note length quantiser
- **Clocks and rhythm:** Clock multiplier, Clock divider, Euclidean rhythm generator, Transport broadcaster, Transport follower
- **Controllers and programs:** NRPN to CC, NRPN to CC (assembled), NRPN Generator, Bank Select (menu), Bank Select (param), Program Change CV, Program Change Trigger, Volca Sample
- **Other:** Monitor, Tipsy, TipsyIn, delay examples, and the experimental *creative* scripts (Bouncing ball delay, Gravity well, ...)

---

## MIDI messages

### Message handles

A script never holds a MIDI message directly. It holds a **handle**, a reference to a message MIDI-KIT keeps for it. `midi.onMessage` receives the incoming message as a handle, and `midi.create()` makes a new, empty one. All `midi.get*` and `midi.set*` functions take a handle as their first argument.

Three rules:

- **A handle is only valid inside the callback that received or created it.** Keeping one in a variable and using it in a later callback is an error. Build messages in the callback that sends them.
- **Messages can only be created inside callbacks.** Creating one at the top level fails the load. Creating one in `param.onTooltip`, `param.onValueText`, `input.onTooltip` or a menu item's `onGetValue` is an error as well. All other callbacks are fine, including `rack.onLoad()`, `rack.onUnload()` and a menu item's `onChange`.
- **Reuse a handle instead of creating one per message.** Sending copies the message, so you can change a handle and send it again. A chord, a burst of clock pulses or an all-notes-off needs one `midi.create()`, then a setter and a `midiOut.send()` per message.

**How many at once.** A callback can hold **32** handles at the same time. The incoming message uses one of them. A message received in `midi.onNrpn` or `midi.onRpn` uses 4, one in `midi.onCc14bit` uses 2 (they consist of that many MIDI messages). When all are in use, `midi.create()` and the other constructors raise "message store full (32 handles; reuse a handle or raise it with @requires messages=N)" and the rest of the callback is skipped. Messages sent before the error have already gone out, so a sequence (an NRPN, a chord release) can end up half-sent. A script that really needs more at once can ask for up to 512 with [`@requires messages=N`](#requires).

**Empty handles can't be sent.** A fresh `midi.create()` holds no message until a setter fills it. Sending it, or a [group handle](#nrpn-rpn-and-14-bit-cc) whose setter hasn't run, raises "message has no status byte".

**Creating handles**

| Function | Returns |
| --- | --- |
| `midi.create()` | a new, empty message |
| `midi.clone(msg)` | a new message with a copy of `msg`'s content. Changing the copy doesn't affect the original. Cloning a group handle clones the whole group |
| `midi.createNRPN()` | an empty NRPN [group handle](#nrpn-rpn-and-14-bit-cc), filled with `midi.setNRPN()` |
| `midi.createRPN()` | an empty RPN group handle, filled with `midi.setRPN()` |
| `midi.createCc14bit()` | an empty 14-bit CC group handle, filled with `midi.setCc14bit()` |

`midi.clone()` is the usual way to send a changed copy while also passing on the original:

```js
let copy = midi.clone(msg);
midi.setChannel(copy, 5);
midiOut.send(msg);
midiOut.send(copy);
```

### Message types

`midi.getType(msg)` tells what a message is. `midi.onMessage` receives the same value as its third argument, `msgType`, so most scripts never call `getType()` themselves.

```js
midi.onMessage = function(midiPort, msg, msgType) {
   switch (msgType) {
      case midi.NOTE_ON:  /* a key goes down */ break;
      case midi.NOTE_OFF: /* a key goes up */ break;
      case midi.CC:       /* ... */ break;
      case midi.ACTIVE_SENSING: break;    // drop
      default: midiOut.send(msg);
   }
};
```

```lua
midi.onMessage = function(midiPort, msg, msgType)
   if msgType == midi.NOTE_ON then
      -- a key goes down
   elseif msgType == midi.NOTE_OFF then
      -- a key goes up
   elseif msgType ~= midi.ACTIVE_SENSING then
      midiOut.send(msg)
   end
end
```

| Constant | Message | Status byte |
| --- | --- | --- |
| `midi.NOTE_ON` | Note On with velocity above 0 (or a 2-byte Note On without velocity) | `9n` |
| `midi.NOTE_OFF` | Note Off, **and Note On with velocity 0** | `8n`, `9n` |
| `midi.KEY_PRESSURE` | polyphonic aftertouch | `An` |
| `midi.CC` | control change, unless it is part of an assembled NRPN, RPN or 14-bit CC | `Bn` |
| `midi.PROGRAM_CHANGE` | program change | `Cn` |
| `midi.CHAN_PRESSURE` | channel aftertouch | `Dn` |
| `midi.PITCH_WHEEL` | pitch bend | `En` |
| `midi.NRPN`, `midi.RPN`, `midi.CC14BIT` | a [group handle](#nrpn-rpn-and-14-bit-cc), received or created | |
| `midi.SYSEX` | system exclusive | `F0` |
| `midi.MTC_QUARTER_FRAME` | MIDI time code quarter frame | `F1` |
| `midi.SONG_POSITION` | song position pointer | `F2` |
| `midi.SONG_SELECT` | song select | `F3` |
| `midi.TUNE_REQUEST` | tune request | `F6` |
| `midi.CLOCK` | clock | `F8` |
| `midi.START`, `midi.CONTINUE`, `midi.STOP` | transport | `FA`, `FB`, `FC` |
| `midi.ACTIVE_SENSING` | active sensing | `FE` |
| `midi.RESET` | system reset | `FF` |
| `midi.UNKNOWN` | undefined system messages | `F4`, `F5`, `F9`, `FD`, a lone `F7` |
| `midi.NONE` | an empty handle: a fresh `midi.create()`, or a group handle whose setter hasn't run | |

Worth knowing:

- **A Note On with velocity 0 counts as `midi.NOTE_OFF`.** That's how the MIDI specification defines a release, and most keyboards send releases that way. So `midi.NOTE_ON` only ever sees notes that start, which is what a script that tracks held notes needs. The bytes don't change: forwarding the message still sends `9n nn 00`. Releases created with `midi.setNoteOff()` (`8n`) are `midi.NOTE_OFF` as well.
- **Groups have their own type.** An assembled or created NRPN, RPN or 14-bit CC is `midi.NRPN`, `midi.RPN` or `midi.CC14BIT`, never `midi.CC`. A CC that wasn't assembled (because assembly is off) is a plain `midi.CC`.
- **Always compare against the constant.** The constants happen to be readable strings (`"noteOn"`, `"cc"`, ...), which is handy in `rack.log()` and in saved settings, but write `midi.NOTE_ON`, never `"noteOn"`.
- **Handle unknown types with `default` / `else`.** New constants may be added for status bytes that don't have one yet. Existing constants never change.
- The constants are read-only in JavaScript. In Lua they are plain fields: don't overwrite them.
- `midi.getType()` never fails on a valid handle.

### Reading messages

| Function | Returns |
| --- | --- |
| `midi.getType(msg)` | the [message type](#message-types) |
| `midi.getChannel(msg)` | the channel, 1 to 16. `-1` for messages without a channel (clock, transport, SysEx, ...) |
| `midi.getNote(msg)` | the note number. On a plain CC it returns the controller number too, but `getControl()` is the preferred way |
| `midi.getControl(msg)` | the controller number of a CC (0 to 127), the parameter number of an NRPN or RPN (0 to 16383), the MSB controller of a 14-bit CC (0 to 31). `-1` for anything else (notes, pitch bend, clock, ...) |
| `midi.getValue(msg)` | the data value: velocity of a note, value of a CC (0 to 127). The combined 14-bit value (0 to 16383) of an NRPN, RPN or 14-bit CC. The position (0 to 16383, in sixteenth notes) of a Song Position. The data byte of a Song Select or an MTC quarter frame (piece `v >> 4`, nibble `v & 15`; in Lua `v // 16` and `v % 16`) |
| `midi.getPitchWheel(msg)` | the pitch-bend value, 0 to 16383, 8192 is the centre |
| `midi.getProgramChange(msg)` | the program number |
| `midi.getChanPressure(msg)` | the channel aftertouch value |
| `midi.getSysEx(msg)` | the SysEx payload as a hex string, without `f0` / `f7` |
| `midi.getSysExLength(msg)` | the SysEx payload length in bytes, without `f0` / `f7` |
| `midi.getLength(msg)` | the size of the whole message in bytes (SysEx including `f0` / `f7`) |
| `midi.getRaw(msg)` | the message's bytes as a hex string, exactly as received or built |
| `midi.toString(msg)` | one line of readable text for the log, worded like [MIDI-MON](../midi/MidiMon.md) |

`midi.toString()` produces lines like `ch01 note on  60 vel 100`, `ch02 cc7=100`, `ch01 nrpn param=1234 value=16383`, `clock tick` or `sysex (12 data bytes) 43 10 4c …` (the first 32 payload bytes). A Note On with velocity 0 reads as `note off`. It is meant for reading, and its wording may get more detailed in future versions, so don't take it apart in a script: use the getters or `getRaw()` for that.

### Building messages

Setters fill or change a message. Most take the channel as the second argument.

| Function | Notes |
| --- | --- |
| `midi.setNoteOn(msg, ch, note, vel)` | |
| `midi.setNoteOff(msg, ch, note [, vel])` | release velocity defaults to 0 |
| `midi.setNote(msg, note)` | change only the note number |
| `midi.setKeyPressure(msg, ch, note, value)` | polyphonic aftertouch |
| `midi.setCc(msg, ch, cc, value)` | |
| `midi.setProgramChange(msg, ch, program)` | |
| `midi.setChanPressure(msg, ch, value)` | read it back with `getChanPressure()`, not `getValue()` |
| `midi.setPitchWheel(msg, ch, value)` | 0 to 16383, 8192 is the centre |
| `midi.setChannel(msg, ch)` | change only the channel. On a group handle: every message of the group |
| `midi.setValue(msg, value)` | change only the data value (velocity, CC value), 0 to 127. On a group handle: the combined 14-bit value, 0 to 16383 |
| `midi.setSysEx(msg, hex)` | payload only: `f0` / `f7` are added for you, so pass `"43104c0000"`, not `"f043104c0000f7"`. Every byte must be `00` to `7f`, at most 8192 bytes |
| `midi.setRaw(msg, hex)` | the exact bytes, nothing added and no validation of the content (see [Send raw bytes](#send-raw-bytes)), for example `"f11a"` for an MTC quarter frame. At most 8194 bytes |
| `midi.setNRPN(handle, ch, number, value)` | see [NRPN, RPN and 14-bit CC](#nrpn-rpn-and-14-bit-cc) |
| `midi.setRPN(handle, ch, number, value)` | |
| `midi.setCc14bit(handle, ch, cc, value)` | |

**Values are clamped, never wrapped.** A number is rounded to the nearest whole number and limited to the valid range: channels to 1 to 16, 7-bit values (note, velocity, CC number and value, program, pressure) to 0 to 127, 14-bit values to 0 to 16383. So `midi.setNote(msg, 132)` gives note 127, not note 4, and `midi.setNote(msg, 60.5)` gives note 61. `NaN` becomes the lowest value.

**Changing an incoming message.** The `msg` passed to `midi.onMessage` can be changed with setters and then sent, as in [Move CCs from channel 2 to channel 3](#move-ccs-from-channel-2-to-channel-3). To keep the original as well, [clone](#message-handles) it first.

### NRPN, RPN and 14-bit CC

These three kinds of message are each made of **several CCs** that only mean something together:

| Kind | CCs on the wire | Value range |
| --- | --- | --- |
| NRPN | CC 99 / 98 select the parameter, CC 6 / 38 carry the value | parameter 0 to 16383, value 0 to 16383 |
| RPN | CC 101 / 100 select the parameter, CC 6 / 38 carry the value | same |
| 14-bit CC | CC `n` (MSB, upper 7 bits) and CC `n + 32` (LSB, lower 7 bits), `n` = 0 to 31 | 0 to 16383 |

MIDI-KIT treats each as a single **group handle**, so a script deals with "parameter 1234 is now 8000" instead of four separate CCs.

#### Sending a group

Create the group, fill it with its setter and send it. The whole group goes out in order, as one unit.

| Function | Notes |
| --- | --- |
| `midi.createNRPN()` + `midi.setNRPN(h, ch, number, value)` | 4 CCs. NRPN has no "null" parameter: 16383 is an ordinary parameter |
| `midi.createRPN()` + `midi.setRPN(h, ch, number, value)` | 4 CCs. RPN 0 is the pitch-bend range: `midi.setRPN(h, 1, 0, 12 << 7)` sets 12 semitones (upper 7 bits = semitones, lower = cents). Parameter 16383 is the **RPN null** ("no parameter selected"): only the two select CCs are sent, the value is ignored, and `getValue()` returns -1. Send it to deselect, so later data entry is ignored by the receiver |
| `midi.createCc14bit()` + `midi.setCc14bit(h, ch, cc, value)` | 2 CCs: `cc` (0 to 31) carries `value >> 7`, `cc + 32` carries `value & 127` |

There is also an older five-argument form, `midi.setCc14bit(msgMsb, msgLsb, ch, cc, value)`, which fills two separate plain handles. They are sent as two independent messages, so a receiver may see one without the other. Prefer the group form.

#### Working with a group handle

A group handle reads like a single message:

| Function | On a group handle |
| --- | --- |
| `midi.getType(msg)` | `midi.NRPN`, `midi.RPN` or `midi.CC14BIT` once its setter has run, `midi.NONE` before |
| `midi.getControl(msg)` | the parameter number (NRPN, RPN) or the MSB controller (14-bit CC). `-1` before the setter has run |
| `midi.getValue(msg)` | the combined 14-bit value. `-1` before the setter has run |
| `midi.getChannel(msg)` | the group's channel |
| `midi.getNote(msg)`, `midi.getRaw(msg)` | the first message of the group: CC 99 (NRPN), CC 101 (RPN) or the MSB CC |
| `midi.setChannel(msg, ch)` | moves the whole group |
| `midi.setValue(msg, value)` | sets the combined value, keeping channel and number. An error before the setter has run |
| `midi.clone(msg)` | clones the whole group |
| `midiOut.send(msg)`, `midiOut.cancel(msg)` | send, or [cancel](#cancelling-scheduled-messages), the whole group |

Any other setter would change only one CC of the group and leave a broken group on the wire. It raises an error instead and leaves the handle unchanged, for example `midi.setNote: message is an NRPN; use midi.setNRPN()`. The same applies to the five-argument `setCc14bit` with a group handle.

`setCc14bit`, `setNRPN`, `setRPN` and `setValue` all use the same 0 to 16383 scale that `getValue()` returns, so a received value can be passed straight on. The **NRPN to CC** preset ([JavaScript](../../presets/MidiKit/JavaScript/NRPN%20to%20CC.js), [Lua](../../presets/MidiKit/Lua/NRPN%20to%20CC.lua)) and the **NRPN Generator** preset ([JavaScript](../../presets/MidiKit/JavaScript/NRPN%20Generator.js), [Lua](../../presets/MidiKit/Lua/NRPN%20Generator.lua)) show this in practice.

#### Receiving NRPN, RPN and 14-bit CC

By default `midi.onMessage` sees the individual CCs as they arrive. To receive complete changes instead, enable assembly for the kind you want and define its callback:

| Function | Effect |
| --- | --- |
| `midi.enableNrpnIn(midiPort [, channel] [, dataEntry])` | assemble NRPNs on that MIDI input into `midi.onNrpn(midiPort, msg)` calls. `channel` defaults to all channels. `dataEntry` is `"lsb"` (default) or `"msb"`, see below |
| `midi.enableRpnIn(midiPort [, channel] [, dataEntry])` | the same for RPNs, into `midi.onRpn(midiPort, msg)` |
| `midi.enableCc14bitIn(midiPort [, cc] [, channel])` | assemble 14-bit CC pairs into `midi.onCc14bit(midiPort, msg)`. `cc` is the MSB controller, 0 to 31. Leave it out to assemble all 32 |

To give `dataEntry` for all channels, pass `null` (JavaScript) or `nil` (Lua) as the channel: `midi.enableNrpnIn(1, null, "msb")`.

The `msg` the callback receives is a group handle, read as described [above](#working-with-a-group-handle). A received group reads exactly like one built with `midi.createNRPN()` and `midi.setNRPN()`. Sending it with `midiOut.send(msg)` forwards the whole group, rebuilt as CC 99, 98, 6, 38 (101, 100, 6, 38 for RPN; MSB and LSB for 14-bit CC), whatever the device actually sent. A value that arrived as CC 38 alone goes out with CC 6 = 0.

**The component CCs are taken out of `midi.onMessage`.** Once a kind is enabled, the CCs it is built from no longer reach `midi.onMessage`, so the script doesn't have to filter them. They are consumed: if a device drops a message halfway, the CCs that did arrive are gone.

- **Only for the enabled kind.** A script that enabled only 14-bit CC still sees CC 98 / 99 in `midi.onMessage`, because it didn't enable NRPN.
- **Enable a kind only together with its callback.** Without the callback, the assembled changes go nowhere *and* their CCs are withheld, so the script sees less MIDI than before.

**When a change is reported**

- Selecting a parameter (CC 99 / 98 or 101 / 100) alone reports nothing. Only the value does. The RPN reset 127 / 127 reports nothing either.
- **Data entry mode `"lsb"`** (the default): a change is reported on CC 38, with the value built from the last CC 6 and this CC 38. A CC 6 alone only stores the upper part. A CC 38 without a CC 6 before it gives just the lower part. Suits devices that always send CC 38 last.
- **Data entry mode `"msb"`**: a change is also reported on CC 6, with the lower part 0. Suits devices that send 7-bit NRPN (`99, 98, 6` without CC 38) and devices that change the coarse value with CC 6 alone. A device that sends the full `6, 38` then reports twice per change, first the coarse value and then the exact one, so a script that forwards each change sends both.
- The value is on the 0 to 16383 scale in both modes. A script that wants a 7-bit value reads `midi.getValue(msg) >> 7`.
- Each `enable` call sets the mode for the channels it names, and a missing `dataEntry` means `"lsb"`, so the last call for a channel decides. A script can call it again later, from a menu item for example, and the new mode applies from the next CC 6. Any other value than `"lsb"` or `"msb"` is an error.
- While a parameter is selected, CC 6 is withheld from `midi.onMessage` in both modes.
- Not supported in either mode: CC 38 sent *before* CC 6, and data increment / decrement (CC 96 / 97).
- **A 14-bit CC needs both halves.** A change is reported when the LSB (CC `n + 32`) arrives. An MSB alone reports nothing and is withheld, so the device has to send the pair for every change. An MSB of 0 counts like any other, so values below 128 work. The very first MSB from a controller not seen before still reaches `midi.onMessage` as a plain CC.

**Overlapping ranges.** CC 0 to 31 are all possible 14-bit MSBs, and CC 6 is also NRPN / RPN data entry. So:

- With *all* 14-bit CCs and NRPN enabled, CC 6 / 38 are taken as a 14-bit pair and a data entry can report both `onCc14bit` and `onNrpn`. This is expected.
- Enabling all 14-bit CCs claims every controller from 0 to 31, so an ordinary 7-bit controller in that range (a mod wheel on CC 1) is withheld after its first message.
- Enable only the 14-bit controllers you mean, with `midi.enableCc14bitIn(midiPort, cc)`.

Like everything else enabled by a script, the assembly settings and data entry modes are reset when the script is reloaded or the module is reset.

---

## Sending MIDI

### Output ports

MIDI-KIT has four MIDI outputs. Every sending function uses the output last chosen with `midiOut.selectPort()`, or output 1 if it was never called.

| Function | Effect |
| --- | --- |
| `midiOut.selectPort(midiPort)` | send on output `midiPort` (1 to 4) from now on, until it is called again. The choice is kept across callbacks |
| `midiOut.enablePorts(count)` | enable outputs 1 to `count`, see below |

To send the same message on several outputs, call `midiOut.selectPort()` and `midiOut.send()` once per output.

#### Enabling MIDI ports

Only MIDI input 1 and output 1 are on by default. A script that uses more calls, at the top level or in `rack.onLoad()`:

```js
midi.enablePorts(3);      // deliver messages from MIDI inputs 1-3 to midi.onMessage
midiOut.enablePorts(2);   // allow sending on MIDI outputs 1-2
```

- `n` is 1 to 4 and enables the first `n` ports. `1` changes nothing. Anything out of range is an error.
- Until an input is enabled, its messages never reach the script. Until an output is enabled, messages sent to it are dropped, logged once per output.
- Enabled ports appear in the module's context menu (as "MIDI input 2", ...), where you choose their devices. Input and output 1 are on the panel.
- Enabled ports are forgotten when the script is reloaded or cleared, or the module is reset.

### Send now, later, or on a beat

| Function | Sends the message ... |
| --- | --- |
| `midiOut.send(msg)` | now. With [`midiOut.enableTiming()`](#sample-accurate-timing): on the frame of the event being handled |
| `midiOut.sendAfterMs(msg, ms)` | `ms` milliseconds from now. `-1` means "right after everything Rack's output queue still holds" (two audio blocks and one frame) |
| `midiOut.sendAtFrame(msg, frame)` | at engine frame `frame` (see [Frames](#frames)). A negative frame means now |
| `midiOut.sendAfterTrigger(msg, ticks [, trigPort [, channel]])` | after `ticks` triggers on trigger input `trigPort` (default 1), polyphonic channel `channel` (default 1). The trigger input must be [enabled](#trigger-inputs) |

**Each call sends the message as it is at that moment.** The message is copied, so:

- calling a send function twice sends twice, and changing the handle afterwards doesn't change what was already sent;
- `midiOut.sendAfterTrigger(msg, 5)` followed by `midiOut.send(msg)` sends two messages.

Messages reach the output while the callback is still running, in the order of the calls. In a very long callback, the first messages may already be on the wire before it returns.

"From now" for `sendAfterMs` means from the latest frame the module had processed when the script ran. With `midiOut.enableTiming()` it is the frame of the event being handled, see [Which frame a message gets](#which-frame-a-message-gets).

### Cancelling scheduled messages

`midiOut.cancel()` withdraws messages that `sendAfterMs`, `sendAtFrame` or `sendAfterTrigger` are still holding back, on the output selected with `midiOut.selectPort()`.

```js
midiOut.cancel();       // everything scheduled on the selected output
midiOut.cancel(msg);    // only scheduled messages with the same "address" as msg
```

The address ignores values and compares only what a message is about:

| `msg` is a ... | it cancels scheduled messages with the same ... |
| --- | --- |
| Note Off, or Note On with velocity 0 | Note Off (either form), channel and note |
| Note On (velocity above 0) | Note On, channel and note |
| polyphonic aftertouch | type, channel and note |
| control change | type, channel and controller number |
| program change, channel aftertouch, pitch bend | type and channel |
| SysEx | any scheduled SysEx |
| other system message (clock, start, stop, MTC, ...) | status byte |
| NRPN or RPN handle | the whole NRPN / RPN with the same channel and parameter number |
| 14-bit CC handle | the whole 14-bit CC with the same channel and MSB controller |

- Note On and Note Off are different addresses. Cancelling both takes two calls.
- A message received in `midi.onNrpn`, `onRpn` or `onCc14bit` is a group handle and cancels the matching scheduled group.
- Groups are never split. A plain CC 99 doesn't cancel part of a scheduled NRPN, and `midiOut.cancel()` without argument removes groups whole.
- It never affects `midiOut.send()` (not even with `enableTiming()`), nor anything that has already gone out.
- Calls apply in order: `sendAfterMs(a, 10); cancel(); sendAfterMs(b, 10)` sends only `b`.
- **It takes effect a moment later**, when the audio thread next checks (every 8 samples), not at the exact frame of the event. A message due before then still goes out.
- Cancelling a Note Off whose Note On has already gone out leaves the note hanging. That's up to the script.
- Nothing matching is not an error. An empty handle, an unset group handle, a second argument or an argument that isn't a message are errors.
- In `rack.onUnload()` it does nothing: the script's scheduled messages are dropped anyway.

Example: retrigger a note whose scheduled Note Off is still pending, so the old release doesn't cut the new note short:

```js
let off = midi.create();
midi.setNoteOff(off, 1, 60);
midiOut.cancel(off);               // withdraw the old scheduled release
midiOut.send(off);                 // release now, then play the note again
```

```lua
local off = midi.create()
midi.setNoteOff(off, 1, 60)
midiOut.cancel(off)
midiOut.send(off)
```

### Panic

`midiOut.panic()` silences whatever the script's output may have left on a device: held notes, a held sustain pedal, a pitch bend. It needs no record of what was played.

1. It drops every message the script has scheduled for later, like `midiOut.cancel()`, so no note starts after the reset.
2. It sends, per channel and in this order: sustain off (CC 64), all notes off (CC 123), all sound off (CC 120) and reset all controllers (CC 121).

Which outputs and channels:

- Every output the script has enabled with `midiOut.enablePorts()`, and no other. It ignores `midiOut.selectPort()`.
- An output set to a MIDI channel in the module gets the messages on that channel only. An output without a channel setting gets all 16 channels.
- It returns `true`, or `false` if the output queue was too full to take all of it (see [Queues](#queues)).

The natural place is `rack.onUnload()`: the devices are then left silent whenever the script is replaced, edited, reloaded or removed. Call it in `rack.onLoad()` too, to start from silent devices after a script that was cut off.

```js
rack.onLoad = function() {
   midiOut.enablePorts(2);
};

rack.onUnload = function() {
   midiOut.panic();
};
```

```lua
rack.onLoad = function()
   midiOut.enablePorts(2)
end

rack.onUnload = function()
   midiOut.panic()
end
```

Some devices ignore CC 120, 121 or 123. For those, send the note offs yourself.

### Queues

Everything a script sends goes through queues of limited size:

| Queue | Holds | When full |
| --- | --- | --- |
| output | 2048 messages per module, handed on at up to 128 every 8 samples (2048 take about 2.7 ms at 48 kHz) | the message is dropped and logged |
| `sendAfterMs`, `sendAtFrame` | 256 per output | the message is sent at once, logged once per script |
| `sendAfterTrigger` | 32 per trigger input channel | the message is sent at once, logged once per script |

A delayed message that finds its queue full is sent at once rather than dropped, because a dropped Note Off would leave a note hanging. For a long tail of delayed notes, release them in steps, or keep fewer messages pending than these limits.

---

## Sample-accurate timing

By default MIDI-KIT hands a message to the MIDI output as soon as the script has sent it, at the next audio block boundary. That gives the lowest latency, but the moment a message leaves can wander by up to one audio block: 5.3 ms at a block size of 256 and 48 kHz. That is fine for a filter, a merge or a panic button, and audible in a clock, an arpeggiator or a sequencer.

### Turning it on

Call `midiOut.enableTiming()` once, at the top level or in `rack.onLoad()`:

```js
rack.onLoad = function() {
   midiOut.enableTiming();
};
```

- Every message then carries an exact *frame* (see below), and Rack's MIDI output sends it on that frame, to within about 100 µs.
- It applies to messages sent after the call, and can't be switched off again. A script that doesn't call it behaves as described in [Sending MIDI](#sending-midi).
- Like the other `enable` calls, it is forgotten when the script is reloaded or cleared, or the module is reset.

**The cost:** Rack delays timed output by one audio block (5.3 ms at 256 samples and 48 kHz). Every message is delayed by the same amount, so a clock or a sequence keeps its shape. But a script that answers incoming MIDI answers about one block later than without timing. Don't use it where the lowest possible latency matters more than a steady rhythm.

### Frames

A **frame** is one tick of Rack's sample counter: one second is as many frames as the sample rate (48000 at 48 kHz). `rack.getEventFrame()` returns the frame of the event being handled:

| In ... | `rack.getEventFrame()` is ... |
| --- | --- |
| `midi.onMessage` | the frame the message arrived on (for an assembled NRPN, RPN or 14-bit CC: the frame of its last CC) |
| `trig.onTrigger` | the frame of the rising edge |
| `trig.onTipsyMessage` | the frame the message was complete |
| `rack.onBroadcast` | the frame of the event the sender was handling |
| top-level code, `rack.onLoad`, `rack.onUnload`, menu callbacks | `-1`: there is no event |

A script places messages relative to this frame with `midiOut.sendAtFrame()`. The clock multiplier in the [Cookbook](#clock-multiplier-one-trigger-per-beat-into-24-midi-clock-pulses) measures the distance between two edges in frames and spreads pulses over it.

**Converting between milliseconds and frames.** The number of frames in 10 ms depends on the sample rate (441 at 44.1 kHz, 480 at 48 kHz), so don't hard-code frame counts. Convert instead:

| Function | Returns |
| --- | --- |
| `rack.msToFrames(ms)` | frames in `ms` milliseconds at the current sample rate, rounded to a whole frame (negative stays negative) |
| `rack.framesToMs(frames)` | milliseconds in `frames` frames, not rounded |

Both work anywhere, including `rack.onLoad()`, with or without `enableTiming()`. Note that `midiOut.sendAfterMs()` already does "event plus *n* ms" by itself. Use `msToFrames` when a time offset is combined with other frame arithmetic (like the [swing](#clock-multiplier-one-trigger-per-beat-into-24-midi-clock-pulses) example), and `framesToMs` when a measured distance should become a time or a tempo (like [Show the tempo of a clock](#show-the-tempo-of-a-clock)).

### Which frame a message gets

| Call | Without `enableTiming()` | With `enableTiming()` |
| --- | --- | --- |
| `midiOut.send(msg)` | at once | on the frame of the event being handled. Outside an event (`rack.onLoad`, menu callbacks) as soon as possible. In `rack.onUnload`, behind everything Rack's output queue still holds (two blocks and one frame) |
| `midiOut.sendAfterMs(msg, ms)` | `ms` after the latest frame the module had processed when the script ran | `ms` after the frame of the event (outside an event, after the latest frame processed) |
| `midiOut.sendAtFrame(msg, frame)` | held until `frame`, then sent at once | on `frame` |
| `midiOut.sendAfterTrigger(msg, ticks, ...)` | at once when the tick is reached | on the frame of the trigger edge that reaches the tick |

### Timed trigger outputs

With `enableTiming()`, the trigger output functions `trig.setTrigger`, `setGate`, `setHigh` and `setLow` are timed too when they are called while handling an event. The output changes on the event's frame plus one audio block, the same delay the MIDI gets, so a "note to trigger" script produces the note and the trigger together.

- Outside an event (`rack.onLoad`, menu callbacks) a write happens when the script runs, as without timing. In `rack.onUnload` it is ignored.
- A write whose frame has already passed (a slow script) happens at once.
- Up to 64 timed writes can be waiting. Beyond that, a write happens at once.

### Order

Messages for the same frame leave in the order they were sent, and are moved one sample apart so an NRPN, a 14-bit CC pair, or a Note Off followed by a Note On of the same note always arrive in order. A message for an earlier frame leaves before one for a later frame, whatever the order of the calls.

Messages only a few samples apart can still swap places if Rack hands them to its output in different audio blocks. Rack's own MIDI-CV and CV-MIDI have the same limit. Order is kept per output of the module: two outputs, or two modules, that send to the same device are not ordered against each other.

### Unloading and stuck notes

A Note On sent just before a reload may still be waiting in Rack's output queue, up to one audio block. A Note Off sent at once would overtake it and leave the note stuck. So the module holds back what `rack.onUnload()` sends with `midiOut.send()` for two audio blocks and one frame, which puts it behind everything Rack still holds. A script that plays notes just sends its note offs or `midiOut.panic()` from `rack.onUnload()`, as the **Arpeggiator** and **Euclidean rhythm generator** presets do. When the module is removed, the messages go out at once, because Rack's output queue goes away with the module.

### Finding out when timing doesn't hold

Rack can only place a message on its frame if the message arrives in time: no later than one audio block after that frame. A slow script, or many busy MIDI-KIT scripts in the same patch, can make messages arrive late. Rack then sends them at once, which is the timing you'd have without `enableTiming()`, and nothing tells you. `midiOut.enableTiming(true)` reports such messages in the log, at most once per second:

```
Timing: message(s) reached the output too late
```

The report is off by default and costs nothing when off. A message only a few samples behind its frame is not late: Rack's one block of delay absorbs that.

**Output devices.** Only drivers that honor a message's frame can place it. With a driver that doesn't, messages go out when MIDI-KIT hands them over, as without timing.

---

## Knobs, CV and triggers

### Panel knobs

| Function | Effect |
| --- | --- |
| `param.enable(i)` | show knob `i` on the panel |
| `param.getValue(i [, fallback])` | the knob's position, 0 to 1. If knob `i` doesn't exist on this module (knob 3 on MIDI-µKIT) and a `fallback` is given, returns the fallback instead of raising an error |
| `param.count` | the number of knobs: 4, or 2 on MIDI-µKIT |

A knob number above `param.count` is an error, see [Module variants](#module-variants).

To turn the 0 to 1 range into something useful, multiply it, or use [`number.rescale()`](#number-helpers):

```js
let ch = Math.ceil(param.getValue(1) * 16);                    // 1 to 16 (0 at the far left)
let ms = number.rescale(param.getValue(2), 0, 1, 10, 1000);    // 10 to 1000
```

### CV inputs

| Function | Effect |
| --- | --- |
| `input.enable(i)` | show CV input `i` on the panel |
| `input.getVoltage(i [, ch])` | the voltage at input `i`, polyphonic channel `ch` (default 1) |
| `input.isHigh(i [, ch])` | `true` when the voltage is above 0.7 V |
| `input.isLow(i [, ch])` | `true` when the voltage is below 0.7 V |
| `input.count` | the number of CV inputs: 4, or 2 on MIDI-µKIT |

CV inputs are read when the script asks for them, typically inside `midi.onMessage` or `trig.onTrigger`. There is no callback for a changing voltage. To react to a gate or clock, use a [trigger input](#trigger-inputs).

### Tooltips and value display

Three optional functions replace the default panel tooltips. Each receives the knob or input number and returns a string:

| Function | Shows |
| --- | --- |
| `param.onTooltip(i)` | the name of knob `i` |
| `param.onValueText(i)` | the value text of knob `i` |
| `input.onTooltip(i)` | the name of CV input `i` |

They are looked up each time a tooltip is shown, so a script may replace them at any time, for example when a mode changes. See [Same, with a proper knob label](#same-with-a-proper-knob-label) for an example. They can't create MIDI messages.

### Trigger inputs

MIDI-KIT has two trigger inputs, each polyphonic. The optional `ch` is the polyphonic channel and defaults to 1.

| Function | Effect |
| --- | --- |
| `trig.enableIn(trigPort [, ch])` | start listening to channel `ch` of trigger input `trigPort` |
| `trig.onTrigger(trigPort, ch)` | callback, called on every rising edge of an enabled input channel |
| `trig.getTicks(trigPort [, ch])` | how many triggers that input channel has counted |
| `trig.isHigh(trigPort [, ch])`, `trig.isLow(trigPort [, ch])` | the current state of the input |
| `trig.inCount` | the number of trigger inputs: 2 |

**A trigger input does nothing until `trig.enableIn()` is called** for it: no `trig.onTrigger`, no counting (`trig.getTicks()` stays 0), and no `midiOut.sendAfterTrigger()` messages sent. Enable each input channel you need. For a polyphonic clock, enable each channel: `trig.enableIn(1, 1)`, `trig.enableIn(1, 2)`, ...

### Trigger outputs

| Function | Effect |
| --- | --- |
| `trig.setTrigger(i [, ch])` | a short trigger pulse on output `i` |
| `trig.setGate(i [, ch], durationMs)` | a gate of `durationMs` milliseconds |
| `trig.setHigh(i [, ch])`, `trig.setLow(i [, ch])` | set the output high or low until changed |
| `trig.outCount` | the number of trigger outputs: 2 |

An output number above 2 is an error. Without `midiOut.enableTiming()`, an output changes when the script runs, which can be off by up to an audio block. With it, the change is placed exactly, together with the MIDI sent for the same event, see [Timed trigger outputs](#timed-trigger-outputs).

### Tipsy

[Tipsy](https://github.com/baconpaul/tipsy-encoder) is a protocol that sends arbitrary data (text, JSON, ...) between modules as a stream of voltages on an ordinary cable. MIDI-KIT sends on **trigger output 1** and receives on **trigger input 1**. Only these two are supported, so the functions take no port number. Modules that understand Tipsy include [TRANSIT](../transit/Transit.md), which receives preset snapshots this way. See the [Cookbook](#sending-data-with-tipsy) for complete examples.

| Function | Effect |
| --- | --- |
| `trig.sendTipsy(data [, mimeType])` | send `data` (a string) on trigger output 1. `mimeType` defaults to `"text/plain"` |
| `trig.enableTipsyIn([enabled])` | decode Tipsy messages arriving at trigger input 1. `false` releases the input again |
| `trig.onTipsyMessage(data, mimeType)` | callback, called once for every complete message received. Both arguments are strings |

**Sending**

- At most 256 bytes of data, and a MIME type of at most 255 characters.
- The data goes out one voltage per sample until the message is complete. During that time Tipsy takes over trigger output 1. Afterwards the output is back under the control of the `trig.*` functions.
- It sends no MIDI: it ignores `midiOut.selectPort()` and doesn't use a message handle.

**Receiving**

- `data` may contain any bytes, including zero bytes, up to 256 bytes.
- **While Tipsy owns trigger input 1, channel 1 of that input stops being a trigger input:** `trig.onTrigger` doesn't fire, `trig.getTicks()` doesn't count, and `trig.isHigh()` / `trig.isLow()` read `0`. The Tipsy voltages cross the trigger threshold constantly and would otherwise fire on almost every sample. Other channels and trigger input 2 are not affected. `trig.enableTipsyIn(false)` restores normal behavior.
- A broken or interrupted stream is reported once in the log, and the decoder picks up again with the next message.

---

## Module services

### Log and overlay

| Function | Effect |
| --- | --- |
| `rack.log(value [, value ...])` | write one line to the module's log |
| `rack.overlay(s1 [, s2 [, s3]])` | show up to three lines of text on the panel. All arguments must be strings |

`rack.log()` joins all its arguments without a separator, so `rack.log("CC ", cc, " = ", value)` needs no conversion. Each value is written like this:

| Value | Written as |
| --- | --- |
| string | as is, without quotes |
| number | like `number.toString()`: `1 / 3` gives `0.333333`, whole numbers print every digit however large |
| boolean | `true` / `false` |
| `null` / `undefined` (JavaScript), `nil` (Lua) | `null` / `undefined` |
| object, array, table, function | as the language itself would print it |

Numbers, strings and booleans look the same in both languages.

### Context menu items

`rack.registerContextMenu(item)` adds an entry to the module's right-click menu. Use it to change script settings without editing the script. Items appear in the order they are registered, and there is no limit on how many.

```js
rack.registerContextMenu({
   type: "boolean",
   label: "Velocity to CC",
   onGetValue: function() { return config.velocityToCc; },
   onChange: function(checked) { config.velocityToCc = checked; }
});
```

In Lua, the item is a table with the same fields: `{ type = "boolean", label = "...", onGetValue = function() ... end, onChange = function(checked) ... end }`.

#### Item types

| `type` | Looks like | `onChange` receives |
| --- | --- | --- |
| `"boolean"` | a line with a checkmark | `(checked)`: `true` or `false` |
| `"options"` | a submenu, checkmark on the current choice | `(index, label)`, or with value pairs `(value, label)` |
| `"action"` | a plain line, called on every click | nothing |
| `"fileopen"` | a line that opens a file dialog | `(content, fileName)` |
| `"separator"` | a divider line | (not clickable) |
| `"label"` | a heading | (not clickable) |

**Options.** `options` is either a list of labels, or a list of `[label, value]` pairs:

```js
// labels: onChange gets the index (0 = first), onGetValue returns an index (-1 = none)
rack.registerContextMenu({
   type: "options",
   label: "Out mode",
   options: ["Internal", "External", "Both"],
   onGetValue: function() { return config.outMode; },
   onChange: function(index, label) { config.outMode = index; }
});

// pairs: onChange gets the value, onGetValue returns a value
rack.registerContextMenu({
   type: "options",
   label: "Multiplier",
   options: [["1x", 1], ["2x", 2], ["4x", 4], ["8x", 8]],
   onGetValue: function() { return config.ratio; },
   onChange: function(value, label) {
      config.ratio = value;
      rack.setConfig("ratio", value);
   }
});
```

```lua
rack.registerContextMenu({
   type = "options",
   label = "Multiplier",
   options = { {"1x", 1}, {"2x", 2}, {"4x", 4}, {"8x", 8} },
   onGetValue = function() return config.ratio end,
   onChange = function(value, label)
      config.ratio = value
      rack.setConfig("ratio", value)
   end
})
```

With pairs, the script needs no index-to-value mapping. The checkmark goes on the option whose value equals what `onGetValue` returns (`===` in JavaScript, `==` in Lua). If none matches, for example because a saved setting comes from an older version of the script, nothing is checked.

**Ready-made MIDI channel menu.** Give an `"options"` item the label `"#midichannel"` and leave out `options`: the menu appears as **MIDI channel** with the channels 1 to 16, and `onChange` and `onGetValue` work with the channel number. `"#midichannel+all"` adds an **All** entry with the value `0` in front. Text after the key becomes part of the label, so `"#midichannel+all Input"` is shown as **MIDI channel (Input)**, which helps to tell two channel menus apart.

```js
rack.registerContextMenu({
   type: "options",
   label: "#midichannel+all",
   onGetValue: function() { return config.channel; },
   onChange: function(channel) {
      config.channel = channel;
      rack.setConfig("channel", channel);
   }
});
```

**Action, file, separator, label.**

```js
rack.registerContextMenu({
   type: "action",
   label: "Send all notes off",
   onChange: function() { midiOut.panic(); }
});

rack.registerContextMenu({
   type: "fileopen",
   label: "Import scale…",
   onChange: function(content, fileName) {
      // content: the file's text, at most 8192 bytes; fileName: e.g. "just.scl"
      rack.log("read ", content.length, " bytes from ", fileName);
   }
});

rack.registerContextMenu({ type: "separator" });
rack.registerContextMenu({ type: "label", label: "Clock" });
```

#### Fields

| Field | Required | Rule |
| --- | --- | --- |
| `type` | yes | one of the six types above |
| `label` | yes, except for `"separator"` | a non-empty string |
| `options` | for `"options"`, unless the label is a `#midichannel` key | a non-empty list of strings, or a non-empty list of `[label, value]` pairs whose values are numbers, strings or booleans. The two forms can't be mixed. In a list of pairs, no two options may have the same label or the same value (`1` and `1.0` are the same value, `1` and `"1"` are not) |
| `onChange` | yes, except for `"separator"` and `"label"` | a function |
| `onGetValue` | no | a function returning the current value: `true` / `false`, an index, or a pair's value. Without it, a boolean shows unchecked and an options menu checks the first entry |

`rack.registerContextMenu()` returns `true`. A malformed item is an error, which at the top level stops the script from loading.

#### How the callbacks behave

- **`onGetValue`** is called every time the menu opens, so the checkmark always reflects the script's current state, including settings restored when the patch was loaded. It runs while the menu is being built and must not send anything.
- **`onChange`** is called when the item is clicked. The menu shows the new checkmark at once, before the callback has run. `onChange` may call any function, including the `midiOut.*` senders: messages go out when it returns, as soon as possible (there is no event, so `rack.getEventFrame()` is `-1`). Trigger and Tipsy outputs work as usual. An error inside it is logged as `Context menu callback error: ...` and does no harm.
- To keep a menu choice when the patch is saved, call `rack.setConfig()` in `onChange`, see [Saving settings](#saving-settings).

**File items.**

- The file is passed on as stored, including line breaks (`\r\n` for a file saved on Windows). In Lua binary data arrives unchanged; in JavaScript the content is read as UTF-8 text.
- A file larger than **8192 bytes**, or one that can't be read, is refused with a message to the user, and `onChange` is not called. Cancelling the dialog calls nothing.
- The dialog has no file-type filter and no preset folder.
- If the script is replaced while the dialog is open, the chosen file is dropped.

#### Changing the menu at runtime

- Registering an item with a `label` that already exists **replaces** it, in the same position. That's how a script changes a menu, for example re-registering "Active input" with a different number of options when a setting changes. (Separators have no label, so each one is added.)
- `rack.unregisterContextMenu(label)` removes an item and returns `true`, or `false` if there was none. Registering the label again afterwards adds it at the end.
- All items are removed when the script is reloaded or cleared.

### Saving settings

`rack.getConfig()` and `rack.setConfig()` store script settings with the patch, so they survive saving and reopening it. It is a simple key / value store, and can be used anywhere and any number of times: in top-level code, `rack.onLoad()`, `rack.onUnload()`, `midi.onMessage`, a menu's `onChange`.

| Call | Effect |
| --- | --- |
| `rack.setConfig(key, value)` | store `value` under `key`, replacing what was there |
| `rack.setConfig(key, undefined)` (JavaScript), `rack.setConfig(key, nil)` (Lua) | remove `key` |
| `rack.getConfig(key)` | the stored value, or `undefined` / `nil` if `key` was never set |
| `rack.getConfig(key, default)` | the stored value, or `default` if `key` was never set |

**There is no "save" step.** A patch save writes whatever was last stored. Call `rack.setConfig()` the moment a setting changes, typically in a menu item's `onChange`. It is cheap.

**Read every setting with a default** at the top level or in `rack.onLoad()`:

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

The [Cookbook](#a-channel-menu-and-a-toggle-saved-with-the-patch) has a complete example with menu items.

**Which actions keep the settings?**

| Action | Saved settings |
| --- | --- |
| saving and reopening the patch | kept |
| **Reload** (*Script* menu, `Alt+Y`), applying an edit in the editor | kept |
| loading a file, pasting a script, choosing an example | start empty |
| **Clear** | removed |

Settings belong to the script that wrote them. A different script starts with an empty store.

**Keys and values**

| | Rule |
| --- | --- |
| key | starts with a letter or underscore, followed by letters, digits and underscores, at most 64 characters (`channel`, `_scale`, `noteLength2`). Anything else, including a dot, is rejected |
| value | boolean, number, string, array or object (table in Lua), nested at most **4 levels** deep |
| size | all settings together at most **64 KB** (as JSON) |

A rejected key or value (an invalid key, a function, a value that refers to itself, nested too deeply, too large) leaves the store unchanged and writes one line to the log.

### Messages between modules

MIDI-KIT modules in the same patch can send each other values, without a cable:

| Function | Effect |
| --- | --- |
| `rack.sendBroadcast(value [, topic])` | send `value` to all other MIDI-KIT modules. Returns how many received it |
| `rack.onBroadcast(value, topic)` | callback, called when another module sends a broadcast |

```js
rack.sendBroadcast({ state: "start" }, "transport");

rack.onBroadcast = function(value, topic) {
   if (topic === "transport") { /* ... */ }
};
```

```lua
rack.sendBroadcast({ state = "start" }, "transport")

function rack.onBroadcast(value, topic)
   if topic == "transport" then --[[ ... ]] end
end
```

See [Share a transport between modules](#share-a-transport-between-modules) for a complete example.

**Who receives**

- Every *other* MIDI-KIT module whose script defines `rack.onBroadcast`. A script never receives its own broadcasts.
- A module without the callback is skipped and not counted in the return value. Like the other callbacks, `rack.onBroadcast` must be [assigned at the top level](#assign-each-callback-once-at-the-top-level).
- A bypassed module still sends and receives.
- There's no subscription: every receiver gets every broadcast and ignores topics it doesn't know.
- A receiver can't tell which module sent a broadcast. Put an id in the value if you need one.

**What can be sent**

| | Rule |
| --- | --- |
| value | whatever `rack.setConfig()` accepts: booleans, numbers, strings, arrays and objects (tables in Lua), nested at most 4 levels. A Lua table with keys 1 to n becomes an array, any other table an object |
| size | at most **4 KB** as JSON |
| topic | optional, a string of at most **64 bytes**. The receiver gets `undefined` / `nil` if there was none |

The receiver gets its own copy of the value. A value or topic that can't be sent (a function, a value that refers to itself, nested too deeply, too large, a topic that isn't a string) writes one line to the log and returns `0`, and the script carries on. Calling `rack.sendBroadcast()` without a value is an error.

**Delivery**

- **A broadcast arrives a few samples later**, on the receiver's next processing pass, not before `rack.sendBroadcast()` returns.
- Broadcasts from one sender arrive in the order they were sent. The order across different receivers is not defined.
- The receiver sees the frame of the sender's event, so `rack.getEventFrame()` and [timed sending](#sample-accurate-timing) work relative to the original event. A broadcast sent outside an event (`rack.onLoad`, a menu callback) has frame `-1`.
- Each receiver queues up to **16** broadcasts. More than that between two passes are dropped, and "Broadcast input queue full" is logged once.
- **No replay.** A module loaded after a broadcast never sees it. A script that needs to catch up has to ask for the current state.

**Loading and unloading**

- A broadcast from `rack.onLoad()` announces a newly loaded module. Modules loaded together with a patch can receive each other's `rack.onLoad()` broadcasts.
- A broadcast from `rack.onUnload()` is allowed. Receivers handle it after the sender is gone.
- A module stops receiving when its script is replaced, cleared or removed.

**Don't answer every broadcast with a broadcast.** If two scripts both reply from `rack.onBroadcast`, they answer each other forever. The modules stay responsive, but it never stops. Reply only to requests, never to replies, for example by giving each its own topic.

### Random numbers

| Function | Effect |
| --- | --- |
| `rack.random()` | a random number from 0 up to (not including) 1 |
| `rack.setRandomSeed(seed)` | restart the sequence from `seed`, any finite number |

`rack.random()` is **repeatable**: each module has its own seed, stored with the patch, and every script load (also a reload or reopening the patch) restarts the sequence from it. The same script therefore produces the same "random" values every time. Another MIDI-KIT module has a different seed.

`rack.setRandomSeed(seed)` restarts the sequence immediately. It doesn't change the seed stored with the patch, so the next load starts from the stored seed again. Call it in `rack.onLoad()` for a fixed sequence of your own. `NaN` and infinity are errors.

For non-repeatable randomness, JavaScript's `Math.random()` and Lua's `math.random()` are also available.

### Number helpers

| Function | Returns |
| --- | --- |
| `number.rescale(x, xMin, xMax, yMin, yMax [, curve])` | `x` mapped from the range `xMin` to `xMax` onto `yMin` to `yMax`. Without `curve` the result is not clamped: values outside the input range end up outside the output range. `curve` bends the response: 0 is linear, positive values start slowly and rise steeply towards the end, negative values the other way round |
| `number.crossfade(a, b, pos)` | the blend of `a` and `b`: `a` at `pos` = 0, `b` at `pos` = 1 |
| `number.toString(x)` | `x` as text: whole numbers without decimals, others with up to 6 decimals, trailing zeros removed (`0.333333`, `2.5`, `48000`) |

They behave the same in both languages.

---

## Language notes

### Comparison

| | JavaScript | Lua |
| --- | --- | --- |
| End of statement | `;` optional | newline, `;` optional |
| Local variable | `let x = 1;` (also `const`, `var`) | `local x = 1` (without `local`: global) |
| Equal / not equal | `===`, `!==` | `==`, `~=` |
| And, or, not | `&&`, `\|\|`, `!` | `and`, `or`, `not` |
| Blocks | `{ ... }` | `then ... end`, `do ... end`, `function ... end` |
| Function | `function f(x) { ... }` | `local function f(x) ... end` |
| First list element | `list[0]` | `list[1]` |
| Length of a list / string | `list.length`, `s.length` | `#list`, `#s` |
| Join text | `"Port " + i` | `"Port " .. i` |
| Number to text | `number.toString(n)`, `String(n)`, `n.toFixed(1)` | `number.toString(n)`, `tostring(n)`, `string.format("%.1f", n)` |
| Comments | `//` and `/* */` | `--` and `--[[ ]]` |
| No value | `null`, `undefined` | `nil` |

In both languages, **the length of a string counts bytes, not characters**: `'Київ'.length === 8` and `('Київ'):len() == 8`.

### JavaScript

The JavaScript engine is [QuickJS](https://bellard.org/quickjs/), a complete ES2020 implementation: `let`/`const`, arrow functions, classes, destructuring, template literals, `switch`, `try`/`catch`, and the full standard library (`Math`, `JSON`, `String`, `Array`, `Date`, `RegExp`, `Number`, ...).

Limits come from the module, not the language:

- Only the MIDI-KIT objects (`rack`, `midi`, `midiOut`, `trig`, `input`, `param`, `number`) are available. There is no `require` or `import`, no `console` (use `rack.log()`), and no file or network access.
- Memory is limited to 1 MiB.

### Lua

The Lua engine is [MiniLua](https://github.com/edubart/minilua), running the full Lua 5.5 language: closures, metatables, `goto`, integer and float numbers, and string patterns (`string.find`, `match`, `gmatch`, `gsub`, `format`). Only the libraries are trimmed to what is safe inside a patch:

- **Available:** the basic functions (`pairs`, `ipairs`, `pcall`, `tostring`, `tonumber`, `select`, `setmetatable`, ...), `math`, `string` and `table`.
- **Not available:** `io`, `os`, `package` / `require`, `debug`, `coroutine` and `utf8`, as well as `dofile`, `loadfile`, `load` and `string.dump`. There is no file or system access.
- **`print` is removed**, because it would write to Rack's console rather than the module log. Use `rack.log()`.
- Memory is limited to 1 MiB.

MIDI-KIT adds two things:

- **`string.split(s, sep [, limit])`**, also callable as `s:split(sep)`, works like JavaScript's `split`: `sep` is plain text, not a pattern. Empty pieces are kept (`("a,,b"):split(",")` gives three pieces), an empty `sep` splits into single bytes, and `limit` caps the number of pieces.
- **`json.encode(value)`** and **`json.decode(text)`** (the bundled [json.lua](https://github.com/rxi/json.lua) by rxi, MIT license). Invalid input raises an error, so wrap `json.decode` in `pcall`. JSON `null` becomes `nil`.

Two Lua 5.5 details that can surprise:

- The variable of a `for` loop is read-only: `for i = 1, n do i = i + 1 end` doesn't compile. Copy it into a local first.
- Numbers only turn into text automatically with `..`. Everywhere else use `tostring(n)` or `number.toString(n)`.

Everything else follows the [Lua 5.5 reference manual](https://www.lua.org/manual/5.5/).

---

## Troubleshooting

**Nothing comes out.**

- MIDI-KIT passes nothing through on its own. Every message must be sent with `midiOut.send()`.
- Is `midi.onMessage` assigned at the top level? A late assignment is never used, see [Assign each callback once](#assign-each-callback-once-at-the-top-level). The log says so at load if it's missing.
- Sending on output 2 to 4, or listening on input 2 to 4? [Enable the ports](#enabling-midi-ports) first.
- An empty handle can't be sent ("message has no status byte"): fill it with a setter first.

**`trig.onTrigger` never fires.** Call `trig.enableIn()` for every input channel you need. If Tipsy is enabled, channel 1 of trigger input 1 no longer triggers, see [Tipsy](#tipsy).

**CCs disappear from `midi.onMessage`.** NRPN, RPN or 14-bit CC assembly is enabled and takes the CCs it is built from, see [Receiving NRPN, RPN and 14-bit CC](#receiving-nrpn-rpn-and-14-bit-cc). Enabling *all* 14-bit CCs takes every controller from 0 to 31, a mod wheel on CC 1 included.

**Notes end up as Note Off.** A Note On with velocity 0 *is* a Note Off, and `midi.getType()` says so, see [Message types](#message-types). The bytes are unchanged when the message is forwarded.

**The log fills with activity you didn't play.** Many keyboards send Active Sensing (`FE`) about every 300 ms for as long as they are connected. A pass-through should forward it, but a script that logs, counts or answers every message should skip `midi.ACTIVE_SENSING`, and usually `midi.CLOCK` as well.

**"message store full".** The callback holds too many messages at once. Reuse one handle for a series of messages, see [Message handles](#message-handles). Only if the script really needs many messages at the same time, raise the limit with [`@requires messages=N`](#requires).

**An error about a handle from another callback.** Handles are only valid in the callback that created or received them. Store the *values* you need (channel, note, ...) and build a new message later.

**An error when creating a message at the top level or in a tooltip.** Messages can only be created in callbacks, see [Message handles](#message-handles).

**A 14-bit CC arrives half.** The five-argument `midi.setCc14bit(msb, lsb, ...)` sends two independent messages. Use `midi.createCc14bit()` and the four-argument form to send the pair as a unit.

**Settings are lost.** Call `rack.setConfig()` when the setting changes, there is no save step. Loading a different script, or a script by file or paste, starts with empty settings. See [Saving settings](#saving-settings).

**A menu checkmark is wrong.** Provide `onGetValue` and return the current value from it. With `[label, value]` options, the returned value must equal one of the option values exactly.

**Notes hang after editing the script.** Send note offs or call `midiOut.panic()` in `rack.onUnload()`, see [Panic](#panic).

**Clock or sequence timing wobbles.** Use [sample-accurate timing](#sample-accurate-timing). If it still wobbles, turn on `midiOut.enableTiming(true)` to see whether messages arrive too late.

**The script stops with "exceeded execution budget" or "interrupted".** A callback (or the top-level code) ran too long, usually an endless loop. See [Limits](#limits).

---

## Limits

Every limit a script can run into, and what happens there. Anything not listed is not limited by MIDI-KIT.

| Area | Limit | At the limit |
| --- | --- | --- |
| Run time of one callback or of the top-level code | about 10 million instructions, in both languages | the call is aborted with "exceeded execution budget" (Lua) or "interrupted" (JavaScript). At the top level the load fails. After a callback, the next one runs normally |
| Memory | 1 MiB per script, in both languages | the script is stopped and "memory limit and was stopped" is logged. Loading a script again starts afresh |
| Message handles at once, per callback | 32 by default, up to 512 with `@requires messages=N`. A received NRPN or RPN takes 4, a 14-bit CC 2 | `midi.create()` and the other constructors raise "message store full". A `@requires` value above 512 refuses the script |
| `sendAfterMs()`, `sendAtFrame()` | at most 2 hours ahead | later times are moved to 2 hours. `NaN` and infinity raise "must be a finite number" and send nothing |
| `sendAfterTrigger()` | at most 10000 ticks ahead | larger counts become 10000. A negative count counts as 0 |
| Output queue | 2048 messages per module, handed on 128 at a time every 8 samples | the message is dropped and logged |
| Scheduled by time | 256 per output (`sendAfterMs()`, `sendAtFrame()`) | the message is sent at once, logged once per script |
| Scheduled by trigger | 32 per trigger input channel (`sendAfterTrigger()`) | the message is sent at once, logged once per script |
| SysEx | 8192 payload bytes (8194 with `f0` / `f7`); `setSysEx()` accepts only bytes `00` to `7f`, `setRaw()` does not check the bytes. That's about 2.6 s on a classic 5-pin MIDI cable | `midi.setSysEx()` and `midi.setRaw()` raise an error. A longer incoming message is dropped whole and "MIDI input: message(s) longer than 8194 bytes dropped" is logged, so whatever a script receives it can also forward or clone |
| Tipsy | 256 bytes of data, MIME type at most 255 characters | nothing is sent, and "Tipsy: invalid parameters" or "Tipsy: mime type too long" is logged. A received stream that is too long is reported as malformed |
| Saved settings | 64 KB in total, values nested at most 4 levels, keys at most 64 characters | the change is rejected and logged |
| Broadcasts | 4 KB per value, topic at most 64 bytes, 16 waiting per receiver | the broadcast is rejected (sender) or dropped (receiver) and logged |
| File read by a `"fileopen"` menu item | 8192 bytes | the user gets a message, `onChange` is not called |
| Timed trigger output writes | 64 waiting | further writes happen at once |
| Identical log lines in a row | 3 | the rest are counted and shown as one "… repeated N×" line, once a different line arrives or the repetition has stopped for half a second. This applies to errors and `rack.log()` alike |

The two scheduling limits count from the same point as the delay itself: the latest frame the module had processed, or with `midiOut.enableTiming()` the frame of the event being handled. A delay of exactly `7200000` ms is not changed. For `sendAtFrame()` the limit is 7200 seconds' worth of frames past that point.

---

## API index

Every function and callback, with the section that explains it.

**`rack`**

| Name | Section |
| --- | --- |
| `rack.log`, `rack.overlay` | [Log and overlay](#log-and-overlay) |
| `rack.getEventFrame`, `rack.msToFrames`, `rack.framesToMs` | [Frames](#frames) |
| `rack.random`, `rack.setRandomSeed` | [Random numbers](#random-numbers) |
| `rack.getConfig`, `rack.setConfig` | [Saving settings](#saving-settings) |
| `rack.registerContextMenu`, `rack.unregisterContextMenu` | [Context menu items](#context-menu-items) |
| `rack.sendBroadcast`, `rack.onBroadcast` | [Messages between modules](#messages-between-modules) |
| `rack.onLoad`, `rack.onUnload` | [The script's lifetime](#the-scripts-lifetime) |

**`midi`**

| Name | Section |
| --- | --- |
| `midi.onMessage` | [Top-level code and callbacks](#top-level-code-and-callbacks) |
| `midi.enablePorts`, `midi.portCount` | [Enabling MIDI ports](#enabling-midi-ports) |
| `midi.create`, `midi.clone` | [Message handles](#message-handles) |
| `midi.getType`, `midi.NOTE_ON`, `midi.CC`, ... | [Message types](#message-types) |
| `midi.getChannel`, `getNote`, `getControl`, `getValue`, `getPitchWheel`, `getProgramChange`, `getChanPressure`, `getSysEx`, `getSysExLength`, `getLength`, `getRaw`, `toString` | [Reading messages](#reading-messages) |
| `midi.setNoteOn`, `setNoteOff`, `setNote`, `setKeyPressure`, `setCc`, `setProgramChange`, `setChanPressure`, `setPitchWheel`, `setChannel`, `setValue`, `setSysEx`, `setRaw` | [Building messages](#building-messages) |
| `midi.createNRPN`, `createRPN`, `createCc14bit`, `setNRPN`, `setRPN`, `setCc14bit` | [Sending a group](#sending-a-group) |
| `midi.enableNrpnIn`, `enableRpnIn`, `enableCc14bitIn`, `onNrpn`, `onRpn`, `onCc14bit` | [Receiving NRPN, RPN and 14-bit CC](#receiving-nrpn-rpn-and-14-bit-cc) |

**`midiOut`**

| Name | Section |
| --- | --- |
| `midiOut.selectPort`, `midiOut.enablePorts`, `midiOut.portCount` | [Output ports](#output-ports) |
| `midiOut.send`, `sendAfterMs`, `sendAtFrame`, `sendAfterTrigger` | [Send now, later, or on a beat](#send-now-later-or-on-a-beat) |
| `midiOut.cancel` | [Cancelling scheduled messages](#cancelling-scheduled-messages) |
| `midiOut.panic` | [Panic](#panic) |
| `midiOut.enableTiming` | [Sample-accurate timing](#sample-accurate-timing) |

**`param`, `input`, `trig`, `number`**

| Name | Section |
| --- | --- |
| `param.enable`, `param.getValue`, `param.count` | [Panel knobs](#panel-knobs) |
| `param.onTooltip`, `param.onValueText`, `input.onTooltip` | [Tooltips and value display](#tooltips-and-value-display) |
| `input.enable`, `input.getVoltage`, `input.isHigh`, `input.isLow`, `input.count` | [CV inputs](#cv-inputs) |
| `trig.enableIn`, `trig.onTrigger`, `trig.getTicks`, `trig.isHigh`, `trig.isLow`, `trig.inCount` | [Trigger inputs](#trigger-inputs) |
| `trig.setTrigger`, `trig.setGate`, `trig.setHigh`, `trig.setLow`, `trig.outCount` | [Trigger outputs](#trigger-outputs) |
| `trig.sendTipsy`, `trig.enableTipsyIn`, `trig.onTipsyMessage` | [Tipsy](#tipsy) |
| `number.rescale`, `number.crossfade`, `number.toString` | [Number helpers](#number-helpers) |
