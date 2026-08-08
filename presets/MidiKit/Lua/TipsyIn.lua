--[[
@target stoermelder MIDI-KIT
@engine minilua@v1
@author stoermelder
-- @description Receives Tipsy messages on the trigger input and logs them. Numeric payloads are forwarded as MIDI CC. Pair with Tipsy.lua (or any Tipsy sender) patched into TRIG.
--]]

rack.onLoad = function()
    -- Claim the trigger input for the Tipsy decoder. While claimed, the
    -- trigger input no longer fires trig.onTrigger or counts ticks.
    trig.enableTipsyIn()
    rack.log("Listening for Tipsy on TRIG")
end

trig.onTipsyMessage = function(data, mimeType)
    rack.log("Tipsy [" .. mimeType .. "] " .. data)

    -- A plain numeric payload is forwarded as CC 12 on the MIDI output.
    local value = tonumber(data)
    if value ~= nil then
        if value < 0 then value = 0 end
        if value > 127 then value = 127 end
        local msg = midi.create()
        midi.setCc(msg, 1, 12, math.floor(value))
        midiOut.send(msg)
    end
end
