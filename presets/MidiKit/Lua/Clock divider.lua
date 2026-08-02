--[[
@target stoermelder MIDI-KIT
@engine Lua
@author stoermelder
@description Divides incoming MIDI clock, forwarding every Nth tick and emitting a trigger on the trigger output
--]]

-- MIDI clock divider for MIDI-KIT
--
-- MIDI clock runs at a fixed 24 pulses per quarter note (ppqn). Gear that wants
-- a slower clock - a step sequencer running at eighth notes, an arpeggiator on
-- quarters - needs the stream thinned out. This script forwards only every Nth
-- clock tick, so downstream devices still see a valid 0xF8 stream, just slower.
--
-- Start (0xFA), Continue (0xFB) and Stop (0xFC) are always forwarded untouched,
-- and Start/Continue reset the divider phase so the divided clock always lands
-- on the downbeat rather than wherever the previous run left off.
--
-- Because 24 ppqn divides evenly, the useful divisors are musical:
--
--     divisor  1 -> 24 ppqn (no division)
--     divisor  2 -> 8th note triplet feel (12 ppqn)
--     divisor  3 ->  8 pulses per quarter
--     divisor  6 -> one pulse per 16th note
--     divisor 12 -> one pulse per 8th note
--     divisor 24 -> one pulse per quarter note
--
-- The divided clock is also mirrored to the module's trigger output, so it can
-- drive Rack clock inputs directly without a MIDI-to-CV round trip.


-- Configuration - change these values as needed
local config = {
    -- Forward every Nth clock tick (1 = pass everything through)
    divisor = 6,

    -- Also emit a trigger on trigger output 1 for every forwarded tick
    emitTrigger = true,

    -- Trigger output port used when emitTrigger is set
    trigPort = 1,

    -- Show the running pulse count in the panel overlay
    showOverlay = true,

    -- Forward all non-clock messages (notes, CC, ...) unchanged
    passThroughOther = true
}

-- Internal state
local state = {
    tickCount = 0,
    pulseCount = 0,
    running = false
}

rack.onLoad = function()
    rack.log("Clock divider initialized")
    rack.log("Divisor: ", config.divisor, " (24 ppqn / ", config.divisor, ")")
end

local function resetPhase()
    state.tickCount = 0
    state.pulseCount = 0
end

rack.onMidiMessage = function(midiPort, msg)
    if midi.isStart(msg) then
        resetPhase()
        state.running = true
        midiOut.send(msg)
        return
    end

    if midi.isContinue(msg) then
        -- Continue resumes mid-bar, but restarting the phase here keeps the
        -- divided clock aligned to the resume point rather than to a stale count.
        resetPhase()
        state.running = true
        midiOut.send(msg)
        return
    end

    if midi.isStop(msg) then
        state.running = false
        midiOut.send(msg)
        return
    end

    if midi.isClock(msg) then
        state.tickCount = state.tickCount + 1
        if state.tickCount < config.divisor then
            -- Swallowed - this is the division itself
            return
        end
        state.tickCount = 0
        state.pulseCount = state.pulseCount + 1

        midiOut.send(msg)
        if config.emitTrigger then
            trig.setTrigger(config.trigPort)
        end
        if config.showOverlay then
            rack.overlay("Clock /" .. config.divisor, "pulse " .. state.pulseCount)
        end
        return
    end

    if config.passThroughOther then
        midiOut.send(msg)
    end
end
