--[[
@target stoermelder MIDI-KIT
@engine minilua@v1
@author stoermelder
@description NRPN to 14-bit CC converter (assembled input)
--]]

-- NRPN to CC Converter for MidiKit — assembled-input version
--
-- Same converter as "NRPN to CC", but using MIDI-KIT's assembled-input API
-- instead of a hand-rolled state machine: a spec-compliant NRPN write
-- (CC 99/98 number MSB/LSB, then CC 6/38 value MSB/LSB) is reassembled by the
-- module into a single parameter change and delivered to midi.onNrpn. See the
-- manual "NRPN to CC" example for the worked version that shows what each CC
-- means.
--
-- The parameter arrives decoded:
--   midi.getControl(msg)  -> the NRPN number (0-16383)
--   midi.getValue(msg)    -> the combined 14-bit value (0-16383)


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

-- Context menu - right-click the module to change the output channel live.
-- Each menu mirrors a `config` value above; onChange applies the choice.
local CHANNEL_LABELS = {}
for c = 1, 16 do CHANNEL_LABELS[c] = tostring(c) end

rack.registerContextMenu({
    type = "options",
    label = "CC channel",
    options = CHANNEL_LABELS,
    onGetValue = function()
        return config.ccChannel - 1
    end,
    onChange = function(idx)
        config.ccChannel = idx + 1
        rack.log("CC channel: ", config.ccChannel)
    end
})

rack.onLoad = function()
    rack.log("NRPN to CC converter initialized")
    rack.log("Mapped NRPN numbers: ", #config.map)
    rack.log("Channel: ", config.ccChannel)
end

-- Assemble NRPN parameter changes on MIDI input port 1 into midi.onNrpn.
midi.enableNrpnIn(1)

-- No-op: NRPN parameter changes arrive in midi.onNrpn, not here. This only
-- suppresses the "no midi.onMessage" load warning. Non-NRPN messages are
-- dropped, exactly as in the manual "NRPN to CC" example.
midi.onMessage = function(midiPort, msg) end

midi.onNrpn = function(midiPort, msg)
    local nrpnNumber = midi.getControl(msg)
    local nrpnValue = midi.getValue(msg)

    local ccNumber = findCcNumber(nrpnNumber)
    if ccNumber < 0 then
        -- NRPN number not present in config.map, ignore
        return
    end

    rack.log("nrpn #", nrpnNumber, ": value=", nrpnValue, " -> cc", ccNumber)

    local cc14 = midi.createCc14bit()
    midi.setCc14bit(cc14, config.ccChannel, ccNumber, nrpnValue / 128)
    -- The pair (CC ccNumber = MSB, CC ccNumber + 32 = LSB) is sent atomically.
    midiOut.send(cc14)
end
