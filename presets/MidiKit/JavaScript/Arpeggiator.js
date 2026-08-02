/**
 * @target stoermelder MIDI-KIT
 * @engine QuickJs
 * @author stoermelder
 * @description Arpeggiator clocked by the trigger input, with clock division, octave range, note length and playmode params
 */

// Arpeggiator for MIDI-KIT
//
// Held notes (tracked from incoming Note-On/Note-Off on config.channel) are
// stepped through in a pattern and re-sung one at a time, advanced by the
// module's trigger input rather than by incoming MIDI - feed a clock (e.g.
// from a Rack clock source, or MIDI clock forwarded to a trigger output
// elsewhere in the patch) into the trigger input and the arp follows it.
//
// param 1 - Clock division: how many trigger ticks make up one arp step.
//   Value is quantised to a musical division list, not a raw multiplier -
//   see DIVISIONS below.
// param 2 - Octave range: 1-4 octaves; the held notes are repeated one
//   octave higher each time up to this count before the pattern cycles.
// param 3 - Note length: gate length as a fraction of one step, expressed in
//   trigger ticks (min 1, capped at clockDivision - 1 so notes never tie into
//   the next step).
// param 4 - Playmode: Up / Down / Up-Down.
//
// Notes are only advanced on a trigger tick that lands on a step boundary
// (i.e. every clockDivision-th tick), so the trigger input can run at a
// finer resolution than the arp itself - the same divide-down idea as
// Clock divider.js, just applied to a CV trigger instead of MIDI clock.
//
// Silence (no keys held) simply stops stepping; the next Note-On restarts
// the pattern from its first note on the next step boundary.


// Configuration - change these values as needed
let config = {
    // Trigger input driving the arp (1-based)
    trigPort: 1,

    // Only arpeggiate notes on this channel; 0 = every channel
    channel: 0,

    // Output channel for arpeggiated notes; 0 = same as input note's channel
    outChannel: 0,

    // Show the currently playing step in the panel overlay
    showOverlay: true
};

// Clock division choices, in trigger ticks per arp step (fewer ticks = faster)
let DIVISIONS = [1, 2, 3, 4, 6, 8, 12, 16, 24, 32];

// Playmode names, selected by param 4
let PLAYMODES = ["Up", "Down", "Up-Down"];

// Internal state.
// held: note numbers currently down, in the order they were pressed.
// pattern: the expanded, octave-doubled sequence built from held+octaves+playmode.
// step: index into pattern for the note played on the next step boundary.
// tickCount: counts trigger ticks up to the current clockDivision.
// soundingNote/soundingChannel: the note+channel currently sustained by the
// arp, so it can be released before the next one starts or on unload.
let state = {
    held: [],
    pattern: [],
    step: 0,
    tickCount: 0,
    soundingNote: -1,
    soundingChannel: 1
};

param.enable(1);
param.enable(2);
param.enable(3);
param.enable(4);

param.getName = function(i) {
    if (i === 1) return "Clock division";
    if (i === 2) return "Octave range";
    if (i === 3) return "Note length";
    if (i === 4) return "Playmode";
    return "";
};

function divisionIndex() {
    let idx = Math.floor(param.getValue(1) * DIVISIONS.length);
    if (idx >= DIVISIONS.length) idx = DIVISIONS.length - 1;
    return idx;
};

function octaveRange() {
    let o = Math.floor(param.getValue(2) * 4) + 1;
    if (o > 4) o = 4;
    return o;
};

function playmodeIndex() {
    let idx = Math.floor(param.getValue(4) * PLAYMODES.length);
    if (idx >= PLAYMODES.length) idx = PLAYMODES.length - 1;
    return idx;
};

param.getValueFormat = function(i) {
    if (i === 1) return DIVISIONS[divisionIndex()] + " ticks/step";
    if (i === 2) return octaveRange() + " oct";
    if (i === 3) return number.toFixed(param.getValue(3) * 100, 0) + " %";
    if (i === 4) return PLAYMODES[playmodeIndex()];
    return number.toString(param.getValue(i));
};

function matchesChannel(ch) {
    return config.channel === 0 || ch === config.channel;
};

