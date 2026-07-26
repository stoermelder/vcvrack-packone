--[[
@target stoermelder MIDI-KIT
@engine Lua
@author stoermelder
@description Drops all incoming MIDI messages except for MIDI channel 2
--]]

function processMidi(midiPort, msg)
    if midi.getChannel(msg) == 2 then
        midiOut.send(msg)
    end
end