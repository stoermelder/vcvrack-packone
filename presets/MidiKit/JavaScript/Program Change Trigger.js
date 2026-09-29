/**
 * @target stoermelder MIDI-KIT
 * @engine QuickJs@v1
 * @author stoermelder
 * @description Sends a Program Change message for each trigger on up to 16 trigger channels, with a configurable program number per channel
 */

// This script listens on trigger input 1 only (MIDI-KIT has two); each of its poly channels
// (up to 16) acts as its own trigger here. A rising edge on channel N sends
// a Program Change with the program number configured for N.
// Feed trigger input 1 with a poly cable (e.g. from a merge module) - a mono
// cable only drives channel 1. Channels without an entry in
// config.programs are not enabled and do nothing.

// Configuration - change these values as needed
const config = {
    // MIDI channel (1-16) the Program Change messages are sent on
    channel: rack.getConfig("channel", 1),

    // Show each sent program in the on-panel overlay
    showOverlay: true,

    // Program numbers (0-127) sent by trigger channel 1, 2, ... 16
    programs: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15]
};

const CHANNEL_LABELS = [];
for (let c = 1; c <= 16; c++) CHANNEL_LABELS[CHANNEL_LABELS.length] = String(c);

// Setup
rack.onLoad = function() {
    // trig.onTrigger is only called for enabled trigger channels
    for (let ch = 1; ch <= config.programs.length; ch++) {
        trig.enableIn(1, ch);
    }

    // Context menu - right-click the module to change the MIDI channel live.
    rack.registerContextMenu({
        type: "options",
        label: "Channel",
        options: CHANNEL_LABELS,
        onGetValue: function() {
            return config.channel - 1;
        },
        onChange: function(idx) {
            config.channel = idx + 1;
            rack.setConfig("channel", config.channel);
            rack.log("Channel: ", config.channel);
        }
    });

    // Log the active mapping ("trigger=program")
    const pairs = [];
    for (let ch = 1; ch <= config.programs.length; ch++) {
        pairs.push(ch + "=" + config.programs[ch - 1]);
    }
    rack.log("MIDI ch " + config.channel + ": " + pairs.join(" "));
};

// Callbacks
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
