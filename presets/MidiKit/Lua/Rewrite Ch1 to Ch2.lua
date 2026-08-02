--[[
@target stoermelder MIDI-KIT
@engine Lua
@author stoermelder
@description Rewrites all MIDI messages on channel 1 to channel 2
--]]

rack.onMidiMessage = function(midiPort, msg)
    if midi.getChannel(msg) == 1 then
        midi.setChannel(msg, 2)
    end
    midiOut.selectPort(1)
    midiOut.send(msg)
end