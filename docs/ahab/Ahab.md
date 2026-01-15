# stoermelder AHAB

AHAB is a port of the [ORCA-C](https://github.com/hundredrabbits/Orca-c) project into a VCV Rack module.

AHAB is a live, grid-based sequencer/patcher for creating patterns, rhythms and MIDI/CV output by typing simple characters into an on-panel field. AHAB lets you create live-running patterns by editing a small grid.

- Type to place characters in the field and build rhythmic or melodic patterns. 
- Use the Run button or the spacebar to start and stop the engine. 
- The module can output both sync pulses and musical data you can route to synths or other modules.

Here are some resources to get you started with live-coding using AHAB/ORCA:

- https://metasyn.srht.site/learn-orca
- https://100r.co/media/content/projects/zine_orca.png
- https://100r.co/site/orca.html
- https://github.com/hundredrabbits/Orca

### MIDI Routing

The primary way to get musical data out of AHAB is via MIDI. AHAB can send MIDI to both to one of four virtual ports and one driver output. The virtual ports must be enabled on the context menu before they can be used in other MIDI-capable modules.

### CV operators '<' and '>'

AHAB provides two additional operators that let your patterns interact with the module's CV jacks:

- **'<'** (read) — Use '<' to read values from the module's 4 input jacks.
  - Use digits for ports (1–4) to read numeric control values
  - Use letters for ports (a–d) to read pitch-style signals (handy for notes and V/Oct control).

- **'>'** (write) — Use '>' to send values from the pattern to the module's 4 output jacks.
  - Use The numeric ports (1–4) provide stepped control voltages useful for gates, triggers or stepped CV.
  - Use letters for ports (a–d) to write pitch voltages suitable for V/Oct synth inputs.

### Feature Comparison

AHAB has some features not found in the original ORCA/ORCA-C:

- The simulation can be triggered by an external clock signal, using the _Clock In_ port. This allows you to drive the simulation even by non-evenly spaced clock sources.

- The copy/paste clipboard is shared across all AHAB instances in your Rack. You can copy/cut from one AHAB module and paste into another. You can save the content of the clipboard to a text file and save it as an ORCA file.

- Operators '<' and '>' let you read from and write to the module's CV ports.

- The usable MIDI CC range can be configured in the context menu, allowing you to use a range different from 64-99, as in ORCA-C.

- If tooltips are enabled in Rack's settings, hovering over operators will show a brief description and their input/output configuration.

Features missing in AHAB compared to ORCA:

- The operator $ (commander) is not available, as it is not in ORCA-C.

- Hotkey Ctrl/Cmd+P to trigger the operator at the cursor is not available, as ORCA-C does not support this feature.

### Keyboard Shortcuts

| Shortcut | Action |
|---|---|
| Space | Toggle run/stop |
| Backspace | Clear selected cells |
| Ctrl/Cmd + Z | Undo |
| Ctrl/Cmd + Shift + Z | Redo |
| Ctrl/Cmd + N | Clear |
| Ctrl/Cmd + O | Load file |
| Ctrl/Cmd + B | Inject file |
| Ctrl/Cmd + I | Toggle insert mode (cursor moves right) |
| Ctrl/Cmd + A | Select all |
| Ctrl/Cmd + C | Copy selected cells to clipboard |
| Ctrl/Cmd + X | Cut selected cells to clipboard |
| Ctrl/Cmd + V | Paste selected cells from clipboard |
| Ctrl/Cmd + F | Run simulation for one frame |
| Ctrl/Cmd + Shift + 7 | Toggle comment block |
| Ctrl/Cmd + Shift + R | Reset frame number to zero |
| Arrow keys | Move cursor / Move selection |
| Shift + Arrow keys | Expand selection |
| Alt + Arrow keys | Move selected cells |
| Ctrl/Cmd + Arrow keys | Move/expand in grid cells |
| Escape | Clear selection |
| {, } | Decrease/increase grid step rows |
| [, ] | Decrease/increase grid step columns |

## Changelog

- v2.3.0
    - Initial release of AHAB
