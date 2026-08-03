--[[
@target stoermelder MIDI-KIT
@engine Lua
@author stoermelder
@description Turns every incoming note into a chord by adding transposed copies, with matching Note-Offs
--]]

-- Chord harmonizer for MIDI-KIT
--
-- Adds transposed copies of every played note, turning a single-finger melody
-- into chords. config.intervals lists the semitone offsets to add - 0 keeps the
-- played note itself, so removing it produces only the harmony voices.
--
-- Presets to try:
--     { 0, 4, 7 }        major triad
--     { 0, 3, 7 }        minor triad
--     { 0, 3, 7, 10 }    minor seventh
--     { 0, 7 }           power chord
--     { 0, 12 }          octave doubling
--     { 0, -12, 12 }     three octaves
--
-- Note-Off handling is the part worth reading. Every voice sent for a note must
-- be released, and only once: if two different intervals collide on the same
-- note number (or a transposed voice lands on a note the player is also holding
-- directly) a naive "send a Note-Off per interval" would release the note while
-- another voice still wants it. This script therefore reference-counts sounding
-- note numbers in state.refCount and only emits a Note-Off when the last user
-- of that note lets go.
--
-- onUnload releases every still-sounding note when the script is replaced,
-- the module is reset, or the module is removed - without it, a chord held
-- at that moment would hang forever, since nothing else remembers those
-- note numbers are down once this script's state is gone.


-- Configuration - change these values as needed
local config = {
    -- Semitone offsets added for every played note. Include 0 to keep the
    -- original note; omit it to hear only the harmony voices.
    intervals = { 0, 4, 7 },

    -- Only harmonize this channel; 0 = every channel
    channel = 0,

    -- Velocity scaling for the added voices, relative to the played note.
    -- The 0-offset voice is always sent at full velocity.
    harmonyVelocity = 0.8
}

-- Internal state.
-- refCount[n] counts how many voices currently want note number n sounding.
-- voicesOf[n] is the list of note numbers that the played note n produced,
-- so the Note-Off can release exactly the same set.
local state = {
    refCount = {},
    voicesOf = {}
}

rack.onLoad = function()
    for n = 0, 127 do
        state.refCount[n] = 0
        state.voicesOf[n] = {}
    end
    rack.log("Chord harmonizer initialized")
    rack.log("Voices per note: ", #config.intervals)
end

rack.onUnload = function()
    for n = 0, 127 do
        if state.refCount[n] > 0 then
            local off = midi.create()
            midi.setNoteOff(off, 1, n)
            midiOut.send(off)
        end
    end
end

local function matchesChannel(ch)
    return config.channel == 0 or ch == config.channel
end

-- Releases every voice started for played note `note`
local function releaseVoices(ch, note)
    local voices = state.voicesOf[note]

    for i = 1, #voices do
        local target = voices[i]
        state.refCount[target] = state.refCount[target] - 1

        -- Last voice holding this note number - now it may actually stop
        if state.refCount[target] <= 0 then
            state.refCount[target] = 0
            local off = midi.create()
            midi.setNoteOff(off, ch, target)
            midiOut.send(off)
        end
    end

    state.voicesOf[note] = {}
end

-- Context menu - right-click the module to change these settings live.
-- Each menu mirrors a `config` value above; onChange applies the choice.
local CHORD_INTERVALS = {
    { 0, 4, 7 },     -- Major triad
    { 0, 3, 7 },     -- Minor triad
    { 0, 3, 7, 10 }, -- Minor seventh
    { 0, 7 },        -- Power chord
    { 0, 12 },       -- Octave doubling
    { 0, -12, 12 }   -- Three octaves
}
local CHORD_LABELS = { "Major triad", "Minor triad", "Minor seventh", "Power chord", "Octave doubling", "Three octaves" }
local CHANNEL_LABELS = { "All" }
for c = 1, 16 do CHANNEL_LABELS[c + 1] = tostring(c) end

local function chordIndex()
    for i = 1, #CHORD_INTERVALS do
        if #config.intervals == #CHORD_INTERVALS[i] then
            local same = true
            for j = 1, #CHORD_INTERVALS[i] do
                if config.intervals[j] ~= CHORD_INTERVALS[i][j] then same = false break end
            end
            if same then return i - 1 end
        end
    end
    return 0
end

rack.registerContextMenu({
    type = "options",
    label = "Chord",
    options = CHORD_LABELS,
    onGetValue = function()
        return chordIndex()
    end,
    onChange = function(idx)
        config.intervals = CHORD_INTERVALS[idx + 1]
        rack.log("Chord: ", CHORD_LABELS[idx + 1], " (", #config.intervals, " voices)")
    end
})

rack.registerContextMenu({
    type = "options",
    label = "Channel",
    options = CHANNEL_LABELS,
    onGetValue = function()
        return config.channel
    end,
    onChange = function(idx)
        config.channel = idx
        rack.log("Channel: ", CHANNEL_LABELS[idx + 1])
    end
})

rack.onMidiMessage = function(midiPort, msg)
    local ch = midi.getChannel(msg)

    if not matchesChannel(ch) then
        midiOut.send(msg)
        return
    end

    -- Velocity 0 is the running-status spelling of a Note-Off
    local isRelease = midi.isNoteOff(msg) or (midi.isNoteOn(msg) and midi.getValue(msg) == 0)

    if midi.isNoteOn(msg) and not isRelease then
        local note = midi.getNote(msg)
        local vel = midi.getValue(msg)
        local voices = {}

        for i = 1, #config.intervals do
            local offset = config.intervals[i]
            local target = note + offset

            if target >= 0 and target <= 127 then
                -- Only actually sound the note if nothing else is holding it.
                -- Otherwise just take a reference - the note is already down.
                if state.refCount[target] == 0 then
                    local v = vel
                    if offset ~= 0 then
                        v = math.floor(vel * config.harmonyVelocity + 0.5)
                        if v < 1 then v = 1 end
                    end

                    local on = midi.create()
                    midi.setNoteOn(on, ch, target, v)
                    midiOut.send(on)
                end
                state.refCount[target] = state.refCount[target] + 1
                voices[#voices + 1] = target
            end
        end

        state.voicesOf[note] = voices

        return
    end

    if isRelease then
        local note = midi.getNote(msg)

        -- Never saw the Note-On (script loaded mid-chord): pass the release
        -- through untouched so the note cannot hang.
        if #state.voicesOf[note] == 0 then
            midiOut.send(msg)
            return
        end

        releaseVoices(ch, note)
        return
    end

    midiOut.send(msg)
end
