--[[
@target stoermelder MIDI-KIT
@engine Lua
@author stoermelder
@description NRPN to 14-bit CC converter
--]]

-- NRPN to CC Converter for MidiKit
-- Converts NRPN (Non-Registered Parameter Number) messages to 14-bit CC messages
--
-- A spec-compliant NRPN message is sent as 4 CC messages on the same channel:
-- - CC 98 (0x62): NRPN parameter number, LSB
-- - CC 99 (0x63): NRPN parameter number, MSB
-- - CC 38 (0x26): Data Entry value, LSB
-- - CC 6  (0x06): Data Entry value, MSB
--
-- This script accumulates all four bytes, and once a full NRPN message has been
-- received it looks up the NRPN parameter number in config.map and forwards the
-- 14-bit value as a 14-bit CC pair (CC ccNumber = MSB, CC ccNumber + 32 = LSB).
-- NRPN numbers not listed in config.map are ignored.


-- Configuration - change these values as needed
local config = {
    -- Mapping of NRPN parameter numbers to output CC numbers.
    -- Add or remove entries as needed; each nrpnNumber may appear only once.
    map = {
        { nrpnNumber = 0, ccNumber = 0 },
        { nrpnNumber = 1, ccNumber = 1 },
        { nrpnNumber = 2, ccNumber = 2 }
    },

    -- Optional: CC channel (1-16, default: 1)
    ccChannel = 1
}

-- Internal state
local state = {
    numberLsb = 0,
    numberMsb = 0,
    valueLsb = 0,
    valueMsb = 0,
    hasNumberLsb = false,
    hasNumberMsb = false,
    hasValueLsb = false,
    hasValueMsb = false
}

-- Returns the CC number mapped to nrpnNumber, or -1 if not mapped
local function findCcNumber(nrpnNumber)
    local ccNumber = -1
    for i = 1, #config.map do
        if config.map[i].nrpnNumber == nrpnNumber then
            ccNumber = config.map[i].ccNumber
            break
        end
    end
    return ccNumber
end

local function resetState()
    state.hasNumberLsb = false
    state.hasNumberMsb = false
    state.hasValueLsb = false
    state.hasValueMsb = false
end

local function init()
    log("NRPN to CC converter initialized")
    log("Mapped NRPN numbers: " .. #config.map)
    log("Channel: " .. config.ccChannel)
end

function processMidi(midiPort, msg)
    if not midi.isCc(msg) then
        return
    end

    local cc = midi.getNote(msg) -- CC number
    local value = midi.getValue(msg) -- CC value (0-127)

    if cc == 98 then -- NRPN number LSB
        state.numberLsb = value
        state.hasNumberLsb = true
    elseif cc == 99 then -- NRPN number MSB
        state.numberMsb = value
        state.hasNumberMsb = true
    elseif cc == 38 then -- Data entry LSB
        state.valueLsb = value
        state.hasValueLsb = true
    elseif cc == 6 then -- Data entry MSB
        state.valueMsb = value
        state.hasValueMsb = true
    else
        return
    end

    -- Once all four bytes of the NRPN message have arrived, convert and forward
    if state.hasNumberLsb and state.hasNumberMsb and state.hasValueLsb and state.hasValueMsb then
        local nrpnNumber = (state.numberMsb << 7) | state.numberLsb
        local nrpnValue = (state.valueMsb << 7) | state.valueLsb
        resetState()

        local ccNumber = findCcNumber(nrpnNumber)
        if ccNumber < 0 then
            -- NRPN number not present in config.map, ignore
            return
        end

        log("nrpn #" .. nrpnNumber .. ": value=" .. nrpnValue .. " -> cc" .. ccNumber)

        local ccMsb = midi.create()
        local ccLsb = midi.create()
        midi.setCc14bit(ccMsb, ccLsb, config.ccChannel, ccNumber, nrpnValue / 128)
        midiOut.send(ccMsb)
        midiOut.send(ccLsb)
    end
end

-- Initialize when script loads
init()
