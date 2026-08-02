--[[
@target stoermelder MIDI-KIT
@engine Lua
@author stoermelder
@description Arpeggiator clocked by the trigger input, with clock division, octave range, note length and playmode params
--]]

-- Arpeggiator for MIDI-KIT
--
-- Held notes (tracked from incoming Note-On/Note-Off on config.channel) are
-- stepped through in a pattern and re-sung one at a time, advanced by the
-- module's trigger input rather than by incoming MIDI - feed a clock (e.g.
-- from a Rack clock source, or MIDI clock forwarded to a trigger output
-- elsewhere in the patch) into the trigger input and the arp follows it.
--
-- param 1 - Clock division: how many trigger ticks make up one arp step.
--   Value is quantised to a musical division list, not a raw multiplier -
--   see DIVISIONS below.
-- param 2 - Octave range: 1-4 octaves; the held notes are repeated one
--   octave higher each time up to this count before the pattern cycles.
-- param 3 - Note length: gate length as a fraction of one step, expressed in
--   trigger ticks (min 1, capped at clockDivision - 1 so notes never tie into
--   the next step).
-- param 4 - Playmode: Up / Down / Up-Down.
--
-- Notes are only advanced on a trigger tick that lands on a step boundary
-- (i.e. every clockDivision-th tick), so the trigger input can run at a
-- finer resolution than the arp itself - the same divide-down idea as
-- Clock divider.lua, just applied to a CV trigger instead of MIDI clock.
--
-- Silence (no keys held) simply stops stepping; the next Note-On restarts
-- the pattern from its first note on the next step boundary.


-- Configuration - change these values as needed
local config = {
    -- Trigger input driving the arp (1-based)
    trigPort = 1,

    -- Only arpeggiate notes on this channel; 0 = every channel
    channel = 0,

    -- Output channel for arpeggiated notes; 0 = same as input note's channel
    outChannel = 0,

    -- Show the currently playing step in the panel overlay
    showOverlay = true
}

-- Clock division choices, in trigger ticks per arp step (fewer ticks = faster)
local DIVISIONS = { 1, 2, 3, 4, 6, 8, 12, 16, 24, 32 }

-- Playmode names, selected by param 4
local PLAYMODES = { "Up", "Down", "Up-Down" }

-- Internal state.
-- held: note numbers currently down, in the order they were pressed.
-- pattern: the expanded, octave-doubled sequence built from held+octaves+playmode.
-- step: index into pattern for the note played on the next step boundary (1-based).
-- tickCount: counts trigger ticks up to the current clockDivision.
-- soundingNote/soundingChannel: the note+channel currently sustained by the
-- arp, so it can be released before the next one starts or on unload.
local state = {
    held = {},
    pattern = {},
    step = 1,
    tickCount = 0,
    soundingNote = -1,
    soundingChannel = 1
}

param.enable(1)
param.enable(2)
param.enable(3)
param.enable(4)

param.getName = function(i)
    if i == 1 then return "Clock division" end
    if i == 2 then return "Octave range" end
    if i == 3 then return "Note length" end
    if i == 4 then return "Playmode" end
    return ""
end

