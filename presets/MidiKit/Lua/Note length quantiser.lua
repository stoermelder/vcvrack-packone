--[[
@target stoermelder MIDI-KIT
@engine Lua
@author stoermelder
@description Replaces every note's held length with a fixed number of clock ticks, so all notes end on the grid
--]]

-- Note length quantiser for MIDI-KIT
--
-- Notes played by hand end whenever the finger lifts, which leaves ragged note
-- lengths - a problem for gate-driven gear, for arpeggiators that re-trigger on
-- release, and for anything where a slightly-too-long note overlaps the next.
--
-- This script discards the incoming Note-Off entirely and schedules its own,
-- exactly config.lengthTicks clock ticks after the Note-On, using
-- midiOut.sendAfterTrigger(). Every note then lasts the same musical duration
-- regardless of how it was played.
--
-- Requires a clock on the module's trigger input (config.trigPort): the length
-- is counted in ticks of that input, not in milliseconds, so it follows tempo.
-- Feed the same clock that drives the rest of the patch. With a 24 ppqn MIDI
-- clock routed to the trigger input:
--
--     lengthTicks  6 -> 16th note
--     lengthTicks 12 -> 8th note
--     lengthTicks 24 -> quarter note
--
-- Retriggering the same note while it is still sounding is handled by sending
-- the pending Note-Off immediately, so the note is re-articulated rather than
-- being cut short later by a stale scheduled release.


-- Configuration - change these values as needed
local config = {
    -- Fixed note length, in ticks of the trigger input's clock
    lengthTicks = 12,

    -- Trigger input the length is counted on (1-based)
    trigPort = 1,

    -- Only quantise this channel; set to 0 to quantise every channel
    channel = 0,

    -- Forward non-note messages (CC, pitch bend, clock, ...) unchanged
    passThroughOther = true,

    -- Log each quantised note
    verbose = false
}

-- Internal state.
-- sounding[n] is true while note number n has a scheduled Note-Off pending.
local state = {
    sounding = {}
}

rack.onLoad = function()
    for n = 0, 127 do
        state.sounding[n] = false
    end
    rack.log("Note length quantiser initialized")
    rack.log("Length: ", config.lengthTicks, " ticks on trigger input ", config.trigPort)
    if config.channel == 0 then
        rack.log("Channel: all")
    else
        rack.log("Channel: ", config.channel)
    end
end

-- Releases every note with a still-pending scheduled Note-Off. Without this,
-- a note whose release hasn't fired yet at the moment the script is replaced,
-- the module is reset, or the module is removed would hang forever - the
-- scheduled Note-Off belongs to the old script state and is discarded with it.
-- state.sounding isn't channel-indexed (only one note-length policy is active
-- at a time), so this releases on config.channel if fixed, or channel 1 when
-- config.channel is 0 (every channel) - the same best-effort choice
-- Chord harmonizer makes for the same reason.
rack.onUnload = function()
    local ch = config.channel == 0 and 1 or config.channel
    for n = 0, 127 do
        if state.sounding[n] then
            local off = midi.create()
            midi.setNoteOff(off, ch, n)
            midiOut.send(off)
        end
    end
end

local function matchesChannel(ch)
    return config.channel == 0 or ch == config.channel
end

-- Builds and schedules the Note-Off that ends a quantised note.
local function scheduleNoteOff(ch, note)
    local off = midi.create()
    midi.setNoteOff(off, ch, note)
    midiOut.sendAfterTrigger(off, config.trigPort, config.lengthTicks)
end

-- Context menu - right-click the module to change these settings live.
-- Each menu mirrors a `config` value above; onChange applies the choice.
local LENGTH_TICKS = { 6, 12, 24, 48 }
local LENGTH_LABELS = { "6 (16th)", "12 (8th)", "24 (quarter)", "48 (half)" }
local CHANNEL_LABELS = { "All" }
for c = 1, 16 do CHANNEL_LABELS[c + 1] = tostring(c) end

local function lengthTicksIndex()
    for i = 1, #LENGTH_TICKS do
        if LENGTH_TICKS[i] == config.lengthTicks then return i - 1 end
    end
    return 0
end

rack.registerContextMenu({
    type = "options",
    label = "Note length",
    options = LENGTH_LABELS,
    onGetValue = function()
        return lengthTicksIndex()
    end,
    onChange = function(idx)
        config.lengthTicks = LENGTH_TICKS[idx + 1]
        rack.log("Length: ", config.lengthTicks, " ticks")
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

rack.registerContextMenu({
    type = "boolean",
    label = "Pass through other messages",
    onGetValue = function()
        return config.passThroughOther
    end,
    onChange = function(checked)
        config.passThroughOther = checked
    end
})

rack.registerContextMenu({
    type = "boolean",
    label = "Log quantised notes",
    onGetValue = function()
        return config.verbose
    end,
    onChange = function(checked)
        config.verbose = checked
    end
})

rack.onMidiMessage = function(midiPort, msg)
    local ch = midi.getChannel(msg)

    if midi.isNoteOn(msg) and matchesChannel(ch) then
        local note = midi.getNote(msg)

        -- Same note still sounding: release it now so the re-articulation is
        -- clean and its pending scheduled Note-Off cannot clip the new note.
        if state.sounding[note] then
            local cut = midi.create()
            midi.setNoteOff(cut, ch, note)
            midiOut.send(cut)
        end

        midiOut.send(msg)
        scheduleNoteOff(ch, note)
        state.sounding[note] = true

        if config.verbose then
            rack.log("note ", note, " -> ", config.lengthTicks, " ticks")
        end
        return
    end

    if midi.isNoteOff(msg) and matchesChannel(ch) then
        -- Dropped on purpose: the scheduled Note-Off is what ends the note.
        -- Note that state.sounding is cleared here rather than when the
        -- scheduled release fires - the script has no callback for that - so a
        -- note held longer than lengthTicks is already marked free by the time
        -- the player lifts the key, which is the same moment it stopped sounding.
        state.sounding[midi.getNote(msg)] = false
        return
    end

    if config.passThroughOther then
        midiOut.send(msg)
    end
end
