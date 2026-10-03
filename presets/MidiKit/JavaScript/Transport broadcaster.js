/**
 * @target stoermelder MIDI-KIT
 * @engine QuickJs@v1
 * @author stoermelder
 * @description Broadcasts a clock on trigger input 1, plus start and stop from the context menu, to other MIDI-KIT modules running "Transport follower"
 */

// Transport broadcaster for MIDI-KIT
//
// Sends transport events to every other MIDI-KIT module whose script defines
// rack.onBroadcast, without a cable: a "clock" message on every pulse of trigger
// input 1, and "start"/"stop" from the "Running" item in the context menu.
// Unloading a running script sends "stop". Pair it with "Transport follower",
// which turns the messages into MIDI clock, start and stop.
//
// The second argument is the topic. A receiver gets every broadcast and tells
// them apart by the topic it is passed as the second argument of onBroadcast.


let running = false;

function setRunning(value) {
    running = value;
    let state = running ? "start" : "stop";
    let n = rack.sendBroadcast({ state: state }, "transport");

    rack.log("Transport " + state + " sent to " + number.toString(n) + " module(s)");
}

rack.onLoad = function() {
    trig.enableIn(1);

    rack.registerContextMenu({
        type: "boolean",
        label: "Running",
        onGetValue: function() { return running; },
        onChange: function(checked) { setRunning(checked); }
    });
};

// A follower must not keep running after this script is gone.
rack.onUnload = function() {
    if (running) setRunning(false);
};

trig.onTrigger = function(trigPort, channel) {
    rack.sendBroadcast({}, "clock");
};
