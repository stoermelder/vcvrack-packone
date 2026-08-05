--[[
@target stoermelder MIDI-KIT
@engine minilua@v1
@author stoermelder
@description Gravity well: notes are bent toward a center pitch, more when they are far from it or played softly
--]]

-- Gravity well for MIDI-KIT
--
-- Every Note-On "falls" toward a configurable center pitch: the note is
-- retuned by a fraction of its distance from the center, so the farther it is
-- from the center the more it is pulled in. How far it falls is set by the
-- velocity - soft notes fall deep into the well (a lot of bending, tension),
-- loud notes resist the pull and stay near their original pitch (release).
-- The tension/release emerges automatically from the note's distance and the
-- incoming velocity, no programming needed.
--
-- param 1 - Center: the gravitational center pitch, 0-127. Notes are bent
--   toward this note; a note on the center passes through untouched.
-- param 2 - Strength: how strongly the well pulls, 0..1. The bend is
--   distance * Strength * (1 - velocity / 127), so 0 disables the effect and
--   the bend grows with distance and shrinks with velocity.
--
-- Because the retuned pitch depends on the velocity, the Note-Off that arrives
-- later carries the *played* note, not the one sent - the script remembers the
-- sent note per channel/note so every release lands on the right pitch (the
-- same trap as the Scale quantiser and Micro scale presets). Everything that
-- is not a Note-On/Note-Off passes through unchanged.

param.enable(1)
param.enable(2)

param.getName = function(i)
    if i == 1 then return "Center" end
    if i == 2 then return "Strength" end
    return ""
end

-- The gravitational center pitch, 0-127.
local function centerParam()
    local c = math.floor(param.getValue(1) * 127 + 0.5)
    if c < 0 then c = 0 end
    if c > 127 then c = 127 end
    return c
end

-- The well's pull, 0..1.
local function strengthParam()
    local s = param.getValue(2)
    if s < 0 then s = 0 end
    if s > 1 then s = 1 end
    return s
end

-- Note names (sharps only) for the Center readout.
local NOTE_NAMES = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }

local function noteName(n)
    return NOTE_NAMES[n % 12 + 1] .. (math.floor(n / 12) - 1)
end

param.getValueFormat = function(i)
    if i == 1 then return number.toString(centerParam()) .. " (" .. noteName(centerParam()) .. ")" end
    if i == 2 then return number.toString(strengthParam()) end
    return number.toString(param.getValue(i))
end

-- Internal state: the note actually sent for each incoming (channel, note), so
-- the Note-Off can release the bent pitch. -1 = nothing mapped.
local state = {
    sentNote = {}
}

rack.onLoad = function()
    for c = 1, 16 do
        state.sentNote[c] = {}
        for n = 0, 127 do state.sentNote[c][n] = -1 end
    end
    rack.log("Gravity well initialized")
    rack.log("Center: ", centerParam(), " | Strength: ", number.toString(strengthParam()))
end

rack.onUnload = function()
    for c = 1, 16 do
        for n = 0, 127 do
            if state.sentNote[c][n] >= 0 then
                local off = midi.create()
                midi.setNoteOff(off, c, state.sentNote[c][n])
                midiOut.send(off)
            end
        end
    end
end

-- The bent pitch for a note: pulled toward the center by a fraction of the
-- distance that shrinks as the velocity rises. Uses floor(x + 0.5) so both
-- engines round identically even for negative (below-center) bends.
local function bendNote(note, vel)
    local distance = note - centerParam()
    local fraction = strengthParam() * (1 - vel / 127)
    local outNote = note - math.floor(distance * fraction + 0.5)
    if outNote < 0 then outNote = 0 end
    if outNote > 127 then outNote = 127 end
    return outNote
end

rack.onMidiMessage = function(midiPort, msg)
    local ch = midi.getChannel(msg)

    local vel = 0
    if midi.isNoteOn(msg) then vel = midi.getValue(msg) end
    local isOn = midi.isNoteOn(msg) and vel > 0
    -- Velocity 0 is the running-status spelling of a Note-Off.
    local isOff = midi.isNoteOff(msg) or (midi.isNoteOn(msg) and vel == 0)

    if isOn then
        local note = midi.getNote(msg)
        local outNote = bendNote(note, vel)
        state.sentNote[ch][note] = outNote

        local on = midi.create()
        midi.setNoteOn(on, ch, outNote, vel)
        midiOut.send(on)
        return
    end

    if isOff then
        local note = midi.getNote(msg)
        local outNote = state.sentNote[ch][note]
        if outNote < 0 then outNote = note end
        state.sentNote[ch][note] = -1

        local off = midi.create()
        midi.setNoteOff(off, ch, outNote)
        midiOut.send(off)
        return
    end

    -- Everything else (CC, pitch bend, clock, ...) passes through.
    midiOut.send(msg)
end
