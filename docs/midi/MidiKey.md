# stoermelder MIDI-KEY

MIDI-KEY is a utility module for converting MIDI messages from your MIDI controller to keyboard events. It allows triggering hotkeys directly from MIDI.

- Assign MIDI notes or control changes to any keyboard key (e.g. map a MIDI note to the Spacebar or Enter key).
- Modifier-key are supported (Ctrl, Alt, Shift).
- Visual feedback: The module highlights active mappings for clarity.
- Save and load presets: Store your mappings for reuse across projects.

## Changelog

- v2.0.0
    - Initial release
- v2.0.1
    - Fixed crash on mapped special keys like ENTER
- v2.3.0
    - Fixed occasional crash in browser preview
- v2.6.0
    - Ignore incoming MIDI messages while module is bypassed