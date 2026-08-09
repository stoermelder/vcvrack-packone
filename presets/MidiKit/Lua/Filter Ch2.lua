--[[
@target stoermelder MIDI-KIT
@engine minilua@v1
@author stoermelder
@description Drops all incoming MIDI messages except for MIDI channel 2
--]]

midi.onMessage = function(midiPort, msg)
    if midi.getChannel(msg) == 2 then
        midiOut.send(msg)
    end
end