--[[
@target stoermelder MIDI-KIT
@engine minilua@v1
@author stoermelder
@description Passes all incoming MIDI messages to the default MIDI output port.
--]]

-- Every MIDI-KIT script is built around midi.onMessage(midiPort, msg): it is called
-- once for each incoming message. `msg` is a handle to that message, and
-- midiOut.send(msg) forwards it to the MIDI output. Nothing is sent unless the
-- script sends it, so this is the smallest useful script: input straight to output.

midi.onMessage = function(midiPort, msg)
    midiOut.send(msg)
end