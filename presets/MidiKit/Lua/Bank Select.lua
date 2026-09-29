--[[
@target stoermelder MIDI-KIT
@engine minilua@v1
@author stoermelder
@description Bank Select + Program Change by knobs (trigger 1) or next preset (trigger 2), e.g. for the Arturia Microfreak
--]]

-- Selects a program on any device that uses MIDI Bank Select (CC 0 = MSB,
-- CC 32 = LSB) followed by a Program Change. The example is the Arturia
-- Microfreak: it has 512 presets, but a Program Change only spans 0-127, so
-- the presets are organised in banks of 128:
--
--   Bank 0 => presets   0-127
--   Bank 1 => presets 128-255
--   Bank 2 => presets 256-383
--   Bank 3 => presets 384-511
--
-- The bank number is sent as the 14-bit Bank Select value: CC 0 carries the
-- MSB (the bank number itself), CC 32 the LSB (0). The number of banks is
-- config.banks; knob 1 is divided evenly into that many banks.
--
-- Knob 1 selects the bank and knob 2 the program within it. Nothing is sent
-- while turning the knobs: a trigger on trigger input 1 (channel 1) sends
-- Bank Select and the Program Change, in that order.
--
-- A trigger on trigger input 2 (channel 1) steps to the next preset instead:
-- it continues from the last preset sent (or from the knobs, if none has
-- been sent yet), moves on to the next bank after the last program of a bank
-- and wraps around to bank 0 after the last bank. Every change is written to
-- the module log.

-- Configuration - change these values as needed
local config = {
    -- MIDI channel (1-16) the device listens on
    channel = rack.getConfig("channel", 1),

    -- Number of banks of 128 presets (the Microfreak has 4)
    banks = 4,

    -- Show each sent preset in the on-panel overlay
    showOverlay = true
}

local PRESETS_PER_BANK = 128
local CHANNEL_LABELS = {}
for c = 1, 16 do CHANNEL_LABELS[c] = tostring(c) end

-- Absolute index (bank * 128 + program) of the preset sent last, -1 if none
local current = -1

-- 0-based bank number from knob 1
local function bankIndex()
    return math.min(config.banks - 1, math.floor(param.getValue(1) * config.banks))
end

-- 0-based program number in the bank from knob 2
local function programNumber()
    return math.min(PRESETS_PER_BANK - 1, math.floor(param.getValue(2) * PRESETS_PER_BANK))
end

local function totalPresets()
    return config.banks * PRESETS_PER_BANK
end

local function sendPreset(index)
    local bank = index // PRESETS_PER_BANK
    local program = index % PRESETS_PER_BANK
    current = index

    -- Bank Select: CC 0 = MSB (bank), CC 32 = LSB (0), sent as an atomic pair
    local bankMsg = midi.createCc14bit()
    midi.setCc14bit(bankMsg, config.channel, 0, bank)
    midiOut.send(bankMsg)

    local programMsg = midi.create()
    midi.setProgramChange(programMsg, config.channel, program)
    midiOut.send(programMsg)

    rack.log("Preset " .. index .. " (bank " .. bank .. ", program " .. program .. ", MIDI ch " .. config.channel .. ")")

    if config.showOverlay then
        rack.overlay(
            "Bank Select: preset " .. index,
            "Bank " .. bank .. ", program " .. program,
            "MIDI ch " .. config.channel)
    end
end

-- Setup
rack.onLoad = function()
    param.enable(1)
    param.enable(2)
    trig.enableIn(1, 1)
    trig.enableIn(2, 1)

    -- Context menu - right-click the module to change the MIDI channel live.
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
            rack.log("Channel: ", config.channel)
        end
    })
end

-- Callbacks
param.getName = function(i)
    if i == 1 then return "Bank" end
    if i == 2 then return "Program in bank" end
    return ""
end

param.getValueFormat = function(i)
    if i == 1 then return number.toString(bankIndex()) end
    if i == 2 then return number.toString(programNumber()) end
    return number.toString(param.getValue(i))
end

trig.onTrigger = function(trigPort, channel)
    if trigPort == 2 then
        -- Next preset: continue from the last one sent, or from the knobs
        local from = current
        if from < 0 then from = bankIndex() * PRESETS_PER_BANK + programNumber() end
        sendPreset((from + 1) % totalPresets())
    else
        sendPreset(bankIndex() * PRESETS_PER_BANK + programNumber())
    end
end

-- Pass all incoming MIDI through unchanged
midi.onMessage = function(midiPort, msg)
    midiOut.send(msg)
end
