--[[
@target stoermelder MIDI-KIT
@engine Lua
@author stoermelder
@description Retunes notes to a micro scale (Scala-style), expressing the residual tuning as per-channel pitch bend
--]]

-- Micro scale processor for MIDI-KIT
--
-- A port of the classic "micro tuning" processor (see the Blokas MIDIHub
-- Micro Scale pipe): every incoming note is mapped onto a scale defined as
-- cents offsets per degree, its exact frequency computed, then re-expressed
-- as the nearest 12-EDO note number plus a pitch bend carrying the residual
-- cents. This is how Scala .scl files are actually played back - each key
-- position is retuned rather than "snapped" like the Scale quantiser.
--
-- Because pitch bend is channel-global in MIDI, every note is dispatched to
-- its own output channel (round-robin over config.channels), so each voice
-- carries an independent bend - exactly why the MIDIHub pipe needs its 16
-- "Use Ch" switches. Enable one channel per simultaneous voice your synth
-- has, or use fewer and accept note stealing.
--
-- The tuning is defined by a Scala .scl file pasted into config.scl below -
-- the same text you would load with "LOAD filename.scl" in the Scala
-- program. onLoad parses it into a list of cents offsets per octave degree
-- (first entry always 0 = the tonic at baseNote); ratios like "5/4" are
-- converted with ratioToCents(numer, denom) = 1200 * log2(numer / denom).
-- See https://www.huygens-fokker.org/scala/ for the format.
--
-- Note-Off handling is the part worth reading, the same trap as the Scale
-- quantiser: the note actually sent may be a *different* number than the one
-- played (it is the nearest semitone to the tuned frequency), so the Note-Off
-- that arrives later must be redirected to the channel and note number the
-- Note-On actually used. state.queueOfNote is a per-note FIFO of output
-- channels so even a unison (same note played twice) releases its voices in
-- press order.
--
-- onUnload releases every still-sounding note when the script is replaced,
-- the module is reset, or the module is removed - without it a note held at
-- that moment would hang forever, since nothing else remembers which channels
-- are down once this script's state is gone.


