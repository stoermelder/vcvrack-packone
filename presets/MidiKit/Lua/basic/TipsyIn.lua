--[[
@target stoermelder MIDI-KIT
@engine minilua@v1
@author stoermelder
-- @description Receives Tipsy messages on the trigger input and logs them. JSON payloads are parsed and forwarded as MIDI CC. Pair with Tipsy.lua (or any Tipsy sender) patched into TRIG.
--]]

-- The receiving side of Tipsy: trig.enableTipsyIn() routes trigger input 1 into
-- the Tipsy decoder, and every complete message is delivered to
-- trig.onTipsyMessage(data, mimeType). Patch the output of a Tipsy sender
-- (e.g. Tipsy.lua) into TRIG. Received data is logged and, for a JSON payload with
-- a numeric "value" field, forwarded as a MIDI CC.

rack.onLoad = function()
    -- Claim the trigger input for the Tipsy decoder. While claimed, the
    -- trigger input no longer fires trig.onTrigger or counts ticks.
    trig.enableTipsyIn()
    rack.log("Listening for Tipsy on TRIG")
end

trig.onTipsyMessage = function(data, mimeType)
    rack.log("Tipsy [" .. mimeType .. "] " .. data)

    -- A JSON payload can drive anything the script can reach - here the
    -- "value" field is forwarded as CC 12 on the MIDI output.
    if mimeType == "application/json" then
        local ok, obj = pcall(json.decode, data)
        if not ok then
            rack.log("  (not valid JSON: " .. tostring(obj) .. ")")
            return
        end
        if type(obj) == "table" and type(obj.value) == "number" then
            local msg = midi.create()
            midi.setCc(msg, 1, 12, math.max(0, math.min(127, math.floor(obj.value))))
            midiOut.send(msg)
        end
    end
end
