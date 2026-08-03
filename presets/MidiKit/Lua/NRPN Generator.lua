--[[
@target stoermelder MIDI-KIT
@engine Lua
@author stoermelder
@description NRPN test generator for nrpn_to_cc.lua - sweeps a 14-bit value driven by MIDI clock
--]]

-- Companion script for a second MIDI-KIT instance.
-- Emits a spec-compliant NRPN message (CC 98/99 = parameter number, CC 38/6 = 14-bit
-- data entry value) built with midi.createNRPN()/midi.setNRPN().
-- Feed MIDI clock (0xF8, e.g. from a DAW or another MIDI-KIT clock source) into this
-- instance's MIDI IN. On every step it sends the next value of a sweeping 14-bit
-- value for the configured NRPN number.
-- Route this instance's MIDI OUT into the MIDI IN of the instance running
-- nrpn_to_cc.lua (e.g. via a virtual MIDI loopback port) to see the conversion happen.


-- Configuration - change these values as needed
local config = {
    -- MIDI channel used for the NRPN message (1-16, default: 1)
    channel = 1,

    -- NRPN parameter number to send (0-16383, default: 0)
    nrpnNumber = 0,

    -- Clock ticks per step (24 ppqn, default: 8)
    ticksPerStep = 8,

    -- Amount the 14-bit value changes per step
    stepSize = 16,

    -- Maximum 14-bit value (0-16383)
    maxValue = 16383
}

-- Internal state
local state = {
    tickCount = 0,
    value = 0,
    direction = 1
}

rack.onLoad = function()
    rack.log("NRPN generator initialized")
    rack.log("Channel: ", config.channel)
    rack.log("NRPN number: ", config.nrpnNumber)
    rack.log("Ticks per step: ", config.ticksPerStep)
end

local function sendNrpn()
    local nrpn = midi.createNRPN()
    midi.setNRPN(nrpn, config.channel, config.nrpnNumber, state.value)
    midiOut.send(nrpn)

    rack.log("Sent nrpn #", config.nrpnNumber, " = ", state.value)
end

local function advanceValue()
    state.value = state.value + state.direction * config.stepSize
    if state.value >= config.maxValue then
        state.value = config.maxValue
        state.direction = -1
    elseif state.value <= 0 then
        state.value = 0
        state.direction = 1
    end
end

-- Context menu - right-click the module to change these settings live.
-- Each menu mirrors a `config` value above; onChange applies the choice.
local CHANNEL_LABELS = {}
for c = 1, 16 do CHANNEL_LABELS[c] = tostring(c) end
local TICKS_PER_STEP = { 1, 2, 4, 8, 16, 24 }
local TICKS_LABELS = { "1", "2", "4", "8 (16th)", "16 (8th)", "24 (quarter)" }

local function ticksIndex()
    for i = 1, #TICKS_PER_STEP do
        if TICKS_PER_STEP[i] == config.ticksPerStep then return i - 1 end
    end
    return 0
end

rack.registerContextMenu({
    type = "options",
    label = "Channel",
    options = CHANNEL_LABELS,
    selected = config.channel - 1,
    onChange = function(idx)
        config.channel = idx + 1
        rack.log("Channel: ", config.channel)
    end
})

rack.registerContextMenu({
    type = "options",
    label = "Ticks per step",
    options = TICKS_LABELS,
    selected = ticksIndex(),
    onChange = function(idx)
        config.ticksPerStep = TICKS_PER_STEP[idx + 1]
        rack.log("Ticks per step: ", config.ticksPerStep)
    end
})

rack.onMidiMessage = function(midiPort, msg)
    if midi.isClock(msg) then
        state.tickCount = state.tickCount + 1
        if state.tickCount >= config.ticksPerStep then
            state.tickCount = 0
            advanceValue()
            sendNrpn()
        end
    elseif midi.isStart(msg) or midi.isContinue(msg) then
        state.tickCount = 0
    end
end
