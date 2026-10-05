/**
 * @target stoermelder MIDI-KIT
 * @engine QuickJs@v1
 * @author stoermelder
 * @description Stateful MIDI merge: forwards one of up to 4 MIDI inputs, switched by a trigger, and replays the new input's held notes and CCs on every switch
 */

// Smart merge for MIDI-KIT
//
// Routes exactly one MIDI input to the output at a time - the active input.
// Every trigger on trigger input 1 steps to the next input and wraps around
// after the last one (config.numInputs). Messages from the inactive inputs are
// not forwarded.
//
// What makes it "smart": a plain switch would leave the output in the state of
// the input that was active before. So all inputs are tracked all the time -
// the last value of every CC and every held note - and on each switch the
// output is brought to the state of the new input:
//
//   1. Note-Offs for the notes held on the old input, so nothing hangs.
//   2. The last value of every CC the new input has sent, in the order the CCs
//      were first seen.
//   3. Note-Ons for the notes currently held on the new input, in press order.
//
// One message handle is reused for all of them. A very large state can overflow
// the module's output queue; the module then drops the rest and says so in the
// log.
//
// Set config.numInputs (2-4) below or in the "Number of inputs" context menu;
// the choice is remembered with the patch. The MIDI inputs 1..n are enabled
// automatically, and the "Active input" context menu lists exactly those.

// Configuration - change these values as needed
let config = {
    // Number of MIDI inputs to merge (2-4)
    numInputs: rack.getConfig("numInputs", 2)
};

// Internal state.
// active: index (0-based) of the input currently routed to the output.
// inputs[i] is what input i has sent so far:
//   ccValue[key] / noteVel[key]: last CC value / velocity of a held note, where
//   key = (channel - 1) * 128 + controller or note number.
//   ccOrder / noteOrder: the keys in the order they first arrived (a note is
//   removed again on release), so a switch replays them in the original order.
let state = {
    active: 0,
    inputs: []
};

function newInputState() {
    return { ccValue: {}, ccOrder: [], noteVel: {}, noteOrder: [] };
};

function keyOf(ch, number) {
    return (ch - 1) * 128 + number;
};

function channelOf(key) {
    return Math.floor(key / 128) + 1;
};

function numberOf(key) {
    return key % 128;
};

function removeKey(list, key) {
    let i = list.indexOf(key);
    if (i >= 0) list.splice(i, 1);
};

// Updates the tracked state of input `idx` with one incoming message.
function track(idx, msg) {
    let input = state.inputs[idx];

    if (midi.isNoteOn(msg) && midi.getValue(msg) > 0) {
        let key = keyOf(midi.getChannel(msg), midi.getNote(msg));
        // A re-press while held only updates the velocity, not the position.
        if (input.noteVel[key] === undefined) input.noteOrder.push(key);
        input.noteVel[key] = midi.getValue(msg);
    }
    else if (midi.isNoteOff(msg) || midi.isNoteOn(msg)) {
        // Note-Off, or the Note-On with velocity 0 that stands for one
        let key = keyOf(midi.getChannel(msg), midi.getNote(msg));
        delete input.noteVel[key];
        removeKey(input.noteOrder, key);
    }
    else if (midi.isCc(msg)) {
        let key = keyOf(midi.getChannel(msg), midi.getControl(msg));
        // Later changes update the value; the position stays where the CC was first seen.
        if (input.ccValue[key] === undefined) input.ccOrder.push(key);
        input.ccValue[key] = midi.getValue(msg);
    }
};

// Brings the output from the state of input `from` to the state of input `to`.
function replayState(from, to) {
    let old = state.inputs[from];
    let next = state.inputs[to];
    // Sending copies the message, so one handle serves every message below.
    let m = midi.create();

    for (let i = 0; i < old.noteOrder.length; i++) {
        let key = old.noteOrder[i];
        midi.setNoteOff(m, channelOf(key), numberOf(key));
        midiOut.send(m);
    }

    for (let i = 0; i < next.ccOrder.length; i++) {
        let key = next.ccOrder[i];
        midi.setCc(m, channelOf(key), numberOf(key), next.ccValue[key]);
        midiOut.send(m);
    }

    for (let i = 0; i < next.noteOrder.length; i++) {
        let key = next.noteOrder[i];
        midi.setNoteOn(m, channelOf(key), numberOf(key), next.noteVel[key]);
        midiOut.send(m);
    }
};

function switchTo(idx) {
    if (idx === state.active) return;
    let from = state.active;
    state.active = idx;
    replayState(from, idx);
    rack.setConfig("activeInput", idx + 1);
    rack.log("Input: ", idx + 1, "/", config.numInputs);
};

// (Re-)registers the "Active input" menu with one entry per configured input.
// Registering a label that already exists replaces that menu item, which is how
// the list follows a change of config.numInputs.
function registerActiveInputMenu() {
    // The value of an option is the 0-based input index, as in state.active.
    let inputs = [];
    for (let i = 0; i < config.numInputs; i++) inputs[inputs.length] = ["Input " + (i + 1), i];

    rack.registerContextMenu({
        type: "options",
        label: "Active input",
        options: inputs,
        onGetValue: function() {
            return state.active;
        },
        onChange: function(input) {
            switchTo(input);
        }
    });
};

// Setup
rack.onLoad = function() {
    if (config.numInputs < 2) config.numInputs = 2;
    if (config.numInputs > 4) config.numInputs = 4;
    midi.enablePorts(config.numInputs);

    // The trigger input steps to the next MIDI input
    trig.enableIn(1, 1);

    for (let i = 0; i < 4; i++) {
        state.inputs[i] = newInputState();
    }
    state.active = rack.getConfig("activeInput", 1) - 1;
    if (state.active < 0 || state.active >= config.numInputs) state.active = 0;

    // Context menu - right-click the module to change these settings live.
    // Each menu mirrors a `config` value above; onChange applies the choice.
    rack.registerContextMenu({
        type: "options",
        label: "Number of inputs",
        options: [["2", 2], ["3", 3], ["4", 4]],
        onGetValue: function() {
            return config.numInputs;
        },
        onChange: function(count) {
            config.numInputs = count;
            rack.setConfig("numInputs", config.numInputs);
            midi.enablePorts(config.numInputs);
            // The active input may be gone now
            if (state.active >= config.numInputs) switchTo(config.numInputs - 1);
            registerActiveInputMenu();
            rack.log("Inputs: ", config.numInputs);
        }
    });

    registerActiveInputMenu();

    rack.log("Smart merge initialized");
    rack.log("Inputs: ", config.numInputs, ", active: ", state.active + 1);
};

// Callbacks

// Held notes are still down on the output when the script goes away - release them.
rack.onUnload = function() {
    let held = state.inputs[state.active];
    if (!held) return;
    let off = midi.create();
    for (let i = 0; i < held.noteOrder.length; i++) {
        let key = held.noteOrder[i];
        midi.setNoteOff(off, channelOf(key), numberOf(key));
        midiOut.send(off);
    }
};

trig.onTrigger = function(trigPort, channel) {
    switchTo((state.active + 1) % config.numInputs);
};

midi.onMessage = function(midiPort, msg) {
    let idx = midiPort - 1;
    if (idx >= config.numInputs) return;

    // Every input is tracked, only the active one is forwarded
    track(idx, msg);
    if (idx === state.active) {
        midiOut.send(msg);
    }
};
