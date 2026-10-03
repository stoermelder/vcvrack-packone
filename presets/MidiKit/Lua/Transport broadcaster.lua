--[[
@target stoermelder MIDI-KIT
@engine minilua@v1
@author stoermelder
@description Broadcasts a clock on trigger input 1, plus start and stop from the context menu, to other MIDI-KIT modules running "Transport follower"
--]]

-- Transport broadcaster for MIDI-KIT
--
-- Sends transport events to every other MIDI-KIT module whose script defines
-- rack.onBroadcast, without a cable: a "clock" message on every pulse of trigger
-- input 1, and "start"/"stop" from the "Running" item in the context menu.
-- Unloading a running script sends "stop". Pair it with "Transport follower",
-- which turns the messages into MIDI clock, start and stop.
--
-- The second argument is the topic. A receiver gets every broadcast and tells
-- them apart by the topic it is passed as the second argument of onBroadcast.


local running = false

local function setRunning(value)
    running = value
    local state = running and "start" or "stop"
    local n = rack.sendBroadcast({ state = state }, "transport")

    rack.log("Transport " .. state .. " sent to " .. number.toString(n) .. " module(s)")
end

function rack.onLoad()
    trig.enableIn(1)

    rack.registerContextMenu({
        type = "boolean",
        label = "Running",
        onGetValue = function() return running end,
        onChange = function(checked) setRunning(checked) end
    })
end

-- A follower must not keep running after this script is gone.
function rack.onUnload()
    if running then setRunning(false) end
end

function trig.onTrigger(trigPort, channel)
    rack.sendBroadcast({}, "clock")
end
