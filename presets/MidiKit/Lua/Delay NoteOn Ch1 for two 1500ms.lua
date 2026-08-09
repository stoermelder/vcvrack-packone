--[[
@target stoermelder MIDI-KIT
@engine minilua@v1
@author stoermelder
@description Delays every Note-On message on channel 1 for 1500ms
--]]

midi.onMessage = function(midiPort, msg)
    if midi.isNoteOn(msg) and midi.getChannel(msg) == 1 then
        midiOut.sendAfterMs(msg, 1500)
    end
end