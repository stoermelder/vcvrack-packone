--[[
@target stoermelder MIDI-KIT
@engine Lua
@author stoermelder
@description Snaps incoming notes to the nearest note of a selectable scale, tracking held notes so releases still match
--]]

-- Scale quantiser for MIDI-KIT
--
-- Forces every incoming note into a scale, so a keyboard (or a random source)
-- can only ever produce notes that fit the key. Notes already in the scale pass
-- through unchanged; notes outside it are moved to the nearest scale degree.
--
-- The catch that makes this more than a one-liner: a quantised Note-On is sent
-- as a *different* note number than the one played, so the Note-Off that
-- arrives later - carrying the original number - would fail to release it and
-- leave a hanging voice. This script remembers the substitution per note in
-- state.playedAs and rewrites the Note-Off to match.
--
-- The scale is a list of semitone offsets from the root, in the octave
-- 0..11. Several common scales are pre-defined below; point config.scale at
-- whichever one you want, or write your own list.


-- Scale definitions - semitone offsets from the root note
local scales = {
    chromatic  = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 },
    major      = { 0, 2, 4, 5, 7, 9, 11 },
    minor      = { 0, 2, 3, 5, 7, 8, 10 },
    harmonic   = { 0, 2, 3, 5, 7, 8, 11 },
    dorian     = { 0, 2, 3, 5, 7, 9, 10 },
    phrygian   = { 0, 1, 3, 5, 7, 8, 10 },
    lydian     = { 0, 2, 4, 6, 7, 9, 11 },
    mixolydian = { 0, 2, 4, 5, 7, 9, 10 },
    pentatonic = { 0, 2, 4, 7, 9 },
    minorPenta = { 0, 3, 5, 7, 10 },
    blues      = { 0, 3, 5, 6, 7, 10 },
    wholeTone  = { 0, 2, 4, 6, 8, 10 }
}

-- Configuration - change these values as needed
local config = {
    -- Root note of the scale, as a pitch class: 0 = C, 1 = C#, ... 11 = B
    root = 0,

    -- Which scale to snap to - pick any list from `scales` above
    scale = scales.minor,

    -- Only quantise this channel; 0 = every channel
    channel = 0,

    -- When a note sits exactly between two scale degrees, round up instead of down
    preferUpward = false,

    -- Show each substitution in the panel overlay
    showOverlay = true
}

-- Internal state.
-- playedAs[n] is the note number actually sent for incoming note n, so the
-- matching Note-Off can be rewritten the same way.
local state = {
    playedAs = {}
}

local noteNames = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }

-- noteNames is 1-based like every Lua table, but pitch classes are 0-based
local function noteName(note)
    return noteNames[(note % 12) + 1]
end

local function init()
    for n = 0, 127 do
        state.playedAs[n] = -1
    end
    log("Scale quantiser initialized")
    log("Root: " .. noteName(config.root))
    log("Scale degrees: " .. #config.scale)
end

local function matchesChannel(ch)
    return config.channel == 0 or ch == config.channel
end

-- Snaps a note number to the nearest member of the configured scale.
-- Works in pitch-class space, then puts the octave back, so the search only
-- ever has to look one octave up and down.
local function quantise(note)
    -- Distance above the root, folded into 0..11.
    -- Lua's % already returns a non-negative result for a positive divisor.
    local rel = (note - config.root) % 12
    local octaveBase = note - rel

    local best = config.scale[1]
    local bestDist = 127
    local bestUp = false

    for i = 1, #config.scale do
        local degree = config.scale[i]

        -- Check the degree in this octave and in the one above, so a note just
        -- below the root snaps up to the root rather than down a whole octave.
        for o = 0, 1 do
            local candidate = degree + o * 12
            local dist = math.abs(candidate - rel)
            local up = candidate >= rel

            -- Strictly closer always wins. On an exact tie - the note sits
            -- midway between two degrees - config.preferUpward decides, which
            -- is the only case where the choice is arbitrary.
            local better = false
            if dist < bestDist then
                better = true
            elseif dist == bestDist and up ~= bestUp then
                better = (config.preferUpward == up)
            end

            if better then
                bestDist = dist
                best = candidate
                bestUp = up
            end
        end
    end

    local out = octaveBase + best
    -- A snap upward at the very top of the range could exceed 127; drop an
    -- octave rather than emit an invalid note byte.
    while out > 127 do
        out = out - 12
    end
    while out < 0 do
        out = out + 12
    end
    return out
end

function processMidi(midiPort, msg)
    if not matchesChannel(midi.getChannel(msg)) then
        midiOut.send(msg)
        return
    end

    if midi.isNoteOn(msg) then
        local note = midi.getNote(msg)
        local snapped = quantise(note)
        state.playedAs[note] = snapped

        midi.setNote(msg, snapped)
        midiOut.send(msg)

        if config.showOverlay and snapped ~= note then
            overlay("Quantise", noteName(note) .. " -> " .. noteName(snapped))
        end
        return
    end

    if midi.isNoteOff(msg) then
        local note = midi.getNote(msg)
        -- Release whatever was actually sent for this key. If the note was
        -- never seen (script loaded mid-chord), fall back to the raw number.
        local sent = state.playedAs[note]
        if sent >= 0 then
            midi.setNote(msg, sent)
            state.playedAs[note] = -1
        end
        midiOut.send(msg)
        return
    end

    midiOut.send(msg)
end

-- Initialize when script loads
init()
