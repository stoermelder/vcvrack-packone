--[[
@target stoermelder MIDI-KIT
@engine Lua
@author stoermelder
@description Bouncing ball delay: each Note-On spawns a train of echoes that bounce faster and quieter until they settle
--]]

-- Bouncing ball delay for MIDI-KIT
--
-- Every Note-On spawns a "ball": the note is repeated as a train of echoes,
-- each one arriving sooner than the last (gravity) and quieter than the last
-- (damping), until the velocity drops below the minimum threshold and the ball
-- settles - the sound of a ball bearing dropped on a table.
--
-- The original Note-On (and its release) passes through untouched; the echo
-- train runs on top and is fully pre-scheduled, so releasing the note does not
-- stop a train that has already started - just like a real delay keeps ringing
-- after the source stops. The whole train is computed and scheduled with
-- midiOut.sendAfterMs() when the Note-On arrives, using the param values at
-- that moment.
--
-- param 1 - Gravity: how much the gap between bounces shrinks each bounce.
--   The interval is multiplied by (1 - Gravity) per bounce, so 0 gives a
--   uniform (regular) delay and higher values make the bounces bunch up.
-- param 2 - Bounciness: how much of the velocity survives each bounce (the
--   damping). 1 keeps the echoes at full velocity forever (the train then
--   stops only at the echo cap), lower values fade the ball out quickly.
-- param 3 - Min velocity: echoes stop once their velocity falls below this
--   threshold - the "settle" point.
--
-- The echo interval starts at config.initialInterval ms and each echo's gate
-- is config.gateMs long (capped at half the current interval so echoes never
-- bleed into the next bounce). Each ball is capped at config.maxEchoes echoes
-- because the engine allows 32 live message handles per callback and each echo
-- needs two (a Note-On and a Note-Off).

-- Configuration - change these values as needed
local config = {
    -- Delay of the first echo after the note, in milliseconds.
    initialInterval = 250,

    -- Length of each echo's gate in milliseconds (capped at half the current
    -- bounce interval).
    gateMs = 40,

    -- Safety cap on echoes per ball - each echo uses two message handles
    -- (Note-On + Note-Off) against the engine's 32-handle per-callback limit.
    maxEchoes = 12
}

param.enable(1)
param.enable(2)
param.enable(3)

param.getName = function(i)
    if i == 1 then return "Gravity" end
    if i == 2 then return "Bounciness" end
    if i == 3 then return "Min velocity" end
    return ""
end

-- Interval shrink per bounce: 0..0.4, so intervals stay at 100%..60%.
local function gravityParam()
    local g = param.getValue(1) * 0.4
    if g > 0.4 then g = 0.4 end
    return g
end

-- Velocity retention per bounce: 0..1.
local function bouncinessParam()
    return param.getValue(2)
end

-- Echoes settle below this velocity: 1..127.
local function minVelocityParam()
    local m = math.floor(param.getValue(3) * 126 + 0.5) + 1
    if m < 1 then m = 1 end
    if m > 127 then m = 127 end
    return m
end

param.getValueFormat = function(i)
    if i == 1 then return number.toString(gravityParam()) end
    if i == 2 then return string.format("%.0f", bouncinessParam() * 100) .. " %" end
    if i == 3 then return number.toString(minVelocityParam()) end
    return number.toString(param.getValue(i))
end

rack.onLoad = function()
    rack.log("Bouncing ball delay initialized")
    rack.log("Gravity: ", number.toString(gravityParam()), " | Bounciness: ", string.format("%.0f", bouncinessParam() * 100), "% | Min velocity: ", minVelocityParam())
end

-- Schedules the echo train for one ball started by the given Note-On. The
-- whole train is pre-computed here: each bounce's Note-On and Note-Off is
-- placed with sendAfterMs(), the gap shrinking by (1 - gravity) and the
-- velocity by bounciness until it settles below the min-velocity threshold.
local function spawnBall(ch, note, vel)
    local interval = config.initialInterval
    local velocity = vel
    local t = interval
    local count = 0

    while velocity >= minVelocityParam() and count < config.maxEchoes do
        -- Cap the gate at half the current interval so this echo's Note-Off
        -- lands before the next bounce's Note-On.
        local gate = config.gateMs
        if gate > interval * 0.5 then gate = interval * 0.5 end
        if gate < 1 then gate = 1 end

        local on = midi.create()
        midi.setNoteOn(on, ch, note, math.floor(velocity + 0.5))
        midiOut.sendAfterMs(on, t)

        local off = midi.create()
        midi.setNoteOff(off, ch, note)
        midiOut.sendAfterMs(off, t + gate)

        velocity = velocity * bouncinessParam()
        interval = interval * (1 - gravityParam())
        t = t + interval
        count = count + 1
    end
end

rack.onMidiMessage = function(midiPort, msg)
    if midi.isNoteOn(msg) and midi.getValue(msg) > 0 then
        local ch = midi.getChannel(msg)
        local note = midi.getNote(msg)
        local vel = midi.getValue(msg)
        -- Dry note first (so it leads the echoes), then the echo train.
        midiOut.send(msg)
        spawnBall(ch, note, vel)
        return
    end
    -- Everything else (Note-Off, CC, pitch bend, clock, ...) passes through.
    midiOut.send(msg)
end