// Rebuilds state.pattern from state.held, the octave range and the playmode.
// Called whenever the held-note set changes so the pattern is always fresh
// at the next step boundary.
function rebuildPattern() {
    let notes = [];
    let oct = octaveRange();

    for (let o = 0; o < oct; o++) {
        for (let i = 0; i < state.held.length; i++) {
            let n = state.held[i] + 12 * o;
            if (n <= 127) notes[notes.length] = n;
        }
    }

    let mode = playmodeIndex();
    if (mode === 0) {
        // Up - as built
        state.pattern = notes;
    }
    else if (mode === 1) {
        // Down - reverse
        let rev = [];
        for (let i = notes.length - 1; i >= 0; i--) rev[rev.length] = notes[i];
        state.pattern = rev;
    }
    else {
        // Up-Down - ascend then descend, without repeating the two end notes
        let updown = [];
        for (let i = 0; i < notes.length; i++) updown[updown.length] = notes[i];
        for (let i = notes.length - 2; i >= 1; i--) updown[updown.length] = notes[i];
        state.pattern = updown;
    }

    if (state.step >= state.pattern.length) state.step = 0;
};

// Releases whatever note the arp is currently sustaining, if any.
function releaseSounding() {
    if (state.soundingNote >= 0) {
        let off = midi.create();
        midi.setNoteOff(off, state.soundingChannel, state.soundingNote);
        midiOut.send(off);
        state.soundingNote = -1;
    }
};

function onLoad() {
    rack.log("Arpeggiator initialized");
    rack.log("Trigger input: ", config.trigPort);
};

function onUnload() {
    releaseSounding();
};

function onMidiMessage(midiPort, msg) {
    let ch = midi.getChannel(msg);

    if (midi.isNoteOn(msg) && matchesChannel(ch) && midi.getValue(msg) > 0) {
        let note = midi.getNote(msg);
        // Ignore duplicates - a note already held stays in its original
        // press-order slot rather than jumping to the end.
        let known = false;
        for (let i = 0; i < state.held.length; i++) {
            if (state.held[i] === note) known = true;
        }
        if (!known) {
            state.held[state.held.length] = note;
            rebuildPattern();
        }
        return;
    }

    if ((midi.isNoteOff(msg) || (midi.isNoteOn(msg) && midi.getValue(msg) === 0)) && matchesChannel(ch)) {
        let note = midi.getNote(msg);
        let filtered = [];
        for (let i = 0; i < state.held.length; i++) {
            if (state.held[i] !== note) filtered[filtered.length] = state.held[i];
        }
        state.held = filtered;
        rebuildPattern();
        if (state.held.length === 0) {
            // Nothing left held - stop the arp and let go of the last note.
            releaseSounding();
            state.step = 0;
            state.tickCount = 0;
        }
        return;
    }

    // Forward everything else (CC, pitch bend, clock, ...) unchanged.
    midiOut.send(msg);
};

function onTrigger(trigPort) {
    if (trigPort !== config.trigPort) return;

    let division = DIVISIONS[divisionIndex()];
    state.tickCount++;
    if (state.tickCount < division) return;
    state.tickCount = 0;

    releaseSounding();

    if (state.pattern.length === 0) return;

    let note = state.pattern[state.step];
    let ch = config.outChannel === 0 ? 1 : config.outChannel;

    let on = midi.create();
    midi.setNoteOn(on, ch, note, 100);
    midiOut.send(on);

    let lengthTicks = Math.floor(division * param.getValue(3));
    if (lengthTicks < 1) lengthTicks = 1;
    if (lengthTicks > division - 1) lengthTicks = division > 1 ? division - 1 : 1;

    let off = midi.create();
    midi.setNoteOff(off, ch, note);
    midiOut.sendAfterTrigger(off, config.trigPort, lengthTicks);

    state.soundingNote = note;
    state.soundingChannel = ch;

    if (config.showOverlay) {
        rack.overlay("Arp " + PLAYMODES[playmodeIndex()], "note " + note + " (" + (state.step + 1) + "/" + state.pattern.length + ")");
    }

    state.step = state.step + 1;
    if (state.step >= state.pattern.length) state.step = 0;
};
