--[[
@target stoermelder MIDI-KIT
@engine minilua@v1
@author stoermelder
@description Euclidean rhythm generator clocked by the trigger input, with steps, fills, note and velocity params
--]]

-- Euclidean rhythm generator for MIDI-KIT
--
-- Steps through a Euclidean (Bjorklund) rhythm on the module's trigger input
-- and fires a MIDI Note-On for every hit. Euclidean rhythms spread a number of
-- hits as evenly as possible across a number of steps - e.g. 5 hits in 8 steps
-- gives the classic [10110110] "Bembe" pattern - which makes them the standard
-- source of syncopated, non-linear percussion and bass gates.
--
-- Every trigger tick advances the rhythm by one step. A hit fires a Note-On
-- that sustains until the next trigger tick (a one-step gate), so the
-- generator is monophonic and adjacent hits retrigger cleanly. Feed any clock
-- into the trigger input (a Rack clock source, or MIDI clock forwarded to a
-- trigger output elsewhere in the patch) and the rhythm follows it.
--
-- param 1 - Steps: length of the bar, 1-16.
-- param 2 - Fills: number of hits in the bar, 0..Steps. 0 silences the rhythm,
--   Steps fills every step.
-- param 3 - Note: MIDI note number fired on each hit, 0-127.
-- param 4 - Velocity: velocity of each hit, 1-127.
--
-- All four params are read live and the pattern is recomputed on every trigger
-- tick, so moving a knob changes the rhythm/note/velocity immediately.
--
-- Everything arriving on MIDI IN is passed through unchanged - this script is
-- a pure generator and does not want to swallow the rest of a MIDI chain.


-- Configuration - change these values as needed
local config = {
    -- Output channel for the generated notes (1-16)
    outChannel = 1
}

-- Internal state.
-- step: the step index played on the next trigger tick (0-based; Lua arrays
--   are 1-based, so the pattern is read at step + 1).
-- pattern: the current Euclidean pattern as 1/0 flags, length = Steps.
-- soundingNote: the note currently sustaining (-1 = nothing), released on the
--   next trigger tick or on unload.
local state = {
    step = -1,
    pattern = {},
    soundingNote = -1,
    soundingChannel = 1
}

param.enable(1)
param.enable(2)
param.enable(3)
param.enable(4)

-- Step the rhythm from trigger channel 1 only: trig.onTrigger fires per poly
-- channel, and trig.enableIn() gates it — enabling just channel 1 means the
-- other channels are ignored.
trig.enableIn(1, 1)

param.getName = function(i)
    if i == 1 then return "Steps" end
    if i == 2 then return "Fills" end
    if i == 3 then return "Note" end
    if i == 4 then return "Velocity" end
    return ""
end

local function stepsParam()
    local s = math.floor(param.getValue(1) * 15 + 0.5) + 1
    if s > 16 then s = 16 end
    return s
end

local function fillsParam()
    local s = stepsParam()
    local f = math.floor(param.getValue(2) * s + 0.5)
    if f < 0 then f = 0 end
    if f > s then f = s end
    return f
end

local function noteParam()
    local n = math.floor(param.getValue(3) * 127 + 0.5)
    if n < 0 then n = 0 end
    if n > 127 then n = 127 end
    return n
end

local function velocityParam()
    local v = math.floor(param.getValue(4) * 126 + 0.5) + 1
    if v < 1 then v = 1 end
    if v > 127 then v = 127 end
    return v
end

-- Note names (sharps only) for the Note param readout.
local NOTE_NAMES = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }

local function noteName(n)
    return NOTE_NAMES[n % 12 + 1] .. (math.floor(n / 12) - 1)
end

param.getValueFormat = function(i)
    if i == 1 then return number.toString(stepsParam()) .. " steps" end
    if i == 2 then return number.toString(fillsParam()) .. " / " .. number.toString(stepsParam()) .. " hits" end
    if i == 3 then return number.toString(noteParam()) .. " (" .. noteName(noteParam()) .. ")" end
    if i == 4 then return number.toString(velocityParam()) end
    return number.toString(param.getValue(i))
end

-- Bjorklund's algorithm: distributes `hits` pulses as evenly as possible
-- among `steps` slots, returning a 1/0 array of length `steps`. This is the
-- canonical source of Euclidean rhythms - see Bjorklund, "The Theory of
-- Rep-Rate Pattern Generation in Synchronous Systems" (2003). 0 hits yields
-- all rests and hits == steps yields all hits.
local function euclid(steps, hits)
    if hits > steps then hits = steps end
    if hits == 0 then
        local r = {}
        for i = 1, steps do r[i] = 0 end
        return r
    end
    if hits == steps then
        local r = {}
        for i = 1, steps do r[i] = 1 end
        return r
    end

    -- Euclidean algorithm on (steps - hits) and hits, recording the quotient
    -- and remainder at each level, then unwinding them into the pattern.
    -- `level` stays 0-based; counts/remainders are indexed level + 1.
    local counts = {}
    local remainders = {}
    local divisor = steps - hits
    remainders[1] = hits
    local level = 0
    while true do
        counts[level + 1] = math.floor(divisor / remainders[level + 1])
        remainders[level + 2] = divisor % remainders[level + 1]
        divisor = remainders[level + 1]
        level = level + 1
        if remainders[level + 1] <= 1 then break end
    end
    counts[level + 1] = divisor

    local pattern = {}
    local function build(l)
        if l == -1 then
            pattern[#pattern + 1] = 0
        elseif l == -2 then
            pattern[#pattern + 1] = 1
        else
            for i = 1, counts[l + 1] do build(l - 1) end
            if remainders[l + 1] ~= 0 then build(l - 2) end
        end
    end
    build(level)
    return pattern
end

local function rebuildPattern()
    state.pattern = euclid(stepsParam(), fillsParam())
end

local function releaseSounding()
    if state.soundingNote >= 0 then
        local off = midi.create()
        midi.setNoteOff(off, state.soundingChannel, state.soundingNote)
        midiOut.send(off)
        state.soundingNote = -1
    end
end

rack.onLoad = function()
    rack.log("Euclidean rhythm generator initialized")
end

rack.onUnload = function()
    releaseSounding()
end

-- Context menu - right-click the module to change these settings live.
local CHANNEL_LABELS = {}
for c = 1, 16 do CHANNEL_LABELS[c] = tostring(c) end

rack.registerContextMenu({
    type = "options",
    label = "Output channel",
    options = CHANNEL_LABELS,
    onGetValue = function()
        return config.outChannel - 1
    end,
    onChange = function(idx)
        config.outChannel = idx + 1
        rack.log("Output channel: ", config.outChannel)
    end
})

midi.onMessage = function(midiPort, msg)
    -- Pure generator - pass everything from MIDI IN through unchanged so the
    -- module stays transparent in a MIDI chain.
    midiOut.send(msg)
end

trig.onTrigger = function(trigPort, channel)
    rebuildPattern()

    local steps = #state.pattern
    if steps == 0 then return end

    -- Advance one step, wrapping at the bar end.
    state.step = (state.step + 1) % steps

    -- Cut the previous hit's note (one-step gate) before sounding the next.
    releaseSounding()

    if state.pattern[state.step + 1] == 1 then
        local ch = config.outChannel
        local note = noteParam()
        local on = midi.create()
        midi.setNoteOn(on, ch, note, velocityParam())
        midiOut.send(on)
        state.soundingNote = note
        state.soundingChannel = ch
    end
end
