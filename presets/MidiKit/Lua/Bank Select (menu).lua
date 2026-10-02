--[[
@target stoermelder MIDI-KIT
@engine minilua@v1
@author stoermelder
@description Bank Select + Program Change from the context menu only, e.g. for the Arturia Microfreak
--]]

-- Selects a program on any device that uses MIDI Bank Select (CC 0 = MSB,
-- CC 32 = LSB) followed by a Program Change, driven entirely by the
-- module's right-click menu: no knobs, no trigger inputs and no MIDI input
-- is used. The example is the Arturia Microfreak: it has 512 presets, but a
-- Program Change only spans 0-127, so the presets are organised in banks
-- of 128:
--
--   Bank 0 => presets   0-127
--   Bank 1 => presets 128-255
--   Bank 2 => presets 256-383
--   Bank 3 => presets 384-511
--
-- The bank number is sent as the 14-bit Bank Select value: CC 0 carries the
-- MSB (the bank number itself), CC 32 the LSB (0). The number of banks is
-- config.banks.
--
-- The menu has four items: Channel, Bank, Program group (the program in
-- steps of 16) and Program (the 16 programs of the chosen group). Picking
-- an entry of Bank or Program sends Bank Select and the Program Change
-- immediately, in that order. Program group only changes which programs the
-- Program item lists, it sends nothing. The selection is saved with the
-- patch, but nothing is sent when the patch loads.

-- Configuration - change these values as needed
local config = {
    -- MIDI channel (1-16) the device listens on
    channel = math.floor(rack.getConfig("channel", 1)),

    -- Number of banks of 128 presets (the Microfreak has 4)
    banks = 4,

    -- Show each sent preset in the on-panel overlay
    showOverlay = true
}

local PRESETS_PER_BANK = 128
local GROUP_SIZE = 16
local GROUPS = PRESETS_PER_BANK // GROUP_SIZE

local CHANNEL_LABELS = {}
for c = 1, 16 do CHANNEL_LABELS[c] = tostring(c) end

local BANK_LABELS = {}
for b = 1, config.banks do BANK_LABELS[b] = tostring(b - 1) end

local GROUP_LABELS = {}
for g = 0, GROUPS - 1 do
    GROUP_LABELS[g + 1] = (g * GROUP_SIZE) .. "-" .. (g * GROUP_SIZE + GROUP_SIZE - 1)
end

-- Current selection, restored from the patch. Config numbers come back as
-- floats; math.floor makes them integers, or tostring() would give "53.0".
local bank = math.min(config.banks - 1, math.floor(rack.getConfig("bank", 0)))
local program = math.min(PRESETS_PER_BANK - 1, math.floor(rack.getConfig("program", 0)))

local function sendPreset()
    -- Bank Select: CC 0 = MSB (bank), CC 32 = LSB (0), sent as an atomic pair
    local bankMsg = midi.createCc14bit()
    midi.setCc14bit(bankMsg, config.channel, 0, bank)
    midiOut.send(bankMsg)

    local programMsg = midi.create()
    midi.setProgramChange(programMsg, config.channel, program)
    midiOut.send(programMsg)

    local index = bank * PRESETS_PER_BANK + program
    rack.log("Preset " .. index .. " (bank " .. bank .. ", program " .. program .. ", MIDI ch " .. config.channel .. ")")

    if config.showOverlay then
        rack.overlay(
            "Bank Select: preset " .. index,
            "Bank " .. bank .. ", program " .. program,
            "MIDI ch " .. config.channel)
    end
end

-- The Program item lists the 16 programs of the current group with their
-- absolute numbers. Registering it again under the same label replaces it in
-- place, which is how the labels follow the group.
local function registerProgramMenu()
    local first = (program // GROUP_SIZE) * GROUP_SIZE
    local labels = {}
    for i = 1, GROUP_SIZE do labels[i] = tostring(first + i - 1) end
    rack.registerContextMenu({
        type = "options",
        label = "Program",
        options = labels,
        onGetValue = function()
            return program % GROUP_SIZE
        end,
        onChange = function(idx)
            program = (program // GROUP_SIZE) * GROUP_SIZE + idx
            rack.setConfig("program", program)
            sendPreset()
        end
    })
end

-- Setup
rack.onLoad = function()
    rack.registerContextMenu({
        type = "options",
        label = "Channel",
        options = CHANNEL_LABELS,
        onGetValue = function()
            return config.channel - 1
        end,
        onChange = function(idx)
            config.channel = idx + 1
            rack.setConfig("channel", config.channel)
        end
    })

    rack.registerContextMenu({
        type = "options",
        label = "Bank",
        options = BANK_LABELS,
        onGetValue = function()
            return bank
        end,
        onChange = function(idx)
            bank = idx
            rack.setConfig("bank", bank)
            sendPreset()
        end
    })

    rack.registerContextMenu({
        type = "options",
        label = "Program group",
        options = GROUP_LABELS,
        onGetValue = function()
            return program // GROUP_SIZE
        end,
        onChange = function(idx)
            -- Keep the position within the group
            program = idx * GROUP_SIZE + program % GROUP_SIZE
            rack.setConfig("program", program)
            registerProgramMenu()
        end
    })

    registerProgramMenu()
end
