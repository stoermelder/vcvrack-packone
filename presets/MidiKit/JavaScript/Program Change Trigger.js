/**
 * @target stoermelder MIDI-KIT
 * @engine QuickJs@v1
 * @author stoermelder
 * @description Sends a Program Change message for each trigger on up to 16 trigger channels, with a configurable program number per channel
 */

// MIDI-KIT has a single polyphonic trigger input; each of its poly channels
// (up to 16) acts as its own trigger here. A rising edge on channel N sends
// a Program Change with the program number configured for N.
// Feed the input with a poly cable (e.g. from a merge module) - a mono
// cable only drives channel 1. Channels without an entry in
// config.programs are not enabled and do nothing.

// Configuration - change these values as needed
const config = {
    // MIDI channel (1-16) the Program Change messages are sent on
    channel: 1,

    // Show each sent program in the on-panel overlay
    showOverlay: true,

    // Program numbers (0-127) sent by trigger channel 1, 2, ... 16
    programs: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15]
};

// Logs the active mapping ("trigger=program") on load
rack.onLoad = function() {
    const pairs = [];
    for (let ch = 1; ch <= config.programs.length; ch++) {
        pairs.push(ch + "=" + config.programs[ch - 1]);
    }
    rack.log("MIDI ch " + config.channel + ": " + pairs.join(" "));
};

// trig.onTrigger is only called for enabled trigger channels
for (let ch = 1; ch <= config.programs.length; ch++) {
    trig.enableIn(1, ch);
}

trig.onTrigger = function(trigPort, channel) {
    const program = config.programs[channel - 1];
    if (program === undefined) return;

    const msg = midi.create();
    midi.setProgramChange(msg, config.channel, program);
    midiOut.send(msg);

    if (config.showOverlay) {
        rack.overlay(
            "Program Change " + program,
            "Trigger " + channel + " -> program " + program,
            "MIDI ch " + config.channel);
    }
};

// Pass all incoming MIDI through unchanged
midi.onMessage = function(midiPort, msg) {
    midiOut.send(msg);
};
