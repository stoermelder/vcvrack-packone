--[[
@target stoermelder MIDI-KIT
@engine minilua@v1
@author stoermelder
@description Multiplies a clock on trigger input 1 into a MIDI clock (24 ppqn), placing every pulse on its own sample
--]]

-- Clock multiplier for MIDI-KIT
--
-- Turns a clock on trigger input 1 into a 24 ppqn MIDI clock for hardware. A
-- Rack clock usually ticks once per beat (quarter note), where MIDI clock needs
-- 24, so the default multiplier is 24. Gear that syncs to MIDI clock shows output
-- jitter immediately, which makes this the case sample-accurate timing is for.
--
-- midiOut.enableTiming() is what makes it work: with it, every message leaves
-- with an engine frame and Rack's output thread transmits it at that frame,
-- instead of MIDI-KIT writing it from the audio thread at whatever block
-- boundary came next. The price is one audio block of output latency, constant
-- for every pulse, which a clock does not mind.
--
-- How the pulses are placed:
--   - The pulse for the input edge goes out on the edge's own frame: send()
--     defaults to the frame of the event it runs in.
--   - The pulses in between are spaced evenly over the next input period. That
--     period is not known yet, so the previous one is used. A clock that speeds
--     up or slows down needs one period to catch up, as with any clock
--     multiplier. Each pulse is scheduled with sendAtFrame() from the edge's
--     frame, rack.getEventFrame().
--
-- An interval much longer than the previous one (the clock was stopped) is
-- treated as a restart: that edge sends its own pulse only, and the next edge
-- measures the new period.
--
-- The multiplier is the number of MIDI clock pulses sent per input tick. Pick it
-- by what one tick of your clock stands for:
--
--     24  quarter notes (one tick per beat)
--     12  8th notes
--      8  8th triplets
--      6  16th notes
--      4  16th triplets
--      3  32nd notes
--      2  32nd triplets
--      1  the input already runs at 24 ppqn

-- Configuration - change these values as needed
local config = {
    -- MIDI clock pulses per input tick; 24 for a clock ticking once per beat
    ratio = rack.getConfig("ratio", 24)
}

-- Internal state
local state = {
    lastEdge = -1,   -- frame of the previous input edge
    period = 0       -- frames between the last two edges, 0 = not known
}

-- An interval this many times the previous one is a restart, not a slow tempo
local RESTART_FACTOR = 4

local function clockPulse()
    local m = midi.create()
    midi.setRaw(m, "f8")
    return m
end

-- Setup
rack.onLoad = function()
    midiOut.enableTiming()
    trig.enableIn(1)

    -- Context menu - right-click the module to change this setting live.
    rack.registerContextMenu({
        type = "options",
        label = "Multiplier",
        options = { {"1x", 1}, {"2x", 2}, {"3x", 3}, {"4x", 4}, {"6x", 6}, {"8x", 8}, {"12x", 12}, {"24x", 24} },
        onGetValue = function()
            return config.ratio
        end,
        onChange = function(ratio, label)
            config.ratio = ratio
            rack.setConfig("ratio", config.ratio)
            rack.log("Multiplier: ", label)
        end
    })

    rack.log("Clock multiplier initialized")
    rack.log("Multiplier: ", config.ratio, "x")
end

-- Callbacks
trig.onTrigger = function(trigPort, channel)
    local edge = rack.getEventFrame()
    local previous = state.lastEdge
    state.lastEdge = edge

    if previous >= 0 then
        local interval = edge - previous
        if state.period > 0 and interval > RESTART_FACTOR * state.period then
            state.period = 0
        elseif interval > 0 then
            state.period = interval
        end
    end

    -- One pulse message serves all of them: sending copies it.
    local pulse = clockPulse()

    -- The pulse for the edge itself, on the edge's frame
    midiOut.send(pulse)

    -- The pulses up to the next edge, spaced over the previous period
    if state.period > 0 then
        for k = 1, config.ratio - 1 do
            midiOut.sendAtFrame(pulse, edge + math.floor(k * state.period / config.ratio + 0.5))
        end
    end
end
