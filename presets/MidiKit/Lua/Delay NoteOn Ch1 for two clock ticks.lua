--[[
@target stoermelder MIDI-KIT
@engine minilua@v1
@author stoermelder
@description Delays every Note-On message on channel 1 for two clock ticks on the clock input
--]]

rack.onMidiMessage = function(midiPort, msg)
    if midi.isNoteOn(msg) then
        if midi.getChannel(msg) == 1 then
            midiOut.sendAfterTrigger(msg, 2)
        end
    end
end