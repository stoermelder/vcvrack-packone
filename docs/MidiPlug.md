# stoermelder MIDI-PLUG

MIDI-PLUG is an utility module for MIDI-routing: It allows merging messages of different MIDI ports, filtering MIDI messages by channel and duplicating messages to two different output ports. 

![MIDI-PLUG intro](./MidiPlug-intro.png)

Each of the two output (lower section) ports can be configured in two different operating modes:

- "Thru" sends every MIDI message untouched to the MIDI device.

- The selection of a specific MIDI channel makes three sub-modes available:

  - "Replace" replaces the MIDI channel of all messages with the selected channel.
  - "Filter" filters to the selected MIDI channel, all other MIDI channels are ignored.
  - "Block" blocks the selected MIDI channel, all other MIDI channels pass through.

System messages pass through unaffected in every case. Please note that the input ports (upper section) also allow MIDI channel filtering before routed to the output ports.

MIDI-PLUG was added in v1.9 of PackOne.