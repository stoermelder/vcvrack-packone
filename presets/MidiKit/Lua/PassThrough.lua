--[[
@target stoermelder MIDI-KIT
@engine Lua
@author stoermelder
@description Passes all incoming MIDI messages to the default MIDI output port.
--]]

function onMidiMessage(midiPort, msg)
    midiOut.send(msg)
end