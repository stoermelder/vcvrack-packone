# stoermelder MIDI-KIT

MIDI-KIT is a scripting module for altering, filtering, delaying, or generating MIDI messages. It bundles two scripting engines — a full JavaScript engine (QuickJS) and a small subset of Lua — so you can pick whichever language you are more comfortable with.

## How it works

MIDI-KIT provides two interchangeable scripting engines. Both expose the same `midi` / `midiOut` / `input` / `trig` / `param` / `number` / `rack` API described further down, so the only thing that differs between them is the language syntax and a few language-specific details.

| Engine | Language | Underlying interpreter |
| ------ | -------- | ---------------------- |
| **JavaScript** | Full JavaScript (ES2020) | [QuickJS](https://bellard.org/quickjs/) |
| **Lua**        | A small subset of Lua 5.x    | [MiniLua](https://github.com/edubart/minilua) (bundled) |

Neither engine is optimized for raw performance, but MIDI events are typically sparse compared to audio/DSP processing and the engines are adequate for most MIDI scripting tasks.

The active engine is chosen by a header tag at the top of the script.

JavaScript:
```
/**
 * @engine QuickJs@v1
 */
```
(`@engine` is mandatory — there is no default engine; a script without a matching tag is ignored.)

Lua:
```
--[[
@engine minilua@v1
--]]
```

The header is parsed line-by-line and may also be used to set `@author` and `@description` metadata, which is shown in the module's log on load.

MIDI-KIT is event-driven: it runs only when a MIDI message arrives on the selected MIDI input, a trigger arrives on the CV trigger input (`trig.onTrigger`, once enabled with `trig.enableIn()`), or a [Tipsy](SCRIPTING.md#trig-dedicated-triggergate-ports) message finishes decoding on the trigger input (`trig.onTipsyMessage`). The scripting API lets you create new MIDI messages; a single incoming event may result in up to 32 outgoing messages (the message-handle store holds 32 handles per callback — creating more raises a script error, see [midi](SCRIPTING.md#midi-message-constructioninspection)). Incoming MIDI messages are not passed through automatically — scripts must explicitly call `midiOut.send()` to forward messages.

The module also exposes four CV inputs and four panel parameters that can be read from scripts to add modulation or dynamic configuration.

Scripts can persist their configuration across patch saves and reloads via `rack.onLoad()` and `rack.onSave()` (see [Persistence](SCRIPTING.md#persistence)).

`midi.onMessage` (on the `midi` object), `rack.onLoad`/`rack.onUnload`/`rack.onSave`, and `trig.onTrigger`/`trig.onTipsyMessage` (on the `trig` object) are only ever read once, right after the script loads — assign each exactly once, at the top level. Reassigning one later, or defining it late, has no effect (see [Hooks and predefined objects are resolved once, at load time](SCRIPTING.md#hooks-and-predefined-objects-are-resolved-once-at-load-time)). `trig.enableIn()`, by contrast, is a live API call — the trigger input does nothing until it is called.

You can use MIDI-KIT as an insert effect via VCV Rack's built-in MIDI Loopback driver. This lets you process incoming messages before they reach other MIDI modules (for example, MIDI‑CC, MIDI‑CV, MIDI‑MAP, or MIDI‑CAT), and likewise process outgoing messages.

A complete list of the scripting API and worked examples is in [SCRIPTING.md](SCRIPTING.md).

## Examples

Worked scripts for every common task — pass-through, filtering, channel
routing, building NRPN/14-bit CC/SysEx messages, clocking from the trigger
input, Tipsy in/out, and adding context-menu items — are in
[SCRIPTING.md](SCRIPTING.md). The shipped example presets (loadable from
the module's context menu) are the same scripts on disk under
`presets/MidiKit/`.

## Language reference

MIDI-KIT supports two scripting languages. The JavaScript engine is [QuickJS](https://bellard.org/quickjs/) (a full ES2020 engine); the Lua engine is a bundled [MiniLua](https://github.com/edubart/minilua). QuickJS ships with the full standard JavaScript library; MiniLua is trimmed to a safe subset. The `midi` / `midiOut` / `input` / `trig` / `param` / `number` / `rack` API is identical across the two engines, so picking an engine is mostly a matter of personal taste.

### Quick comparison

| | JavaScript (QuickJS) | Lua (MiniLua) |
| - | ---------------- | ------------- |
| Statement terminator | `;` optional (ASI) | newline / `;` (no `;` required) |
| Variable declaration | `let x = 1;` (also `var`, `const`) | `local x = 1` (globals are unprefixed) |
| Strict equality | `===`, `!==` (also `==`, `!=`) | `==`, `~=` (no implicit conversion in either) |
| Logical operators | `&&`, `||`, `!` | `and`, `or`, `not` |
| Block keyword | `{ }` | `do ... end` / `function ... end` |
| Function definition | `function f(x) { ... }` or `let f = function(x) { ... };` | `local f = function(x) ... end` |
| String length | counts UTF-8 **bytes** | counts **bytes** as well |
| Implicit number→string | **yes** in `+` concatenation | **yes** for `..` concatenation; use `tostring(n)` elsewhere |
| Comments | `//` and `/* */` | `--` and `--[[ ]]` |
| Header convention | `/** ... @engine QuickJs@v1 ... */` | `--[[ ... @engine minilua@v1 ... --]]` |

### JavaScript (QuickJS)

QuickJS is a full JavaScript engine (ES2020), so all standard JavaScript syntax and the standard library are available: `function` declarations, `while`/`do`/`for` loops, `switch`, `try`/`catch`/`throw`, `class`, `new`, `this`, `var`/`let`/`const`, arrow functions, destructuring, template literals, and `Math`/`JSON`/`String`/`Array`/`Date`/`RegExp`/`Number`. There are no scriptlet subset restrictions to work around.

The only constraints come from the module, not the language:

- A **1 MiB memory limit** on the QuickJS heap.
- The script runs in a **sandboxed API**: only the globals documented here (`rack`, `number`, `input`, `trig`, `param`, `midi`, `midiOut`) are available. There is no `require`/`import`, no `console`, and no file or network access.

Strings are binary data chunks; their length counts bytes rather than Unicode code points: `'Київ'.length === 8`. Numbers are ordinary JavaScript numbers; `+` concatenation auto-coerces numbers to strings, and `number.toString(n)` is also available for explicit conversion.

### Lua (MiniLua)

#### Supported features

- Numeric and string literals: `12`, `12.3`, `"hello"`, `'hello'`, `true`, `false`, `nil`
- Arithmetic: `+`, `-`, `*`, `/`, `%`, unary `-`
- Comparisons: `==`, `~=`, `<`, `<=`, `>`, `>=` (no implicit conversion)
- Logical operators: `and`, `or`, `not` (short-circuit)
- String concatenation: `..` — e.g. `'Port ' .. i`
- Local variables: `local x = 1`
- Tables (associative + array) with index access: `t.k`, `t["k"]`, `t[1]`
- Functions: `local f = function(x) return x * 2 end` and `function f(x) ... end`
- Numeric and generic `for` loops:
  ```
  for i = 1, 10 do ... end             -- numeric
  for k, v in pairs(t) do ... end       -- generic
  ```
- `if ... then ... else ... end` (also `elseif`)
- `repeat ... until cond`
- Multi-return values; `tostring(n)`, `tonumber(s)`, `math.*` (a subset of the standard `math` library is available)
- `pcall` for protected calls
- Strings are binary data chunks: their length counts bytes, not Unicode code points — `'Київ':len() == 8`.

#### Not supported features

- No `goto` and no labels
- No pattern matching with captures (only plain `string.find` is exposed)
- No `os`, `io`, `package`, `require` or `debug` libraries — the engine only opens the safe subset (`_G`, `math`, `string`, `table`) needed to host user scripts
- No coroutines (`coroutine.*`)
- No metamethods / metatables beyond what `string` and `table` need internally

There is no implicit number-to-string coercion outside `..` concatenation — numbers are auto-converted to strings in `..` (e.g. `'Port ' .. i`); everywhere else use `tostring(n)` or the MIDI-KIT helper `number.toString(n)`. For everything else, the standard Lua 5.x semantics apply; please refer to the [Lua reference manual](https://www.lua.org/manual/5.4/) for details.

## Settings

The module's panel and right-click context menu are laid out as follows.

**Panel**

- A **MIDI input** device selector and a **MIDI output** device selector.
- A text display that serves as the **script editor** (type or paste the
  script directly into it) and also shows the module's **log** — `rack.log()`
  output and script load/error messages.
- Four CV **inputs** and four panel **parameters**, readable from scripts via
  `input.*` and `param.*`.
- One CV **trigger input** and one **trigger output**, driven by the script's
  `trig.*` functions.

**Right-click context menu**

- When a script is running, a **"Running Script (Lua)"** or
  **"Running Script (QuickJs)"** label showing the engine and its current
  **RAM usage**.
- A **"Script"** submenu to load example scripts — **"Examples (JavaScript)"**
  and **"Examples (Lua)"** submenus — plus **Clear**, **Paste from
  clipboard**, **Copy to clipboard**, **Load** (from a file dialog),
  **Reload**, and **Save as**.
- Any context-menu items the loaded script registers via
  `rack.registerContextMenu()`.

The script and the config it persists via `rack.onSave()` are stored with the
patch — they are saved and restored with the module's JSON data.

## API surface

The scripting API is identical in both engines (JavaScript and Lua). This is
a concise overview of the API groups; full documentation of every function
and the worked examples are in [SCRIPTING.md](SCRIPTING.md).

| Group | What it does | Details |
| --- | --- | --- |
| `midi.*` | message construction/inspection: `onMessage`, the assembled-input callbacks `onNrpn`/`onRpn`/`onCc14bit` (enabled with `enableNrpnIn`/`enableRpnIn`/`enableCc14bitIn`), constructors `create`/`createNRPN`/`createCc14bit`/`clone`, getters (`getChannel`, `getControl`, `getValue`, …), type predicates (`isCc`, `isNrpn`, …), and setters (`setCc`, `setCc14bit`, `setNRPN`, …) | [midi.*](SCRIPTING.md#midi-message-constructioninspection) |
| `midiOut.*` | sending on the selected output port: `selectPort`, `send`, `sendAfterMs`, `sendAfterTrigger` | [midiOut.*](SCRIPTING.md#midiout-sending) |
| `input.*` | read the module's CV inputs: `enable`, `getVoltage`, `isHigh`, `isLow`, `getName` | [input.*](SCRIPTING.md#input-cv-inputs-on-the-module-1-based) |
| `trig.*` | the dedicated trigger/gate ports: `enableIn`, `onTrigger`, `onTipsyMessage`, `getTicks`, `isHigh`, `isLow`, `setGate`/`setHigh`/`setLow`/`setTrigger`, `sendTipsy`, `enableTipsyIn` | [trig.*](SCRIPTING.md#trig-dedicated-triggergate-ports) |
| `param.*` | read the module's panel parameters: `enable`, `getValue`, `getName`, `getValueFormat` | [param.*](SCRIPTING.md#param-panel-knobs) |
| `number.*` | numeric helpers: `rescale`, `crossfade`, `toString` | [number.*](SCRIPTING.md#number) |
| `rack.*` | module services: `log`, `overlay`, `getFrame`, `random`, `registerContextMenu`, and the `onLoad`/`onUnload`/`onSave` hooks | [rack.*](SCRIPTING.md#rack) · [Persistence](SCRIPTING.md#persistence) |

Full documentation of every function and the scripting examples are in
[SCRIPTING.md](SCRIPTING.md).

## Changelog

- v2.x.x
   - Initial release of MIDI-KIT