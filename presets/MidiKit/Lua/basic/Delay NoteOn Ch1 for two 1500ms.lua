--[[
@target stoermelder MIDI-KIT
@engine minilua@v1
@author stoermelder
@description Delays every note (Note-On and Note-Off) on channel 1 for 1500ms
--]]

-- midiOut.sendAfterMs(msg, ms) schedules a message instead of sending it right
-- away. Only notes on channel 1 are delayed - and, since nothing else is sent,
-- everything else is dropped. Note-Offs are delayed with their Note-Ons, so no
-- note hangs; isNoteOff() does not match a Note-On with velocity 0, but isNoteOn()
-- does, so keyboards that release that way are covered too. Add a midiOut.send(msg)
-- for the other messages to let them through immediately.

midi.onMessage = function(midiPort, msg)
    if (midi.isNoteOn(msg) or midi.isNoteOff(msg)) and midi.getChannel(msg) == 1 then
        midiOut.sendAfterMs(msg, 1500)
    end
end