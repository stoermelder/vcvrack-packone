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
-- One message handle is reused for all of them. A very large state can overflow
-- the module's output queue; the module then drops the rest and says so in the
-- log.
--
-- Set config.numInputs (2-4) below or in the "Number of inputs" context menu;
-- the choice is remembered with the patch. The MIDI inputs 1..n are enabled
-- automatically, and the "Active input" context menu lists exactly those.

-- Configuration - change these values as needed
local config = {
    -- Number of MIDI inputs to merge (2-4)
    numInputs = rack.getConfig("numInputs", 2)
}

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
local function track(idx, msg, msgType)
    local input = state.inputs[idx]

    if msgType == midi.NOTE_ON then
        local key = keyOf(midi.getChannel(msg), midi.getNote(msg))
        -- A re-press while held only updates the velocity, not the position.
        if input.noteVel[key] == nil then input.noteOrder[#input.noteOrder + 1] = key end
        input.noteVel[key] = midi.getValue(msg)
    elseif msgType == midi.NOTE_OFF then
        -- A Note-Off, also the Note-On with velocity 0 that stands for one
        local key = keyOf(midi.getChannel(msg), midi.getNote(msg))
        input.noteVel[key] = nil
        removeKey(input.noteOrder, key)
    elseif msgType == midi.CC then
        local key = keyOf(midi.getChannel(msg), midi.getControl(msg))
        -- Later changes update the value; the position stays where the CC was first seen.
        if input.ccValue[key] == nil then input.ccOrder[#input.ccOrder + 1] = key end
        input.ccValue[key] = midi.getValue(msg)
    end
end

-- Brings the output from the state of input `from` to the state of input `to`.
local function replayState(from, to)
    local old = state.inputs[from]
    local nxt = state.inputs[to]
    -- Sending copies the message, so one handle serves every message below.
    local m = midi.create()

    for i = 1, #old.noteOrder do
        local key = old.noteOrder[i]
        midi.setNoteOff(m, channelOf(key), numberOf(key))
        midiOut.send(m)
    end

    for i = 1, #nxt.ccOrder do
        local key = nxt.ccOrder[i]
        midi.setCc(m, channelOf(key), numberOf(key), nxt.ccValue[key])
        midiOut.send(m)
    end

    for i = 1, #nxt.noteOrder do
        local key = nxt.noteOrder[i]
        midi.setNoteOn(m, channelOf(key), numberOf(key), nxt.noteVel[key])
        midiOut.send(m)
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
    local inputs = {}
    for i = 1, config.numInputs do inputs[i] = { "Input " .. i, i } end

    rack.registerContextMenu({
        type = "options",
        label = "Active input",
        options = inputs,
        onGetValue = function()
            return state.active
        end,
        onChange = function(input)
            switchTo(input)
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
        options = { {"2", 2}, {"3", 3}, {"4", 4} },
        onGetValue = function()
            return config.numInputs
        end,
        onChange = function(count)
            config.numInputs = count
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
    local off = midi.create()
    for i = 1, #held.noteOrder do
        local key = held.noteOrder[i]
        midi.setNoteOff(off, channelOf(key), numberOf(key))
        midiOut.send(off)
    end
end

trig.onTrigger = function(trigPort, channel)
    switchTo(state.active % config.numInputs + 1)
end

midi.onMessage = function(midiPort, msg, msgType)
    if midiPort > config.numInputs then return end

    -- Every input is tracked, only the active one is forwarded
    track(midiPort, msg, msgType)
    if midiPort == state.active then
        midiOut.send(msg)
    end
end
