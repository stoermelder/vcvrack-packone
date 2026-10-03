/**
 * @target stoermelder MIDI-KIT
 * @engine QuickJs@v1
 * @author stoermelder
 * @description Turns the broadcasts of "Transport broadcaster" into MIDI clock, start and stop
 */

// Transport follower for MIDI-KIT
//
// Receives the broadcasts of "Transport broadcaster" and sends MIDI clock (F8),
// start (FA) and stop (FC) to the default output. It ignores every other
// broadcast (by topic), so it can share a patch with scripts that send other messages.
//
// Start and stop are logged; the clock is not, as it would flood the log.
//
// A broadcast arrives on the next processing pass, a few samples after it was
// sent. A module that loads later does not see earlier broadcasts.

function sendRaw(hex) {
    let msg = midi.create();
    midi.setRaw(msg, hex);
    midiOut.send(msg);
}

rack.onBroadcast = function(msg, topic) {
    if (topic === "clock") {
        sendRaw("f8");
    }
    else if (topic === "transport") {
        if (msg.state === "start") {
            rack.log("Transport start");
            sendRaw("fa");
        }
        else if (msg.state === "stop") {
            rack.log("Transport stop");
            sendRaw("fc");
        }
    }
};