local function divisionIndex()
    local idx = math.floor(param.getValue(1) * #DIVISIONS) + 1
    if idx > #DIVISIONS then idx = #DIVISIONS end
    return idx
end

local function octaveRange()
    local o = math.floor(param.getValue(2) * 4) + 1
    if o > 4 then o = 4 end
    return o
end

local function playmodeIndex()
    local idx = math.floor(param.getValue(4) * #PLAYMODES) + 1
    if idx > #PLAYMODES then idx = #PLAYMODES end
    return idx
end

param.getValueFormat = function(i)
    if i == 1 then return number.toString(DIVISIONS[divisionIndex()]) .. " ticks/step" end
    if i == 2 then return number.toString(octaveRange()) .. " oct" end
    if i == 3 then return number.toFixed(param.getValue(3) * 100, 0) .. " %" end
    if i == 4 then return PLAYMODES[playmodeIndex()] end
    return number.toString(param.getValue(i))
end

local function matchesChannel(ch)
    return config.channel == 0 or ch == config.channel
end

-- Rebuilds state.pattern from state.held, the octave range and the playmode.
-- Called whenever the held-note set changes so the pattern is always fresh
-- at the next step boundary.
local function rebuildPattern()
    local notes = {}
    local oct = octaveRange()

    for o = 0, oct - 1 do
        for i = 1, #state.held do
            local n = state.held[i] + 12 * o
            if n <= 127 then notes[#notes + 1] = n end
        end
    end

    local mode = playmodeIndex()
    if mode == 1 then
        -- Up - as built
        state.pattern = notes
    elseif mode == 2 then
        -- Down - reverse
        local rev = {}
        for i = #notes, 1, -1 do rev[#rev + 1] = notes[i] end
        state.pattern = rev
    else
        -- Up-Down - ascend then descend, without repeating the two end notes
        local updown = {}
        for i = 1, #notes do updown[#updown + 1] = notes[i] end
        for i = #notes - 1, 2, -1 do updown[#updown + 1] = notes[i] end
        state.pattern = updown
    end

    if state.step > #state.pattern then state.step = 1 end
end

-- Releases whatever note the arp is currently sustaining, if any.
local function releaseSounding()
    if state.soundingNote >= 0 then
        local off = midi.create()
        midi.setNoteOff(off, state.soundingChannel, state.soundingNote)
        midiOut.send(off)
        state.soundingNote = -1
    end
end

function onLoad()
    rack.log("Arpeggiator initialized")
    rack.log("Trigger input: ", config.trigPort)
end

function onUnload()
    releaseSounding()
end

function onMidiMessage(midiPort, msg)
    local ch = midi.getChannel(msg)

    if midi.isNoteOn(msg) and matchesChannel(ch) and midi.getValue(msg) > 0 then
        local note = midi.getNote(msg)
        -- Ignore duplicates - a note already held stays in its original
        -- press-order slot rather than jumping to the end.
        local known = false
        for i = 1, #state.held do
            if state.held[i] == note then known = true end
        end
        if not known then
            state.held[#state.held + 1] = note
            rebuildPattern()
        end
        return
    end

    if (midi.isNoteOff(msg) or (midi.isNoteOn(msg) and midi.getValue(msg) == 0)) and matchesChannel(ch) then
        local note = midi.getNote(msg)
        local filtered = {}
        for i = 1, #state.held do
            if state.held[i] ~= note then filtered[#filtered + 1] = state.held[i] end
        end
        state.held = filtered
        rebuildPattern()
        if #state.held == 0 then
            -- Nothing left held - stop the arp and let go of the last note.
            releaseSounding()
            state.step = 1
            state.tickCount = 0
        end
        return
    end

    -- Forward everything else (CC, pitch bend, clock, ...) unchanged.
    midiOut.send(msg)
end

function onTrigger(trigPort)
    if trigPort ~= config.trigPort then return end

    local division = DIVISIONS[divisionIndex()]
    state.tickCount = state.tickCount + 1
    if state.tickCount < division then return end
    state.tickCount = 0

    releaseSounding()

    if #state.pattern == 0 then return end

    local note = state.pattern[state.step]
    local ch = config.outChannel == 0 and 1 or config.outChannel

    local on = midi.create()
    midi.setNoteOn(on, ch, note, 100)
    midiOut.send(on)

    local lengthTicks = math.floor(division * param.getValue(3))
    if lengthTicks < 1 then lengthTicks = 1 end
    if lengthTicks > division - 1 then
        lengthTicks = division > 1 and (division - 1) or 1
    end

    local off = midi.create()
    midi.setNoteOff(off, ch, note)
    midiOut.sendAfterTrigger(off, config.trigPort, lengthTicks)

    state.soundingNote = note
    state.soundingChannel = ch

    if config.showOverlay then
        rack.overlay("Arp " .. PLAYMODES[playmodeIndex()], "note " .. number.toString(note) .. " (" .. number.toString(state.step) .. "/" .. number.toString(#state.pattern) .. ")")
    end

    state.step = state.step + 1
    if state.step > #state.pattern then state.step = 1 end
end