-- Configuration - change these values as needed
local config = {
    -- Paste the contents of a Scala .scl file between the [[ and ]] below -
    -- exactly as saved by the Scala program or downloaded from the Scala
    -- archive (https://www.huygens-fokker.org/scala/). The format is:
    --   ! optional comment lines
    --   <description>
    --   <number of notes per octave>
    --   <note 1> ... <note n>
    -- Each note is a frequency ratio like "5/4", or a cents value written
    -- with a decimal point like "386.314" (a bare integer is a ratio). The
    -- tonic 1/1 is implicit, and the last note is the octave (2/1 or 1200.0)
    -- which the scale repeats every octave.
    scl = [[
! Micro scale.scl
!
5-limit just intonation, major scale
 7
!
 9/8
 5/4
 4/3
 3/2
 5/3
 15/8
 2/1
]],

    -- Base MIDI note the scale is anchored to, and its frequency in Hz.
    -- baseNote 60 @ 261.625565 Hz is middle C in A440 tuning.
    baseNote = 60,
    baseFreq = 261.625565,

    -- Pitch bend depth of the receiving synth, in semitones. This must match
    -- the synth's configured bend range or every detune will be off by a
    -- factor. 2 semitones resolves the residual cents of any scale.
    bendDepth = 2,

    -- Output channels to dispatch notes to, round-robin. Each simultaneous
    -- note needs its own channel (pitch bend is channel-global), so enable as
    -- many as your synth has voices.
    channels = { 1, 2, 3, 4, 5, 6, 7, 8 },

    -- Always send a pitch bend on every Note-On, even when unchanged. Off
    -- relies on the receiver remembering the last bend per channel (smaller
    -- stream); turn on only if the receiving device plays wrong notes.
    alwaysSendBend = false,

    -- Only process this input channel; 0 = every channel
    channel = 0
}

-- Internal state, indexed by 1-based output channel.
-- noteOfChannel[c]     = incoming note currently sounding on output channel c (-1 = free).
-- sentNoteOfChannel[c] = the note number actually sent there (may differ from the incoming one).
-- bendOfChannel[c]     = the last pitch wheel value sent there, so unchanged bends can be skipped.
-- queueOfNote[n]       = FIFO of output channels note n was dispatched to, so a Note-Off releases
--                        the exact channel(s) and a unison (same note twice) releases in press order.
local state = {
    noteOfChannel = {},
    sentNoteOfChannel = {},
    bendOfChannel = {},
    queueOfNote = {},
    cursor = -1
}

-- The parsed scale, filled in onLoad from config.scl: cents offsets per
-- octave degree, scale[1] = 0 (the tonic at baseNote). The tonic is implicit
-- and the octave entry (1200 cents) is not stored - the scale repeats.
local scale = { 0 }

-- Converts a Scala frequency ratio to cents: 1200 * log2(numer / denom).
local function ratioToCents(numer, denom)
    return 1200 * math.log(numer / denom, 2)
end

-- Converts one .scl note line to cents. Per the Scala convention a value
-- containing a slash is a frequency ratio, one containing a decimal point is
-- already a cents value, and a bare integer is a ratio. Returns nil when the
-- line is not a note.
local function parseSclNote(s)
    local numerStr, denomStr = s:match("^(%d+%.?%d*)%s*/%s*(%d+%.?%d*)$")
    if numerStr then
        local denom = tonumber(denomStr)
        if denom == 0 then return nil end
        return ratioToCents(tonumber(numerStr), denom)
    end
    if s:find(".", 1, true) then
        return tonumber(s)
    end
    local v = tonumber(s)
    if v and v > 0 then return ratioToCents(v, 1) end
    return nil
end

-- Parses a pasted Scala .scl file into the cents-per-degree list. Lines
-- starting with "!" are comments. In the standard layout the first line is a
-- description, the second the note count and the following lines the notes;
-- a paste that omits the description line is tolerated (the count is first).
-- The tonic (0 cents) and the octave (1200 cents) entries are dropped - the
-- tonic is implicit at scale[1] and the octave is implied by the repetition.
local function parseScl(content)
    -- Non-comment, non-empty lines, in order.
    local lines = {}
    for raw in content:gmatch("[^\r\n]+") do
        local s = raw:gsub("^%s+", ""):gsub("%s+$", "")
        if s ~= "" and s:sub(1, 1) ~= "!" then
            lines[#lines + 1] = s
        end
    end

    -- Standard layout: a description line, then the note count, then the
    -- notes. Tolerate a paste that omits the description line.
    local start = 1
    if #lines >= 2 and lines[2]:match("^%d+$") then
        start = 3                    -- lines[1] = description, lines[2] = count
    elseif #lines >= 1 and lines[1]:match("^%d+$") then
        start = 2                    -- no description; lines[1] = count
    end
    -- else: no count line found - treat every line as a note.

    local scale = { 0 }
    for i = start, #lines do
        local cents = parseSclNote(lines[i])
        if cents and cents > 0 and cents < 1200 then
            scale[#scale + 1] = cents
        end
    end
    return scale
end

-- Rounds to the nearest integer (Lua has no Math.round).
local function round(x)
    return math.floor(x + 0.5)
end

-- The tuned pitch of a MIDI note relative to baseNote, in cents.
local function noteToCents(note)
    local rel = note - config.baseNote
    local size = #scale
    local oct = math.floor(rel / size)
    local deg = ((rel % size) + size) % size
    return oct * 1200 + scale[deg + 1]
end

-- The pitch wheel value for a deviation from the sent note, in semitones.
local function bendToPitchWheel(semis)
    local pw = 8192 + round(semis / config.bendDepth * 8192)
    if pw < 0 then pw = 0 end
    if pw > 16383 then pw = 16383 end
    return pw
end

-- Sends a pitch wheel on output channel ch. Skipped when the value is
-- unchanged and alwaysSendBend is off - the receiver still remembers the last
-- bend on that channel, the data-size optimisation the MIDIHub pipe has.
local function sendBend(ch, pw)
    if not config.alwaysSendBend and pw == state.bendOfChannel[ch] then return end
    local bend = midi.create()
    midi.setPitchWheel(bend, ch, pw)
    midiOut.send(bend)
    state.bendOfChannel[ch] = pw
end

rack.onLoad = function()
    scale = parseScl(config.scl)

    for c = 1, 16 do
        state.noteOfChannel[c] = -1
        state.sentNoteOfChannel[c] = -1
        state.bendOfChannel[c] = 8192
    end
    for n = 0, 127 do
        state.queueOfNote[n] = {}
    end
    rack.log("Micro scale initialized")
    rack.log("Scale degrees: ", #scale - 1, " per octave (parsed from config.scl)")
    if #scale < 2 then rack.log("WARNING: no scale notes parsed - check the pasted .scl in config.scl") end
    rack.log("Base: ", config.baseNote, " @ ", number.toString(config.baseFreq), " Hz")
    rack.log("Bend depth: ", number.toString(config.bendDepth), " st")
end

rack.onUnload = function()
    for c = 1, 16 do
        if state.noteOfChannel[c] >= 0 then
            local off = midi.create()
            midi.setNoteOff(off, c, state.sentNoteOfChannel[c])
            midiOut.send(off)
        end
    end
end

local function matchesChannel(ch)
    return config.channel == 0 or ch == config.channel
end

-- Finds the output channel for a new note: the next free channel in
-- round-robin order, or the next channel when all are busy (note stealing).
local function allocateChannel()
    local n = #config.channels
    for i = 1, n do
        local idx = (state.cursor + i) % n
        local ch = config.channels[idx + 1]
        if state.noteOfChannel[ch] < 0 then
            state.cursor = idx
            return ch
        end
    end
    state.cursor = (state.cursor + 1) % n
    return config.channels[state.cursor + 1]
end

-- Removes `ch` from the FIFO of an incoming note (used when a channel is
-- stolen, so the displaced note's later Note-Off doesn't release the thief).
local function removeFromQueue(note, ch)
    local q = state.queueOfNote[note]
    for i = 1, #q do
        if q[i] == ch then
            table.remove(q, i)
            return
        end
    end
end

-- Context menu - right-click the module to change these settings live.
-- Each menu mirrors a `config` value above; onChange applies the choice.
local CHANNEL_LABELS = { "All" }
for c = 1, 16 do CHANNEL_LABELS[c + 1] = tostring(c) end

rack.registerContextMenu({
    type = "options",
    label = "Input channel",
    options = CHANNEL_LABELS,
    onGetValue = function()
        return config.channel
    end,
    onChange = function(idx)
        config.channel = idx
        rack.log("Input channel: ", CHANNEL_LABELS[idx + 1])
    end
})

rack.registerContextMenu({
    type = "boolean",
    label = "Always send pitch bend",
    onGetValue = function()
        return config.alwaysSendBend
    end,
    onChange = function(checked)
        config.alwaysSendBend = checked
    end
})

rack.onMidiMessage = function(midiPort, msg)
    local ch = midi.getChannel(msg)

    if not matchesChannel(ch) then
        midiOut.send(msg)
        return
    end

    local vel = 0
    if midi.isNoteOn(msg) then vel = midi.getValue(msg) end
    local isOn = midi.isNoteOn(msg) and vel > 0
    -- Velocity 0 is the running-status spelling of a Note-Off.
    local isOff = midi.isNoteOff(msg) or (midi.isNoteOn(msg) and vel == 0)

    if isOn then
        local note = midi.getNote(msg)

        -- Tuned frequency of the note, then back to the nearest 12-EDO note
        -- number plus the residual bend in semitones.
        local cents = noteToCents(note)
        local freq = config.baseFreq * 2 ^ (cents / 1200)
        local semis = 12 * math.log(freq / config.baseFreq, 2) + config.baseNote
        local outNote = round(semis)
        if outNote < 0 then outNote = 0 end
        if outNote > 127 then outNote = 127 end
        local bendSemis = semis - outNote
        local pw = bendToPitchWheel(bendSemis)

        local outCh = allocateChannel()

        -- If we stole a channel, drop the note it was holding so the stale
        -- Note-Off for it doesn't release the new voice.
        local displaced = state.noteOfChannel[outCh]
        if displaced >= 0 then
            removeFromQueue(displaced, outCh)
        end

        state.noteOfChannel[outCh] = note
        state.sentNoteOfChannel[outCh] = outNote
        table.insert(state.queueOfNote[note], outCh)

        sendBend(outCh, pw)

        local on = midi.create()
        midi.setNoteOn(on, outCh, outNote, vel)
        midiOut.send(on)

        return
    end

    if isOff then
        local note = midi.getNote(msg)
        local q = state.queueOfNote[note]
        -- Release the oldest channel this note was dispatched to, skipping any
        -- stale entry left by a stolen channel. One Note-Off releases one
        -- voice, so a unison needs two Note-Offs like the keyboard sent.
        while #q > 0 do
            local outCh = table.remove(q, 1)
            if state.noteOfChannel[outCh] == note then
                local off = midi.create()
                midi.setNoteOff(off, outCh, state.sentNoteOfChannel[outCh])
                midiOut.send(off)
                state.noteOfChannel[outCh] = -1
                state.sentNoteOfChannel[outCh] = -1
                return
            end
        end
        return
    end

    -- Everything else (CC, program change, realtime, ...) passes through.
    midiOut.send(msg)
end
