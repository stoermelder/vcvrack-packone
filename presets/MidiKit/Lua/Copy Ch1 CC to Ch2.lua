--[[
@target stoermelder MIDI-KIT
@engine minilua@v1
@author stoermelder
@description Duplicates all CC messages on channel 1 on channel 2
--]]

midi.onMessage = function(midiPort, msg)
    if midi.getChannel(msg) == 1 then
        if midi.isCc(msg) then
            local msg2 = midi.create()
            midi.setCc(msg2, 2, midi.getControl(msg), midi.getValue(msg))
            midiOut.send(msg2)
        end
    end
    midiOut.send(msg)
end