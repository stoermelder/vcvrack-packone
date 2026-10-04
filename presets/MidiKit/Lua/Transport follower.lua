--[[
@target stoermelder MIDI-KIT
@engine minilua@v1
@author stoermelder
@description Turns the broadcasts of "Transport broadcaster" into MIDI clock, start and stop
--]]

-- Transport follower for MIDI-KIT
--
-- Receives the broadcasts of "Transport broadcaster" and sends MIDI clock (F8),
-- start (FA) and stop (FC) to the default output. It ignores every other
-- broadcast (by topic), so it can share a patch with scripts that send other messages.
--
-- Start and stop are logged; the clock is not, as it would flood the log.
--
-- A broadcast arrives on the next processing pass, a few samples after it was
-- sent. A module that loads later does not see earlier broadcasts.

local function sendRaw(hex)
    local msg = midi.create()
    midi.setRaw(msg, hex)
    midiOut.send(msg)
end

function rack.onBroadcast(msg, topic)
    if topic == "clock" then
        sendRaw("f8")
    elseif topic == "transport" then
        if msg.state == "start" then
            rack.log("Transport start")
            sendRaw("fa")
        elseif msg.state == "stop" then
            rack.log("Transport stop")
            sendRaw("fc")
        end
    end
end
