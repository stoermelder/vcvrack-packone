/**
 * @target stoermelder MIDI-KIT
 * @engine QuickJs
 * @author stoermelder
 * @description Gravity well: notes are bent toward a center pitch, more when they are far from it or played softly
 */

// Gravity well for MIDI-KIT
//
// Every Note-On "falls" toward a configurable center pitch: the note is
// retuned by a fraction of its distance from the center, so the farther it is
// from the center the more it is pulled in. How far it falls is set by the
// velocity - soft notes fall deep into the well (a lot of bending, tension),
// loud notes resist the pull and stay near their original pitch (release).
// The tension/release emerges automatically from the note's distance and the
// incoming velocity, no programming needed.
//
// param 1 - Center: the gravitational center pitch, 0-127. Notes are bent
//   toward this note; a note on the center passes through untouched.
// param 2 - Strength: how strongly the well pulls, 0..1. The bend is
//   distance * Strength * (1 - velocity / 127), so 0 disables the effect and
//   the bend grows with distance and shrinks with velocity.
//
// Because the retuned pitch depends on the velocity, the Note-Off that arrives
// later carries the *played* note, not the one sent - the script remembers the
// sent note per channel/note so every release lands on the right pitch (the
// same trap as the Scale quantiser and Micro scale presets). Everything that
// is not a Note-On/Note-Off passes through unchanged.

param.enable(1);
param.enable(2);

param.getName = function(i) {
    if (i === 1) return "Center";
    if (i === 2) return "Strength";
    return "";
};

// The gravitational center pitch, 0-127.
function centerParam() {
    let c = Math.round(param.getValue(1) * 127);
    if (c < 0) c = 0;
    if (c > 127) c = 127;
    return c;
};

// The well's pull, 0..1.
function strengthParam() {
    let s = param.getValue(2);
    if (s < 0) s = 0;
    if (s > 1) s = 1;
    return s;
};

// Note names (sharps only) for the Center readout.
let NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];

function noteName(n) {
    return NOTE_NAMES[n % 12] + (Math.floor(n / 12) - 1);
};

param.getValueFormat = function(i) {
    if (i === 1) return centerParam() + " (" + noteName(centerParam()) + ")";
    if (i === 2) return number.toString(strengthParam());
    return number.toString(param.getValue(i));
};

// Internal state: the note actually sent for each incoming (channel, note), so
// the Note-Off can release the bent pitch. -1 = nothing mapped.
let state = {
    sentNote: []
};

rack.onLoad = function() {
    for (let c = 1; c <= 16; c++) {
        state.sentNote[c] = [];
        for (let n = 0; n < 128; n++) state.sentNote[c][n] = -1;
    }
    rack.log("Gravity well initialized");
    rack.log("Center: ", centerParam(), " | Strength: ", number.toString(strengthParam()));
};

rack.onUnload = function() {
    for (let c = 1; c <= 16; c++) {
        for (let n = 0; n < 128; n++) {
            if (state.sentNote[c][n] >= 0) {
                let off = midi.create();
                midi.setNoteOff(off, c, state.sentNote[c][n]);
                midiOut.send(off);
            }
        }
    }
};

// The bent pitch for a note: pulled toward the center by a fraction of the
// distance that shrinks as the velocity rises. Uses floor(x + 0.5) so both
// engines round identically even for negative (below-center) bends.
function bendNote(note, vel) {
    let distance = note - centerParam();
    let fraction = strengthParam() * (1 - vel / 127);
    let outNote = note - Math.floor(distance * fraction + 0.5);
    if (outNote < 0) outNote = 0;
    if (outNote > 127) outNote = 127;
    return outNote;
};

rack.onMidiMessage = function(midiPort, msg) {
    let ch = midi.getChannel(msg);

    let vel = midi.isNoteOn(msg) ? midi.getValue(msg) : 0;
    let isOn = midi.isNoteOn(msg) && vel > 0;
    // Velocity 0 is the running-status spelling of a Note-Off.
    let isOff = midi.isNoteOff(msg) || (midi.isNoteOn(msg) && vel === 0);

    if (isOn) {
        let note = midi.getNote(msg);
        let outNote = bendNote(note, vel);
        state.sentNote[ch][note] = outNote;

        let on = midi.create();
        midi.setNoteOn(on, ch, outNote, vel);
        midiOut.send(on);
        return;
    }

    if (isOff) {
        let note = midi.getNote(msg);
        let outNote = state.sentNote[ch][note];
        if (outNote < 0) outNote = note;
        state.sentNote[ch][note] = -1;

        let off = midi.create();
        midi.setNoteOff(off, ch, outNote);
        midiOut.send(off);
        return;
    }

    // Everything else (CC, pitch bend, clock, ...) passes through.
    midiOut.send(msg);
};
