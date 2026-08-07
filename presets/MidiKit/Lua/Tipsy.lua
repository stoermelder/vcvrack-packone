--[[
@target stoermelder MIDI-KIT
@engine minilua@v1
@author stoermelder
@description Sends a text message via the Tipsy protocol on the trigger output whenever a trigger is received on the trigger input. Connect the trigger output to a module with Tipsy support (e.g. Transit).
--]]

rack.onTrigger = function(trigPort, channel)
    -- One message per trigger pulse, clocked by channel 1 only.
    if channel ~= 1 then return end

    -- Send a text message; the mime type defaults to "text/plain"
    trig.sendTipsy("Hello Tipsy!")

    -- Send with an explicit mime type (e.g. JSON data)
    -- local config = '{"label":"My snapshot","value":42}'
    -- trig.sendTipsy(config, "application/json")
end
