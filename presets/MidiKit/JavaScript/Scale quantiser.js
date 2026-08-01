/**
 * @target stoermelder MIDI-KIT
 * @engine Elk
 * @author stoermelder
 * @description Snaps incoming notes to the nearest note of a selectable scale, tracking held notes so releases still match
 */

// Scale quantiser for MIDI-KIT
//
// Forces every incoming note into a scale, so a keyboard (or a random source)
// can only ever produce notes that fit the key. Notes already in the scale pass
// through unchanged; notes outside it are moved to the nearest scale degree.
//
// The catch that makes this more than a one-liner: a quantised Note-On is sent
// as a *different* note number than the one played, so the Note-Off that
// arrives later - carrying the original number - would fail to release it and
// leave a hanging voice. This script remembers the substitution per note in
// state.playedAs and rewrites the Note-Off to match.
//
// The scale is a list of semitone offsets from the root, in the octave
// 0..11. Several common scales are pre-defined below; point config.scale at
// whichever one you want, or write your own list.


// Scale definitions - semitone offsets from the root note
let scales = {
    chromatic:  [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11],
    major:      [0, 2, 4, 5, 7, 9, 11],
    minor:      [0, 2, 3, 5, 7, 8, 10],
    harmonic:   [0, 2, 3, 5, 7, 8, 11],
    dorian:     [0, 2, 3, 5, 7, 9, 10],
    phrygian:   [0, 1, 3, 5, 7, 8, 10],
    lydian:     [0, 2, 4, 6, 7, 9, 11],
    mixolydian: [0, 2, 4, 5, 7, 9, 10],
    pentatonic: [0, 2, 4, 7, 9],
    minorPenta: [0, 3, 5, 7, 10],
    blues:      [0, 3, 5, 6, 7, 10],
    wholeTone:  [0, 2, 4, 6, 8, 10]
};

// Configuration - change these values as needed
let config = {
    // Root note of the scale, as a pitch class: 0 = C, 1 = C#, ... 11 = B
    root: 0,

    // Which scale to snap to - pick any list from `scales` above
    scale: scales.minor,

    // Only quantise this channel; 0 = every channel
    channel: 0,

    // When a note sits exactly between two scale degrees, round up instead of down
    preferUpward: false,

    // Show each substitution in the panel overlay
    showOverlay: true
};

// Internal state.
// playedAs[n] is the note number actually sent for incoming note n, so the
// matching Note-Off can be rewritten the same way.
let state = {
    playedAs: []
};

let noteNames = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];

onLoad = function() {
    for (let n = 0; n < 128; n++) {
        state.playedAs[n] = -1;
    }
    rack.log("Scale quantiser initialized");
    rack.log("Root: " + noteNames[config.root]);
    rack.log("Scale degrees: ", config.scale.length);
};

// Releases every note still substituted in state.playedAs. Without this, a
// held note that has been remapped to a different scale degree would hang
// forever once the script is replaced, the module is reset, or the module is
// removed - the substitution needed to release it correctly lives only in
// this script's state. state.playedAs isn't channel-indexed (only one scale
// is active at a time), so this releases on config.channel if fixed, or
// channel 1 when config.channel is 0 (every channel) - the same best-effort
// choice Chord harmonizer makes for the same reason.
onUnload = function() {
    let ch = config.channel === 0 ? 1 : config.channel;
    for (let n = 0; n < 128; n++) {
        if (state.playedAs[n] >= 0) {
            let off = midi.create();
            midi.setNoteOff(off, ch, state.playedAs[n]);
            midiOut.send(off);
        }
    }
};

let matchesChannel = function(ch) {
    return config.channel === 0 || ch === config.channel;
};

// Snaps a note number to the nearest member of the configured scale.
// Works in pitch-class space, then puts the octave back, so the search only
// ever has to look one octave up and down.
let quantise = function(note) {
    // Distance above the root, folded into 0..11
    let rel = (note - config.root) % 12;
    if (rel < 0) rel = rel + 12;
    let octaveBase = note - rel;

    let best = config.scale[0];
    let bestDist = 127;
    let bestUpNum = 0;

    for (let i = 0; i < config.scale.length; i++) {
        let degree = config.scale[i];

        // Check the degree in this octave and in the one above, so a note just
        // below the root snaps up to the root rather than down a whole octave.
        for (let o = 0; o <= 1; o++) {
            let candidate = degree + o * 12;
            let dist = number.abs(candidate - rel);

            // Strictly closer always wins. On an exact tie - the note sits
            // midway between two degrees - config.preferUpward decides, which
            // is the only case where the choice is arbitrary.
            //
            // The tie test is written over numbers (0/1) rather than
            // booleans, which remains valid and readable; `upNum`/`bestUpNum`
            // carry the same information as boolean up/bestUp.
            let upNum = candidate >= rel ? 1 : 0;
            let wantUp = config.preferUpward ? 1 : 0;

            let better = false;
            if (dist < bestDist) {
                better = true;
            }
            else if (dist === bestDist && upNum !== bestUpNum) {
                better = upNum === wantUp;
            }

            if (better) {
                bestDist = dist;
                best = candidate;
                bestUpNum = upNum;
            }
        }
    }

    let out = octaveBase + best;
    // A snap upward at the very top of the range could exceed 127; drop an
    // octave rather than emit an invalid note byte. Elk has no `while`, so this
    // is the documented `for (; cond;)` idiom.
    for (; out > 127;) {
        out = out - 12;
    }
    for (; out < 0;) {
        out = out + 12;
    }
    return out;
};

onMidiMessage = function(midiPort, msg) {
    if (!matchesChannel(midi.getChannel(msg))) {
        midiOut.send(msg);
        return;
    }

    if (midi.isNoteOn(msg)) {
        let note = midi.getNote(msg);
        let snapped = quantise(note);
        state.playedAs[note] = snapped;

        midi.setNote(msg, snapped);
        midiOut.send(msg);

        if (config.showOverlay && snapped !== note) {
            rack.overlay("Quantise", noteNames[note % 12] + " -> " + noteNames[snapped % 12]);
        }
        return;
    }

    if (midi.isNoteOff(msg)) {
        let note = midi.getNote(msg);
        // Release whatever was actually sent for this key. If the note was
        // never seen (script loaded mid-chord), fall back to the raw number.
        let sent = state.playedAs[note];
        if (sent >= 0) {
            midi.setNote(msg, sent);
            state.playedAs[note] = -1;
        }
        midiOut.send(msg);
        return;
    }

    midiOut.send(msg);
};
