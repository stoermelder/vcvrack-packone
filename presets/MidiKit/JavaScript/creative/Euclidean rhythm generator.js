/**
 * @target stoermelder MIDI-KIT
 * @engine QuickJs@v1
 * @author stoermelder
 * @description Euclidean rhythm generator clocked by the trigger input, with steps, fills, note and velocity params
 */

// Euclidean rhythm generator for MIDI-KIT
//
// Steps through a Euclidean (Bjorklund) rhythm on the module's trigger input
// and fires a MIDI Note-On for every hit. Euclidean rhythms spread a number of
// hits as evenly as possible across a number of steps - e.g. 5 hits in 8 steps
// gives the classic [10110110] "Bembe" pattern - which makes them the standard
// source of syncopated, non-linear percussion and bass gates.
//
// Every trigger tick advances the rhythm by one step. A hit fires a Note-On
// that sustains until the next trigger tick (a one-step gate), so the
// generator is monophonic and adjacent hits retrigger cleanly. Feed any clock
// into the trigger input (a Rack clock source, or MIDI clock forwarded to a
// trigger output elsewhere in the patch) and the rhythm follows it.
//
// param 1 - Steps: length of the bar, 1-16.
// param 2 - Fills: number of hits in the bar, 0..Steps. 0 silences the rhythm,
//   Steps fills every step.
// param 3 - Note: MIDI note number fired on each hit, 0-127.
// param 4 - Velocity: velocity of each hit, 1-127.
//
// All four params are read live and the pattern is recomputed on every trigger
// tick, so moving a knob changes the rhythm/note/velocity immediately.
//
// Everything arriving on MIDI IN is passed through unchanged - this script is
// a pure generator and does not want to swallow the rest of a MIDI chain.


// Configuration - change these values as needed
let config = {
    // Output channel for the generated notes (1-16)
    outChannel: 1
};

// Internal state.
// step: the step index played on the next trigger tick.
// pattern: the current Euclidean pattern as 1/0 flags, length = Steps.
// soundingNote: the note currently sustaining (-1 = nothing), released on the
//   next trigger tick or on unload.
let state = {
    step: -1,
    pattern: [],
    soundingNote: -1,
    soundingChannel: 1
};

param.enable(1);
param.enable(2);
param.enable(3);
param.enable(4);

// Step the rhythm from trigger channel 1 only: trig.onTrigger fires per poly
// channel, and trig.enableIn() gates it — enabling just channel 1 means the
// other channels are ignored.
trig.enableIn(1, 1);

param.getName = function(i) {
    if (i === 1) return "Steps";
    if (i === 2) return "Fills";
    if (i === 3) return "Note";
    if (i === 4) return "Velocity";
    return "";
};

function stepsParam() {
    let s = Math.round(param.getValue(1) * 15) + 1;
    if (s > 16) s = 16;
    return s;
};

function fillsParam() {
    let s = stepsParam();
    let f = Math.round(param.getValue(2) * s);
    if (f < 0) f = 0;
    if (f > s) f = s;
    return f;
};

function noteParam() {
    let n = Math.round(param.getValue(3) * 127);
    if (n < 0) n = 0;
    if (n > 127) n = 127;
    return n;
};

function velocityParam() {
    let v = Math.round(param.getValue(4) * 126) + 1;
    if (v < 1) v = 1;
    if (v > 127) v = 127;
    return v;
};

// Note names (sharps only) for the Note param readout.
let NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];

function noteName(n) {
    return NOTE_NAMES[n % 12] + (Math.floor(n / 12) - 1);
};

param.getValueFormat = function(i) {
    if (i === 1) return stepsParam() + " steps";
    if (i === 2) return fillsParam() + " / " + stepsParam() + " hits";
    if (i === 3) return noteParam() + " (" + noteName(noteParam()) + ")";
    if (i === 4) return number.toString(velocityParam());
    return number.toString(param.getValue(i));
};

// Bjorklund's algorithm: distributes `hits` pulses as evenly as possible
// among `steps` slots, returning a 1/0 array of length `steps`. This is the
// canonical source of Euclidean rhythms - see Bjorklund, "The Theory of
// Rep-Rate Pattern Generation in Synchronous Systems" (2003). 0 hits yields
// all rests and hits == steps yields all hits.
function euclid(steps, hits) {
    if (hits > steps) hits = steps;
    if (hits === 0) {
        let r = [];
        for (let i = 0; i < steps; i++) r[r.length] = 0;
        return r;
    }
    if (hits === steps) {
        let r = [];
        for (let i = 0; i < steps; i++) r[r.length] = 1;
        return r;
    }

    // Euclidean algorithm on (steps - hits) and hits, recording the quotient
    // and remainder at each level, then unwinding them into the pattern.
    let counts = [];
    let remainders = [];
    let divisor = steps - hits;
    remainders[0] = hits;
    let level = 0;
    while (true) {
        counts[level] = Math.floor(divisor / remainders[level]);
        remainders[level + 1] = divisor % remainders[level];
        divisor = remainders[level];
        level++;
        if (remainders[level] <= 1) break;
    }
    counts[level] = divisor;

    let pattern = [];
    function build(l) {
        if (l === -1) pattern[pattern.length] = 0;
        else if (l === -2) pattern[pattern.length] = 1;
        else {
            for (let i = 0; i < counts[l]; i++) build(l - 1);
            if (remainders[l] !== 0) build(l - 2);
        }
    }
    build(level);
    return pattern;
};

// Rebuilds state.pattern from the Steps/Fills params. Called on every trigger
// tick so knob changes take effect immediately.
function rebuildPattern() {
    state.pattern = euclid(stepsParam(), fillsParam());
};

// Releases whatever note the generator is currently sustaining, if any.
function releaseSounding() {
    if (state.soundingNote >= 0) {
        let off = midi.create();
        midi.setNoteOff(off, state.soundingChannel, state.soundingNote);
        midiOut.send(off);
        state.soundingNote = -1;
    }
};

rack.onLoad = function() {
    rack.log("Euclidean rhythm generator initialized");
};

rack.onUnload = function() {
    releaseSounding();
};

// Context menu - right-click the module to change these settings live.
let CHANNEL_LABELS = [];
for (let c = 1; c <= 16; c++) CHANNEL_LABELS[CHANNEL_LABELS.length] = String(c);

rack.registerContextMenu({
    type: "options",
    label: "Output channel",
    options: CHANNEL_LABELS,
    onGetValue: function() {
        return config.outChannel - 1;
    },
    onChange: function(idx) {
        config.outChannel = idx + 1;
        rack.log("Output channel: ", config.outChannel);
    }
});

midi.onMessage = function(midiPort, msg) {
    // Pure generator - pass everything from MIDI IN through unchanged so the
    // module stays transparent in a MIDI chain.
    midiOut.send(msg);
};

trig.onTrigger = function(trigPort, channel) {
    rebuildPattern();

    let steps = state.pattern.length;
    if (steps === 0) return;

    // Advance one step, wrapping at the bar end.
    state.step = (state.step + 1) % steps;

    // Cut the previous hit's note (one-step gate) before sounding the next.
    releaseSounding();

    if (state.pattern[state.step] === 1) {
        let ch = config.outChannel;
        let note = noteParam();
        let on = midi.create();
        midi.setNoteOn(on, ch, note, velocityParam());
        midiOut.send(on);
        state.soundingNote = note;
        state.soundingChannel = ch;
    }
};
