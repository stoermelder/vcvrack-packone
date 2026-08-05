--[[
@target stoermelder MIDI-KIT
@engine minilua@v1
@author stoermelder
@description Routes incoming CC messages on MIDI channel 1 to a MIDI channel set by parameter 1 on the panel
--]]

param.enable(1)

rack.onMidiMessage = function(midiPort, msg)
    if midi.isCc(msg) and midi.getChannel(msg) == 1 then
        local ch = math.ceil(param.getValue(1) * 16)
        midi.setChannel(msg, ch)
    end
    midiOut.send(msg)
end