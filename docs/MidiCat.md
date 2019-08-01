# stoermelder MIDI-CAT

MIDI-CAT is an evolution of [VCV's MIDI-MAP](https://vcvrack.com/manual/Core.html#midi-map): It adds midi output for controller feedback and allows parameter mapping to midi note messages. Besides these new features the module has features known from stoermelder's other mapping modules like scrolling text for long parameter names, "locate and indicate" on the context menu as well as unlocking parameters for changes by hand or by modules like stoermelder's 8FACE.

![MIDI-CAT Intro](./MidiCat-intro.gif)

All parameter changes are sent to midi output with the same CC or note as mapped. Note-mapping (instead of CC) enables an additional context menu option "Note velocity" for the mapping slot: When disabled (default) the midi velocity value is ignored and a value of 127 is assumed.

The module allows you to import presets from VCV MIDI-MAP for a quick migration.

This module was added in v1.1.0 of PackOne.