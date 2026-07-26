--[[
@target stoermelder MIDI-KIT
@engine Lua
@author stoermelder
@description Duplicates all CC messages on channel 1 on channel 2
--]]

function processMidi(midiPort, msg)
    if midi.getChannel(msg) == 1 then
        if midi.isCc(msg) then
            local msg2 = midi.create()
            midi.setCc(msg2, 2, midi.getNote(msg), midi.getValue(msg))
            midiOut.send(msg2)
        end
    end
    midiOut.send(msg)
end