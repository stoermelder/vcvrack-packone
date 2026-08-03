--[[
@target stoermelder MIDI-KIT
@engine Lua
@author stoermelder
@description Snaps incoming notes to the nearest note of a selectable scale, with the root set by CV input 1 (1V/oct); tracks held notes so releases still match
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
--
-- The root of the scale is not a fixed setting: it is read live from CV
-- input 1 using the standard VCV pitch convention (0V = C, +1V = one octave),
-- so the key can be transposed by a pitch CV or a sequencer. Leave input 1
-- unpatched for a C root.


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
    -- Which scale to snap to - pick any list from `scales` above
    scale = scales.minor,

    -- Only quantise this channel; 0 = every channel
    channel = 0,

    -- When a note sits exactly between two scale degrees, round up instead of down
    preferUpward = false
}

-- The root note is taken live from CV input 1: 0V = C, +1V = one octave up
-- (standard VCV pitch CV). Unpatched, the input reads 0V, so the scale is
-- rooted on C.
input.enable(1)
input.getName = function(port)
    if port == 1 then return "Root (1V/oct)" end
    return ""
end

-- Internal state.
-- playedAs[n] is the note number actually sent for incoming note n, so the
-- matching Note-Off can be rewritten the same way.
local state = {
    playedAs = {}
}

rack.onLoad = function()
    for n = 0, 127 do
        state.playedAs[n] = -1
    end
    rack.log("Scale quantiser initialized")
    rack.log("Root from input 1: 0V = C, 1V/oct")
    rack.log("Scale degrees: ", #config.scale)
end

-- Releases every note still substituted in state.playedAs. Without this, a
-- held note that has been remapped to a different scale degree would hang
-- forever once the script is replaced, the module is reset, or the module is
-- removed - the substitution needed to release it correctly lives only in
-- this script's state. state.playedAs isn't channel-indexed (only one scale
-- is active at a time), so this releases on config.channel if fixed, or
-- channel 1 when config.channel is 0 (every channel) - the same best-effort
-- choice Chord harmonizer makes for the same reason.
rack.onUnload = function()
    local ch = config.channel == 0 and 1 or config.channel
    for n = 0, 127 do
        if state.playedAs[n] >= 0 then
            local off = midi.create()
            midi.setNoteOff(off, ch, state.playedAs[n])
            midiOut.send(off)
        end
    end
end

local function matchesChannel(ch)
    return config.channel == 0 or ch == config.channel
end

-- Root note as a pitch class (0 = C, ... 11 = B), read from CV input 1 using
-- the standard VCV pitch convention: 0V = C, 1V = one octave = 12 semitones.
local function getRoot()
    return math.floor(input.getVoltage(1) * 12 + 0.5) % 12
end

-- Snaps a note number to the nearest member of the configured scale.
-- Works in pitch-class space, then puts the octave back, so the search only
-- ever has to look one octave up and down.
local function quantise(note)
    -- Distance above the root, folded into 0..11.
    -- Lua's % already returns a non-negative result for a positive divisor.
    local rel = (note - getRoot()) % 12
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

-- Context menu - right-click the module to change these settings live.
-- Each menu mirrors a `config` value above; onChange applies the choice.
local SCALE_NAMES = { "chromatic", "major", "minor", "harmonic", "dorian", "phrygian", "lydian", "mixolydian", "pentatonic", "minorPenta", "blues", "wholeTone" }
local SCALE_LABELS = { "Chromatic", "Major", "Minor", "Harmonic", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Pentatonic", "Minor pentatonic", "Blues", "Whole tone" }
local CHANNEL_LABELS = { "All" }
for c = 1, 16 do CHANNEL_LABELS[c + 1] = tostring(c) end

local function scaleIndex()
    for i = 1, #SCALE_NAMES do
        if config.scale == scales[SCALE_NAMES[i]] then return i - 1 end
    end
    return 0
end

rack.registerContextMenu({
    type = "options",
    label = "Scale",
    options = SCALE_LABELS,
    selected = scaleIndex(),
    onChange = function(idx)
        config.scale = scales[SCALE_NAMES[idx + 1]]
        rack.log("Scale: ", SCALE_LABELS[idx + 1])
    end
})

rack.registerContextMenu({
    type = "options",
    label = "Channel",
    options = CHANNEL_LABELS,
    selected = config.channel,
    onChange = function(idx)
        config.channel = idx
        rack.log("Channel: ", CHANNEL_LABELS[idx + 1])
    end
})

rack.registerContextMenu({
    type = "boolean",
    label = "Round up on ties",
    checked = config.preferUpward,
    onChange = function(checked)
        config.preferUpward = checked
    end
})

rack.onMidiMessage = function(midiPort, msg)
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
