/**
 * @target stoermelder MIDI-KIT
 * @engine Elk
 * @author stoermelder
 * @description Turns every incoming note into a chord by adding transposed copies, with matching Note-Offs
 */

// Chord harmonizer for MIDI-KIT
//
// Adds transposed copies of every played note, turning a single-finger melody
// into chords. config.intervals lists the semitone offsets to add - 0 keeps the
// played note itself, so removing it produces only the harmony voices.
//
// Presets to try:
//     [0, 4, 7]        major triad
//     [0, 3, 7]        minor triad
//     [0, 3, 7, 10]    minor seventh
//     [0, 7]           power chord
//     [0, 12]          octave doubling
//     [0, -12, 12]     three octaves
//
// Note-Off handling is the part worth reading. Every voice sent for a note must
// be released, and only once: if two different intervals collide on the same
// note number (or a transposed voice lands on a note the player is also holding
// directly) a naive "send a Note-Off per interval" would release the note while
// another voice still wants it. This script therefore reference-counts sounding
// note numbers in state.refCount and only emits a Note-Off when the last user
// of that note lets go.


// Configuration - change these values as needed
let config = {
    // Semitone offsets added for every played note. Include 0 to keep the
    // original note; omit it to hear only the harmony voices.
    intervals: [0, 4, 7],

    // Only harmonize this channel; 0 = every channel
    channel: 0,

    // Velocity scaling for the added voices, relative to the played note.
    // The 0-offset voice is always sent at full velocity.
    harmonyVelocity: 0.8,

    // Show each harmonized note in the panel overlay.
    // Note: voices transposed outside 0..127 are dropped rather than clamped -
    // clamping would pile several voices onto the same edge note.
    showOverlay: true
};

// Internal state.
// refCount[n] counts how many voices currently want note number n sounding.
// voicesOf[n] is the list of note numbers that the played note n produced,
// so the Note-Off can release exactly the same set.
let state = {
    refCount: [],
    voicesOf: []
};

let init = function() {
    for (let n = 0; n < 128; n++) {
        state.refCount[n] = 0;
        state.voicesOf[n] = [];
    }
    log("Chord harmonizer initialized");
    log("Voices per note: " + number.toString(config.intervals.length));
};

let matchesChannel = function(ch) {
    return config.channel === 0 || ch === config.channel;
};

let processMidi = function(midiPort, msg) {
    let ch = midi.getChannel(msg);

    if (!matchesChannel(ch)) {
        midiOut.send(msg);
        return;
    }

    if (midi.isNoteOn(msg)) {
        let note = midi.getNote(msg);
        let vel = midi.getValue(msg);

        // Velocity 0 is a Note-Off in disguise; let the Note-Off branch below
        // handle it by falling through rather than starting new voices.
        if (vel > 0) {
            let voices = [];

            for (let i = 0; i < config.intervals.length; i++) {
                let offset = config.intervals[i];
                let target = note + offset;
                if (target < 0 || target > 127) continue;

                // Only actually sound the note if nothing else is holding it.
                // Otherwise just take a reference - the note is already down.
                if (state.refCount[target] === 0) {
                    let v = offset === 0 ? vel : number.floor(vel * config.harmonyVelocity + 0.5);
                    if (v < 1) v = 1;

                    let on = midi.create();
                    midi.setNoteOn(on, ch, target, v);
                    midiOut.send(on);
                }
                state.refCount[target] = state.refCount[target] + 1;
                voices[voices.length] = target;
            }

            state.voicesOf[note] = voices;

            if (config.showOverlay) {
                overlay("Harmonize", number.toString(note) + " + " + number.toString(voices.length) + " voices");
            }
            return;
        }
    }

    if (midi.isNoteOff(msg) || (midi.isNoteOn(msg) && midi.getValue(msg) === 0)) {
        let note = midi.getNote(msg);
        let voices = state.voicesOf[note];

        // Never saw the Note-On (script loaded mid-chord): pass the release
        // through untouched so the note cannot hang.
        if (voices.length === 0) {
            midiOut.send(msg);
            return;
        }

        for (let i = 0; i < voices.length; i++) {
            let target = voices[i];
            state.refCount[target] = state.refCount[target] - 1;

            // Last voice holding this note number - now it may actually stop
            if (state.refCount[target] <= 0) {
                state.refCount[target] = 0;
                let off = midi.create();
                midi.setNoteOff(off, ch, target);
                midiOut.send(off);
            }
        }

        state.voicesOf[note] = [];
        return;
    }

    midiOut.send(msg);
};

// Initialize when script loads
init();
