--[[
@target stoermelder MIDI-KIT
@engine minilua@v1
@author stoermelder
@description Korg Volca Sample: converts MIDI notes to CC43 speed for chromatic playback with multi-channel part selection and 4-voice poly allocation
--]]

-- Korg Volca Sample transform for MIDI-KIT
--
-- The Volca Sample exposes its 10 sample parts on MIDI channels 1-10.  To
-- play a part chromatically you send CC 43 (speed) with a pitch-dependent
-- value followed by a Note-On on the part's channel.  This script does that
-- mapping transparently so you can play the Volca Sample like any other synth
-- from a standard keyboard.
--
-- Two modes, selected by the incoming MIDI channel:
--
--   Multi-channel (channels 1-10) – chromatic mode.
--     Each incoming note is converted to CC 43 speed + Note-On 60 on the
--     same channel.  Simple one-part-per-channel control.
--
--   Single-channel poly  (config.polyChannel, default 16).
--     Notes 0-9 select the sample part (→ channels 1-10).
--     Notes 36-84 (C2-C6) trigger the currently selected part and are
--     round-robin allocated across 4 voice channels (config.firstVoice ..
--     firstVoice+3), enabling 4-note polyphony from one input channel.
--
-- Pitch bend on any channel is converted to CC 44 (pitch EG intensity) with
-- a configurable range mapping.
--
-- All other messages (CC, aftertouch, program change, clock, etc.) pass
-- through unchanged so they reach the Volca directly.

-- ---------------------------------------------------------------------------
-- Configuration – edit these values to match your setup
-- ---------------------------------------------------------------------------
local config = {
    -- MIDI channel for single-channel poly mode (1-16).  Set to 0 to disable.
    polyChannel = 16,

    -- First of 4 consecutive MIDI channels used as voices in poly mode.
    -- The channels must not overlap with the 10 part channels (1-10).
    firstVoice = 7,         -- voices on channels 7, 8, 9, 10

    -- Chromatic note range.  Notes outside this range are passed through.
    pitchFirstNote = 36,    -- C2
    pitchLastNote = 84,     -- C6

    -- CC numbers used by the Volca Sample
    ccSpeed = 43,           -- sample speed / pitch
    ccPitchEg = 44,         -- pitch EG intensity

    -- Neutral values sent to all parts on load (64 = original speed)
    neutralSpeed = 64,
    neutralPitchEg = 64,

    -- Pitch-bend → CC 44 mapping.  The MSB of the 14-bit pitch-bend value
    -- (0-127) is rescaled from [pbInMin .. pbInMax] into [pbOutMin .. pbOutMax].
    pbInMin = 32,
    pbInMax = 96,
    pbOutMin = 60,
    pbOutMax = 70
}

-- ---------------------------------------------------------------------------
-- Speed table – reconstructed from the Volca Sample's internal pitch mapping.
-- Index 1 = MIDI note 36 (C2), value = CC 43 speed for that pitch.
-- ---------------------------------------------------------------------------
local SPEED_TABLE = {
    19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30,   -- C2 – B2   (36-47)
    32, 34, 37, 40, 43, 45, 48, 51, 53, 56, 59, 61,   -- C3 – B3   (48-59)
    64, 67, 69, 72, 75, 78, 80, 83, 85, 88, 91, 93,   -- C4 – B4   (60-71)
    96, 97, 99,100,102,103,104,106,107,108,109,111,   -- C5 – B5   (72-83)
   112                                                  -- C6        (84)
}

-- ---------------------------------------------------------------------------
-- Internal state
-- ---------------------------------------------------------------------------
local state = {
    -- Current voice channel (round-robin, advances on each Note-On in poly mode)
    voiceChan = config.firstVoice,

    -- Active notes: key = "voiceChannel:originalNote" → true.
    -- Used to route Note-Off to the correct voice channel.
    activeNotes = {}
}

-- ---------------------------------------------------------------------------
-- Helpers
-- ---------------------------------------------------------------------------

-- Returns the CC 43 speed value for a MIDI note, or -1 if out of range.
local function speedForNote(note)
    if note < config.pitchFirstNote or note > config.pitchLastNote then return -1 end
    return SPEED_TABLE[note - config.pitchFirstNote + 1]
end

-- Send CC 43 (speed) + Note-On 60 on the given channel for the given note.
local function sendVolcaNoteOn(channel, note, velocity)
    local speed = speedForNote(note)
    if speed < 0 then return end

    local cc = midi.create()
    midi.setCc(cc, channel, config.ccSpeed, speed)
    midiOut.send(cc)

    local on = midi.create()
    midi.setNoteOn(on, channel, 60, velocity)
    midiOut.send(on)
end

-- Send Note-Off 60 on the given channel.
local function sendVolcaNoteOff(channel)
    local off = midi.create()
    midi.setNoteOff(off, channel, 60)
    midiOut.send(off)
end

