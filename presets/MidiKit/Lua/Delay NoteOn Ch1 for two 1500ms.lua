--[[
@target stoermelder MIDI-KIT
@engine Lua
@author stoermelder
@description Delays every Note-On message on channel 1 for 1500ms
--]]

function processMidi(midiInput, msg)
    if midi.isNoteOn(msg) and midi.getChannel(msg) == 1 then
        midiOut.sendAfterMs(msg, 1500)
    end
end