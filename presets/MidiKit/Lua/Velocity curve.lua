--[[
@target stoermelder MIDI-KIT
@engine Lua
@author stoermelder
@description Reshapes Note-On velocity with a knob-controlled curve, plus configurable floor and ceiling
--]]

-- Velocity curve for MIDI-KIT
--
-- Keyboards differ wildly in how hard you have to hit them to reach velocity
-- 127, and many synth patches only use a narrow slice of the range. This script
-- remaps every Note-On velocity through an adjustable curve before passing it on.
--
-- Parameter 1 on the panel sets the curve shape, live:
--
--     knob at 0.0   exponential - soft playing gets much quieter, the full
--                   range only opens up at the top of the key travel
--     knob at 0.5   linear - velocity passes through unchanged in shape
--     knob at 1.0   logarithmic - light touches already reach high velocities,
--                   useful for stiff keybeds
--
-- The result is then scaled into config.minVelocity..config.maxVelocity, so a
-- patch that sounds wrong below 40 can be given a floor without losing dynamics.
--
-- Velocity 0 is left alone: a Note-On with velocity 0 is the standard "running
-- status" spelling of a Note-Off, and lifting it off the floor would turn every
-- release into a stuck note.


-- Configuration - change these values as needed
local config = {
    -- Panel parameter driving the curve (1-based)
    curveParam = 1,

    -- Output velocity range. Incoming 1..127 is rescaled into this window.
    minVelocity = 1,
    maxVelocity = 127,

    -- Curve strength at the knob extremes. This is the exponent handed to
    -- number.rescale(), where 0 is linear; beyond about 3 the curve is so steep
    -- that most of the key travel maps to a single velocity.
    curveAmount = 2,

    -- Only process this channel; 0 = every channel
    channel = 0
}

param.enable(config.curveParam)

param.getName = function(port)
    if port == config.curveParam then return "Velocity curve" end
    return ""
end

param.getValueFormat = function(port)
    if port == config.curveParam then
        local v = param.getValue(config.curveParam)
        -- Report the curve as a signed shape amount rather than a raw 0..1,
        -- so the panel reads "-2.0 .. 0.0 .. +2.0" around linear.
        return string.format("%+.1f", (v - 0.5) * 2 * config.curveAmount)
    end
    return ""
end

rack.onLoad = function()
    rack.log("Velocity curve initialized")
    rack.log("Range: ", config.minVelocity, "-", config.maxVelocity)
    rack.log("Knob ", config.curveParam, " sets the curve (centre = linear)")
end

local function matchesChannel(ch)
    return config.channel == 0 or ch == config.channel
end

-- Maps velocity 1..127 through the curve and into the configured range.
-- number.rescale takes an optional curve argument, which is exactly the
-- exponential shaping wanted here.
local function shapeVelocity(vel)
    local knob = param.getValue(config.curveParam)
    local curve = (knob - 0.5) * 2 * config.curveAmount
    local out = number.rescale(vel, 1, 127, config.minVelocity, config.maxVelocity, curve)

    -- Guard the endpoints: rescale works in floats and can land a hair outside
    -- the window, which would produce an invalid velocity byte.
    out = math.floor(out + 0.5)
    return math.max(config.minVelocity, math.min(config.maxVelocity, out))
end

-- Context menu - right-click the module to change these settings live.
-- Each menu mirrors a `config` value above; onChange applies the choice.
local CHANNEL_LABELS = { "All" }
for c = 1, 16 do CHANNEL_LABELS[c + 1] = tostring(c) end

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

rack.onMidiMessage = function(midiPort, msg)
    if midi.isNoteOn(msg) and matchesChannel(midi.getChannel(msg)) then
        local vel = midi.getValue(msg)

        -- Velocity 0 is a Note-Off in disguise - pass it through untouched
        if vel == 0 then
            midiOut.send(msg)
            return
        end

        local shaped = shapeVelocity(vel)
        midi.setValue(msg, shaped)
    end

    midiOut.send(msg)
end
