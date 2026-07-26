--[[
@target stoermelder MIDI-KIT
@engine Lua
@author stoermelder
@description Routes incoming CC messages on MIDI channel 1 to a MIDI channel set by parameter 1 on the panel
--]]

param.enable(1)

function processMidi(midiPort, msg)
    if midi.isCc(msg) and midi.getChannel(msg) == 1 then
        local ch = number.ceil(param.getValue(1) * 16)
        midi.setChannel(msg, ch)
    end
    midiOut.send(msg)
end