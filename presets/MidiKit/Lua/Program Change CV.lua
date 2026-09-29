--[[
@target stoermelder MIDI-KIT
@engine minilua@v1
@author stoermelder
@description Sends a Program Change on each trigger, with the program number selected by V/Oct on CV input 1 (0V = program 0)
--]]

-- CV input 1 is read as V/Oct: 0V = program 0, each semitone (1/12 V) adds
-- one, so C0 = 0, C#0 = 1, ... up to program 127 at about 10.58V. The
-- voltage is sampled when the trigger input (channel 1) fires, and a Program
-- Change for the note it selects is sent - the module has no callback for a
-- changing CV, so the trigger acts as the "send" (sample & hold) clock.
-- Negative voltages select program 0.

-- Configuration - change these values as needed
local config = {
    -- MIDI channel (1-16) the Program Change messages are sent on
    channel = 1
}

input.enable(1)
input.getName = function(port)
    if port == 1 then return "Program (V/Oct)" end
    return ""
end

trig.enableIn(1, 1)
trig.onTrigger = function(trigPort, channel)
    local program = math.floor(input.getVoltage(1) * 12 + 0.5)
    if program < 0 then program = 0 end
    if program > 127 then program = 127 end

    local msg = midi.create()
    midi.setProgramChange(msg, config.channel, program)
    midiOut.send(msg)
end

-- Pass all incoming MIDI through unchanged
midi.onMessage = function(midiPort, msg)
    midiOut.send(msg)
end
