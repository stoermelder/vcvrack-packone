/**
 * @target stoermelder MIDI-KIT
 * @engine QuickJs@v1
 * @author stoermelder
 * @description Routes MIDI input 1 to exactly one of up to four MIDI output ports, switched by a trigger
 */

// Port router for MIDI-KIT - the opposite of Smart merge
//
// Sends everything that arrives on MIDI input 1 to exactly one output - the
// active output. Every trigger on trigger input 1 steps to the next output and
// wraps around after the last one (config.numOutputs). The message is unchanged
// and only the active output receives it.
//
// So nothing hangs on the output that is left, a switch first sends a Note-Off
// to the old output for every note still held. At most 128 messages can be
// created per callback, so at most 128 held notes are released per switch.
//
// Set config.numOutputs (2-4) below or in the "Number of outputs" context menu;
// the choice is remembered with the patch, as is the active output. The
// "Active output" context menu lists exactly the enabled outputs.

// Configuration - change these values as needed
const config = {
    // Number of MIDI outputs to switch between (2-4)
    numOutputs: rack.getConfig("numOutputs", 2)
};

const MAX_OUTPUTS = 4;

// Most messages that can be created in one callback (engine limit)
const MAX_MESSAGES = 128;

// Internal state.
// active: number (1-based) of the output currently receiving everything.
// held: notes sent to the active output and not released yet, as [channel, note].
let active = 1;
let held = [];

// Keeps `held` in step with the notes sent to the active output.
function trackHeld(msg) {
    if (!(midi.isNoteOn(msg) || midi.isNoteOff(msg))) return;
    const ch = midi.getChannel(msg), note = midi.getNote(msg);
    held = held.filter(function(n) { return n[0] !== ch || n[1] !== note; });
    if (midi.isNoteOn(msg) && midi.getValue(msg) > 0) held.push([ch, note]);
}

// Releases the notes held on the active output.
function releaseHeld() {
    midiOut.selectPort(active);
    for (let i = 0; i < Math.min(held.length, MAX_MESSAGES); i++) {
        const off = midi.create();
        midi.setNoteOff(off, held[i][0], held[i][1]);
        midiOut.send(off);
    }
    held = [];
}

function switchTo(out) {
    if (out === active) return;
    releaseHeld();
    active = out;
    rack.setConfig("activeOutput", out);
    rack.log("Output: ", out, "/", config.numOutputs);
}

// (Re-)registers the "Active output" menu with one entry per configured output.
// Registering a label that already exists replaces that menu item, which is how
// the list follows a change of config.numOutputs.
function registerActiveOutputMenu() {
    const outputs = [];
    for (let i = 1; i <= config.numOutputs; i++) outputs.push(["Output " + i, i]);

    rack.registerContextMenu({
        type: "options",
        label: "Active output",
        options: outputs,
        onGetValue: function() {
            return active;
        },
        onChange: function(out) {
            switchTo(out);
        }
    });
}

// Setup
rack.onLoad = function() {
    if (config.numOutputs < 2) config.numOutputs = 2;
    if (config.numOutputs > MAX_OUTPUTS) config.numOutputs = MAX_OUTPUTS;
    midiOut.enablePorts(config.numOutputs);

    // The trigger input steps to the next MIDI output
    trig.enableIn(1, 1);

    active = rack.getConfig("activeOutput", 1);
    if (active < 1 || active > config.numOutputs) active = 1;

    // Context menu - right-click the module to change these settings live.
    // Each menu mirrors a `config` value above; onChange applies the choice.
    rack.registerContextMenu({
        type: "options",
        label: "Number of outputs",
        options: [["2", 2], ["3", 3], ["4", 4]],
        onGetValue: function() {
            return config.numOutputs;
        },
        onChange: function(count) {
            config.numOutputs = count;
            rack.setConfig("numOutputs", config.numOutputs);
            midiOut.enablePorts(config.numOutputs);
            // The active output may be gone now
            if (active > config.numOutputs) switchTo(config.numOutputs);
            registerActiveOutputMenu();
            rack.log("Outputs: ", config.numOutputs);
        }
    });

    registerActiveOutputMenu();

    rack.log("MIDI port router initialized");
    rack.log("Outputs: ", config.numOutputs, ", active: ", active);
};

// Callbacks

// Held notes are still down on the output when the script goes away - release them.
rack.onUnload = function() {
    releaseHeld();
};

trig.onTrigger = function(trigPort, channel) {
    switchTo(active % config.numOutputs + 1);
};

midi.onMessage = function(midiPort, msg) {
    trackHeld(msg);
    midiOut.selectPort(active);
    midiOut.send(msg);
};
