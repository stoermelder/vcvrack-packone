/**
 * @target stoermelder MIDI-KIT
 * @engine QuickJs@v1
 * @author stoermelder
 * @description Sends a Program Change on each trigger, with the program number selected by V/Oct on CV input 1 (0V = program 0)
 */

// CV input 1 is read as V/Oct: 0V = program 0, each semitone (1/12 V) adds
// one, so C0 = 0, C#0 = 1, ... up to program 127 at about 10.58V. The
// voltage is sampled when the trigger input (channel 1) fires, and a Program
// Change for the note it selects is sent - the module has no callback for a
// changing CV, so the trigger acts as the "send" (sample & hold) clock.
// Negative voltages select program 0.

// Configuration - change these values as needed
const config = {
    // MIDI channel (1-16) the Program Change messages are sent on
    channel: rack.getConfig("channel", 1),

    // Show each sent program in the on-panel overlay
    showOverlay: true
};

// Note name of a program number under the same mapping (0 = C0)
const NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
const CHANNEL_LABELS = [];
for (let c = 1; c <= 16; c++) CHANNEL_LABELS[CHANNEL_LABELS.length] = String(c);

function noteName(program) {
    return NOTE_NAMES[program % 12] + Math.floor(program / 12);
}

// Setup
rack.onLoad = function() {
    input.enable(1);
    trig.enableIn(1, 1);

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
};

// Callbacks
input.getName = function(port) {
    if (port === 1) return "Program (V/Oct)";
    return "";
};

trig.onTrigger = function(trigPort, channel) {
    let program = Math.floor(input.getVoltage(1) * 12 + 0.5);
    if (program < 0) program = 0;
    if (program > 127) program = 127;

    const msg = midi.create();
    midi.setProgramChange(msg, config.channel, program);
    midiOut.send(msg);

    if (config.showOverlay) {
        rack.overlay(
            "Program Change " + program,
            input.getVoltage(1).toFixed(2) + " V = " + noteName(program),
            "MIDI ch " + config.channel);
    }
};

// Pass all incoming MIDI through unchanged
midi.onMessage = function(midiPort, msg) {
    midiOut.send(msg);
};