-- Advance voice channel for round-robin allocation.
local function nextVoice()
    state.voiceChan = state.voiceChan + 1
    if state.voiceChan > config.firstVoice + 3 then
        state.voiceChan = config.firstVoice
    end
end

-- ---------------------------------------------------------------------------
-- Lifecycle
-- ---------------------------------------------------------------------------
rack.onLoad = function()
    rack.log("Volca Sample initialized")

    if config.polyChannel > 0 then
        rack.log("Poly channel: " .. config.polyChannel)
        rack.log("Voice channels: " .. config.firstVoice .. "-" .. (config.firstVoice + 3))
    else
        rack.log("Multi-channel mode only")
    end
    rack.log("Chromatic range: MIDI " .. config.pitchFirstNote .. "-" .. config.pitchLastNote)

    -- Initialise the Volca – send neutral CC values to all 10 parts.
    for ch = 1, 10 do
        local cs = midi.create()
        midi.setCc(cs, ch, config.ccSpeed, config.neutralSpeed)
        midiOut.send(cs)

        local cp = midi.create()
        midi.setCc(cp, ch, config.ccPitchEg, config.neutralPitchEg)
        midiOut.send(cp)
    end
    rack.log("Neutral CC values sent to parts 1-10")
end

rack.onUnload = function()
    -- Release any active notes.
    for key, _ in pairs(state.activeNotes) do
        local sep = key:find(":")
        local ch = tonumber(key:sub(1, sep - 1))
        sendVolcaNoteOff(ch)
    end
end

-- ---------------------------------------------------------------------------
-- MIDI processing
-- ---------------------------------------------------------------------------
rack.onMidiMessage = function(midiPort, msg)
    local ch = midi.getChannel(msg)
    local isPoly = config.polyChannel > 0 and ch == config.polyChannel

    -- -- Pitch bend → CC 44 ------------------------------------------------
    if midi.isPitchWheel(msg) then
        -- midi.getValue() returns the MSB (0-127) for pitch-bend messages,
        -- matching the Arduino's data[2] byte.
        local msb = midi.getValue(msg)                       -- 0-127
        local outVal = math.floor(
            number.rescale(msb, config.pbInMin, config.pbInMax, config.pbOutMin, config.pbOutMax) + 0.5
        )
        outVal = math.max(config.pbOutMin, math.min(config.pbOutMax, outVal))

        local targetCh = ch
        if isPoly then targetCh = state.voiceChan end
        if targetCh < 1 or targetCh > 10 then targetCh = 1 end

        local cc = midi.create()
        midi.setCc(cc, targetCh, config.ccPitchEg, outVal)
        midiOut.send(cc)
        return  -- consume pitch bend
    end

    -- -- Note On -----------------------------------------------------------
    if midi.isNoteOn(msg) and midi.getValue(msg) > 0 then
        local note = midi.getNote(msg)
        local velocity = midi.getValue(msg)

        if isPoly then
            -- Poly mode: notes 0-9 select the part; chromatic notes trigger.
            if note < 10 then
                state.voiceChan = note + 1   -- note 0 → channel 1, etc.
                return                        -- consume part-select
            end
            if note >= config.pitchFirstNote and note <= config.pitchLastNote then
                local vc = state.voiceChan
                sendVolcaNoteOn(vc, note, velocity)
                state.activeNotes[vc .. ":" .. note] = true
                nextVoice()
                return
            end
        elseif ch >= 1 and ch <= 10 then
            -- Multi-channel: chromatic notes trigger on the same channel.
            if note >= config.pitchFirstNote and note <= config.pitchLastNote then
                sendVolcaNoteOn(ch, note, velocity)
                state.activeNotes[ch .. ":" .. note] = true
                return
            end
        end

        -- Note outside chromatic range or on unhandled channel → pass through.
        midiOut.send(msg)
        return
    end

    -- -- Note Off ----------------------------------------------------------
    if midi.isNoteOff(msg) or (midi.isNoteOn(msg) and midi.getValue(msg) == 0) then
        local note = midi.getNote(msg)

        if isPoly then
            if note < 10 then return end  -- consume part-select release

            if note >= config.pitchFirstNote and note <= config.pitchLastNote then
                -- Find the voice channel that played this note.
                for vc = config.firstVoice, config.firstVoice + 3 do
                    local key = vc .. ":" .. note
                    if state.activeNotes[key] then
                        sendVolcaNoteOff(vc)
                        state.activeNotes[key] = nil
                        return
                    end
                end
                -- Fallback: release on the current voice channel.
                sendVolcaNoteOff(state.voiceChan)
                return
            end
        elseif ch >= 1 and ch <= 10 then
            if note >= config.pitchFirstNote and note <= config.pitchLastNote then
                sendVolcaNoteOff(ch)
                state.activeNotes[ch .. ":" .. note] = nil
                return
            end
        end

        midiOut.send(msg)
        return
    end

    -- -- Everything else (CC, aftertouch, program change, clock, etc.) ----
    -- Pass through unchanged so direct CC control of the Volca still works.
    midiOut.send(msg)
end
