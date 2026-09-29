--[[
@target stoermelder MIDI-KIT
@engine minilua@v1
@author stoermelder
@description Sends a Program Change message for each trigger on the four trigger channels, with a configurable program number per channel
--]]

-- MIDI-KIT has a single polyphonic trigger input; its first four poly
-- channels act as the four triggers here. A rising edge on channel N sends
-- a Program Change with the program number configured for N.
-- Feed the input with a poly cable (e.g. from a merge module) - a mono
-- cable only drives channel 1.

-- Configuration - change these values as needed
local config = {
    -- MIDI channel (1-16) the Program Change messages are sent on
    channel = 1,

    -- Program numbers (0-127) sent by trigger channel 1, 2, 3 and 4
    programs = { 0, 1, 2, 3 }
}

-- trig.onTrigger is only called for enabled trigger channels
for ch = 1, #config.programs do
    trig.enableIn(1, ch)
end

trig.onTrigger = function(trigPort, channel)
    local program = config.programs[channel]
    if program == nil then return end

    local msg = midi.create()
    midi.setProgramChange(msg, config.channel, program)
    midiOut.send(msg)
end

-- Pass all incoming MIDI through unchanged
midi.onMessage = function(midiPort, msg)
    midiOut.send(msg)
end
