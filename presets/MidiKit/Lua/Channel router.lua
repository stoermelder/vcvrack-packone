--[[
@target stoermelder MIDI-KIT
@engine minilua@v1
@author stoermelder
@description Routes the MIDI channels of input 1 to up to four MIDI outputs, configurable per output
--]]

-- Splits one MIDI input into up to four outputs by MIDI channel. Each output
-- gets a list of MIDI channels (1-16): a message arriving on input 1 is sent to
-- every output whose list contains its channel. The channel itself is not
-- changed.
--
-- The default configuration below sends
--
--   channels 1, 2 => output 1
--   channels 3, 5 => output 2
--   channel  4    => output 3
--   output 4      => unused
--
-- A channel may appear in several outputs (the message is copied to each of
-- them), and a channel that appears in none is dropped, unless
-- config.fallbackOutput names an output for it.
--
-- Messages without a channel (MIDI clock, start/stop/continue, SysEx) are sent
-- to every output if config.systemToAll is set, otherwise they are dropped.
--
-- Only as many outputs as config.routes has entries are enabled; the others
-- stay disabled and are not offered in the module's context menu.

-- Configuration - change these values as needed
local config = {
    -- routes[n] lists the MIDI channels (1-16) of input 1 that are sent to
    -- output n. At most four entries; an empty list leaves the output silent.
    routes = {
        { 1, 2 },   -- output 1
        { 3, 5 },   -- output 2
        { 4 },      -- output 3
        {}          -- output 4
    },

    -- Output (1-4) for channels that are in no route, 0 to drop them
    fallbackOutput = 0,

    -- Send channel-less messages (clock, start/stop, SysEx) to every output
    systemToAll = true
}

local MAX_OUTPUTS = 4

local function contains(list, value)
    for _, v in ipairs(list) do
        if v == value then return true end
    end
    return false
end

-- Built once in rack.onLoad() from config
local outputCount = 1
local dest = {}        -- dest[ch] = outputs (1-based) that MIDI channel ch (1-16) is sent to
local fallback = {}
local allOutputs = {}

rack.onLoad = function()
    outputCount = math.min(#config.routes, MAX_OUTPUTS)
    if #config.routes > MAX_OUTPUTS then
        rack.log("Channel router: only " .. MAX_OUTPUTS .. " outputs exist, ignoring the rest of config.routes")
    end
    midiOut.enablePorts(math.max(1, outputCount))

    dest = {}
    for ch = 1, 16 do dest[ch] = {} end

    for out = 1, outputCount do
        local channels = config.routes[out]
        for _, ch in ipairs(channels) do
            if ch < 1 or ch > 16 then
                rack.log("Channel router: ignoring invalid channel " .. ch .. " for output " .. out)
            elseif not contains(dest[ch], out) then
                table.insert(dest[ch], out)
            end
        end
        rack.log("Channel router: output " .. out .. " <- channels " .. (#channels > 0 and table.concat(channels, ", ") or "none"))
    end

    fallback = {}
    if config.fallbackOutput >= 1 and config.fallbackOutput <= outputCount then
        fallback = { config.fallbackOutput }
    end

    allOutputs = {}
    for out = 1, outputCount do allOutputs[out] = out end
end

-- Sends msg to each of the given outputs.
local function sendTo(msg, outputs)
    for i, out in ipairs(outputs) do
        midiOut.selectPort(out)
        midiOut.send(msg)
    end
end

midi.onMessage = function(midiPort, msg)
    local ch = midi.getChannel(msg)
    if ch < 0 then
        if config.systemToAll then sendTo(msg, allOutputs) end
        return
    end
    if #dest[ch] > 0 then
        sendTo(msg, dest[ch])
    else
        sendTo(msg, fallback)
    end
end
