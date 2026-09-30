--[[
@target stoermelder MIDI-KIT
@engine minilua@v1
@author stoermelder
@description Stateful MIDI merge: forwards one of up to 4 MIDI inputs, switched by a trigger, and replays the new input's held notes and CCs on every switch
--]]

-- Smart merge for MIDI-KIT
--
-- Routes exactly one MIDI input to the output at a time - the active input.
-- Every trigger on trigger input 1 steps to the next input and wraps around
-- after the last one (config.numInputs). Messages from the inactive inputs are
-- not forwarded.
--
-- What makes it "smart": a plain switch would leave the output in the state of
-- the input that was active before. So all inputs are tracked all the time -
-- the last value of every CC and every held note - and on each switch the
-- output is brought to the state of the new input:
--
--   1. Note-Offs for the notes held on the old input, so nothing hangs.
--   2. The last value of every CC the new input has sent, in the order the CCs
--      were first seen.
--   3. Note-Ons for the notes currently held on the new input, in press order.
--
-- Only 128 messages can be created per callback, so a switch replays at most 128
-- messages in total (Note-Offs first); if more are needed the rest is dropped
-- and a line is written to the log.
--
-- Set config.numInputs (2-4) below or in the "Number of inputs" context menu;
-- the choice is remembered with the patch. The MIDI inputs 1..n are enabled
-- automatically, and the "Active input" context menu lists exactly those.

-- Configuration - change these values as needed
local config = {
    -- Number of MIDI inputs to merge (2-4)
    numInputs = rack.getConfig("numInputs", 2)
}

-- Most messages that can be created in one callback (engine limit)
local MAX_MESSAGES = 128

-- Context menu choices
local INPUT_COUNT_LABELS = { "2", "3", "4" }

-- Internal state.
-- active: number (1-based) of the input currently routed to the output.
-- inputs[i] is what input i has sent so far:
--   ccValue[key] / noteVel[key]: last CC value / velocity of a held note, where
--   key = (channel - 1) * 128 + controller or note number.
--   ccOrder / noteOrder: the keys in the order they first arrived (a note is
--   removed again on release), so a switch replays them in the original order.
local state = {
    active = 1,
    inputs = {}
}

local function newInputState()
    return { ccValue = {}, ccOrder = {}, noteVel = {}, noteOrder = {} }
end

local function keyOf(ch, number)
    return (ch - 1) * 128 + number
end

local function channelOf(key)
    return key // 128 + 1
end

local function numberOf(key)
    return key % 128
end

local function removeKey(list, key)
    for i = 1, #list do
        if list[i] == key then
            table.remove(list, i)
            return
        end
    end
end

