--[[
@target stoermelder MIDI-KIT
@engine minilua@v1
@author stoermelder
@description Routes MIDI input 1 to exactly one of up to four MIDI output ports, switched by a trigger
--]]

-- Port router for MIDI-KIT - the opposite of Smart merge
--
-- Sends everything that arrives on MIDI input 1 to exactly one output - the
-- active output. Every trigger on trigger input 1 steps to the next output and
-- wraps around after the last one (config.numOutputs). The message is unchanged
-- and only the active output receives it.
--
-- So nothing hangs on the output that is left, a switch first sends a Note-Off
-- to the old output for every note still held. At most 128 messages can be
-- created per callback, so at most 128 held notes are released per switch.
--
-- Set config.numOutputs (2-4) below or in the "Number of outputs" context menu;
-- the choice is remembered with the patch, as is the active output. The
-- "Active output" context menu lists exactly the enabled outputs.

-- Configuration - change these values as needed
local config = {
    -- Number of MIDI outputs to switch between (2-4)
    numOutputs = rack.getConfig("numOutputs", 2)
}

local MAX_OUTPUTS = 4

-- Most messages that can be created in one callback (engine limit)
local MAX_MESSAGES = 128

-- Internal state.
-- active: number (1-based) of the output currently receiving everything.
-- held: notes sent to the active output and not released yet, as { channel, note }.
local active = 1
local held = {}

-- Keeps `held` in step with the notes sent to the active output.
local function trackHeld(msg, msgType)
    if msgType ~= midi.NOTE_ON and msgType ~= midi.NOTE_OFF then return end
    local ch, note = midi.getChannel(msg), midi.getNote(msg)
    for i = 1, #held do
        if held[i][1] == ch and held[i][2] == note then
            table.remove(held, i)
            break
        end
    end
    if msgType == midi.NOTE_ON then
        held[#held + 1] = { ch, note }
    end
end

-- Releases the notes held on the active output.
local function releaseHeld()
    midiOut.selectPort(active)
    for i = 1, math.min(#held, MAX_MESSAGES) do
        local off = midi.create()
        midi.setNoteOff(off, held[i][1], held[i][2])
        midiOut.send(off)
    end
    held = {}
end

local function switchTo(out)
    if out == active then return end
    releaseHeld()
    active = out
    rack.setConfig("activeOutput", out)
    rack.log("Output: ", out, "/", config.numOutputs)
end

-- (Re-)registers the "Active output" menu with one entry per configured output.
-- Registering a label that already exists replaces that menu item, which is how
-- the list follows a change of config.numOutputs.
local function registerActiveOutputMenu()
    local outputs = {}
    for i = 1, config.numOutputs do outputs[i] = { "Output " .. i, i } end

    rack.registerContextMenu({
        type = "options",
        label = "Active output",
        options = outputs,
        onGetValue = function()
            return active
        end,
        onChange = function(out)
            switchTo(out)
        end
    })
end

-- Setup
rack.onLoad = function()
    if config.numOutputs < 2 then config.numOutputs = 2 end
    if config.numOutputs > MAX_OUTPUTS then config.numOutputs = MAX_OUTPUTS end
    midiOut.enablePorts(config.numOutputs)

    -- The trigger input steps to the next MIDI output
    trig.enableIn(1, 1)

    active = rack.getConfig("activeOutput", 1)
    if active < 1 or active > config.numOutputs then active = 1 end

    -- Context menu - right-click the module to change these settings live.
    -- Each menu mirrors a `config` value above; onChange applies the choice.
    rack.registerContextMenu({
        type = "options",
        label = "Number of outputs",
        options = { {"2", 2}, {"3", 3}, {"4", 4} },
        onGetValue = function()
            return config.numOutputs
        end,
        onChange = function(count)
            config.numOutputs = count
            rack.setConfig("numOutputs", config.numOutputs)
            midiOut.enablePorts(config.numOutputs)
            -- The active output may be gone now
            if active > config.numOutputs then switchTo(config.numOutputs) end
            registerActiveOutputMenu()
            rack.log("Outputs: ", config.numOutputs)
        end
    })

    registerActiveOutputMenu()

    rack.log("MIDI port router initialized")
    rack.log("Outputs: ", config.numOutputs, ", active: ", active)
end

-- Callbacks

-- Held notes are still down on the output when the script goes away - release them.
rack.onUnload = function()
    releaseHeld()
end

trig.onTrigger = function(trigPort, channel)
    switchTo(active % config.numOutputs + 1)
end

midi.onMessage = function(midiPort, msg, msgType)
    trackHeld(msg, msgType)
    midiOut.selectPort(active)
    midiOut.send(msg)
end
