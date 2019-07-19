# stoermelder MIDI-CAT

MIDI-CAT is an evolution of [VCV's MIDI-MAP](https://vcvrack.com/manual/Core.html#midi-map): It adds midi output for controller feedback and allows parameter mapping of midi note messages. Besides these new features it contains the features known from stoermelder's other mapping modules like scrolling text for long parameter names, "locate and indicate" on the context menu as well as unlocked parameters for parameter changes by hand or by modules like stoermelder's 8FACE.

![MIDI-CAT Intro](./MidiCat-intro.gif)

Midi output will send all parameter changes made with the same CC or note as mapped. When mapping midi notes you get an additional context menu option "Note velocity" for the slot: When disabled (default) the midi velocity value is ignored and a value of 127 is assumed.