-- Updates the tracked state of input `idx` with one incoming message.
local function track(idx, msg)
    local input = state.inputs[idx]

    if midi.isNoteOn(msg) and midi.getValue(msg) > 0 then
        local key = keyOf(midi.getChannel(msg), midi.getNote(msg))
        -- A re-press while held only updates the velocity, not the position.
        if input.noteVel[key] == nil then input.noteOrder[#input.noteOrder + 1] = key end
        input.noteVel[key] = midi.getValue(msg)
    elseif midi.isNoteOff(msg) or midi.isNoteOn(msg) then
        -- Note-Off, or the Note-On with velocity 0 that stands for one
        local key = keyOf(midi.getChannel(msg), midi.getNote(msg))
        input.noteVel[key] = nil
        removeKey(input.noteOrder, key)
    elseif midi.isCc(msg) then
        local key = keyOf(midi.getChannel(msg), midi.getControl(msg))
        -- Later changes update the value; the position stays where the CC was first seen.
        if input.ccValue[key] == nil then input.ccOrder[#input.ccOrder + 1] = key end
        input.ccValue[key] = midi.getValue(msg)
    end
end

-- Brings the output from the state of input `from` to the state of input `to`.
local function replayState(from, to)
    local left = MAX_MESSAGES
    local dropped = 0
    local old = state.inputs[from]
    local nxt = state.inputs[to]

    for i = 1, #old.noteOrder do
        if left == 0 then
            dropped = dropped + 1
        else
            local key = old.noteOrder[i]
            local off = midi.create()
            midi.setNoteOff(off, channelOf(key), numberOf(key))
            midiOut.send(off)
            left = left - 1
        end
    end

    for i = 1, #nxt.ccOrder do
        if left == 0 then
            dropped = dropped + 1
        else
            local key = nxt.ccOrder[i]
            local cc = midi.create()
            midi.setCc(cc, channelOf(key), numberOf(key), nxt.ccValue[key])
            midiOut.send(cc)
            left = left - 1
        end
    end

    for i = 1, #nxt.noteOrder do
        if left == 0 then
            dropped = dropped + 1
        else
            local key = nxt.noteOrder[i]
            local on = midi.create()
            midi.setNoteOn(on, channelOf(key), numberOf(key), nxt.noteVel[key])
            midiOut.send(on)
            left = left - 1
        end
    end

    if dropped > 0 then
        rack.log("Switch replayed ", MAX_MESSAGES, " messages, ", dropped, " dropped (limit per callback)")
    end
end

local function switchTo(idx)
    if idx == state.active then return end
    local from = state.active
    state.active = idx
    replayState(from, idx)
    rack.setConfig("activeInput", idx)
    rack.log("Input: ", idx, "/", config.numInputs)
end

-- (Re-)registers the "Active input" menu with one entry per configured input.
-- Registering a label that already exists replaces that menu item, which is how
-- the list follows a change of config.numInputs.
local function registerActiveInputMenu()
    local labels = {}
    for i = 1, config.numInputs do labels[i] = "Input " .. i end

    rack.registerContextMenu({
        type = "options",
        label = "Active input",
        options = labels,
        onGetValue = function()
            return state.active - 1
        end,
        onChange = function(idx)
            switchTo(idx + 1)
        end
    })
end

-- Setup
rack.onLoad = function()
    if config.numInputs < 2 then config.numInputs = 2 end
    if config.numInputs > 4 then config.numInputs = 4 end
    midi.enablePorts(config.numInputs)

    -- The trigger input steps to the next MIDI input
    trig.enableIn(1, 1)

    for i = 1, 4 do
        state.inputs[i] = newInputState()
    end
    state.active = rack.getConfig("activeInput", 1)
    if state.active < 1 or state.active > config.numInputs then state.active = 1 end

    -- Context menu - right-click the module to change these settings live.
    -- Each menu mirrors a `config` value above; onChange applies the choice.
    rack.registerContextMenu({
        type = "options",
        label = "Number of inputs",
        options = INPUT_COUNT_LABELS,
        onGetValue = function()
            return config.numInputs - 2
        end,
        onChange = function(idx)
            config.numInputs = idx + 2
            rack.setConfig("numInputs", config.numInputs)
            midi.enablePorts(config.numInputs)
            -- The active input may be gone now
            if state.active > config.numInputs then switchTo(config.numInputs) end
            registerActiveInputMenu()
            rack.log("Inputs: ", config.numInputs)
        end
    })

    registerActiveInputMenu()

    rack.log("Smart merge initialized")
    rack.log("Inputs: ", config.numInputs, ", active: ", state.active)
end

-- Callbacks

-- Held notes are still down on the output when the script goes away - release them.
rack.onUnload = function()
    local held = state.inputs[state.active]
    if not held then return end
    for i = 1, math.min(#held.noteOrder, MAX_MESSAGES) do
        local key = held.noteOrder[i]
        local off = midi.create()
        midi.setNoteOff(off, channelOf(key), numberOf(key))
        midiOut.send(off)
    end
end

trig.onTrigger = function(trigPort, channel)
    switchTo(state.active % config.numInputs + 1)
end

midi.onMessage = function(midiPort, msg)
    if midiPort > config.numInputs then return end

    -- Every input is tracked, only the active one is forwarded
    track(midiPort, msg)
    if midiPort == state.active then
        midiOut.send(msg)
    end
end
