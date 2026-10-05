/**
 * @target stoermelder MIDI-KIT
 * @engine QuickJs@v1
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
//
// onUnload releases every still-sounding note when the script is replaced,
// the module is reset, or the module is removed - without it, a chord held
// at that moment would hang forever, since nothing else remembers those
// note numbers are down once this script's state is gone.

// Configuration - change these values as needed
let config = {
    // Semitone offsets added for every played note. Include 0 to keep the
    // original note; omit it to hear only the harmony voices.
    intervals: rack.getConfig("intervals", [0, 4, 7]),

    // Only harmonize this channel; 0 = every channel
    channel: rack.getConfig("channel", 0),

    // Velocity scaling for the added voices, relative to the played note.
    // The 0-offset voice is always sent at full velocity.
    harmonyVelocity: 0.8
};

// Internal state.
// refCount[n] counts how many voices currently want note number n sounding.
// voicesOf[n] is the list of note numbers that the played note n produced,
// so the Note-Off can release exactly the same set.
let state = {
    refCount: [],
    voicesOf: []
};

// Context menu choices
let CHORD_INTERVALS = [
    [0, 4, 7],      // Major triad
    [0, 3, 7],      // Minor triad
    [0, 3, 7, 10],  // Minor seventh
    [0, 7],         // Power chord
    [0, 12],        // Octave doubling
    [0, -12, 12]    // Three octaves
];

function matchesChannel(ch) {
    return config.channel === 0 || ch === config.channel;
};

function chordIndex() {
    for (let i = 0; i < CHORD_INTERVALS.length; i++) {
        if (config.intervals.length === CHORD_INTERVALS[i].length) {
            let same = true;
            for (let j = 0; j < CHORD_INTERVALS[i].length; j++) {
                if (config.intervals[j] !== CHORD_INTERVALS[i][j]) { same = false; break; }
            }
            if (same) return i;
        }
    }
    return -1;
};

// Setup
rack.onLoad = function() {
    for (let n = 0; n < 128; n++) {
        state.refCount[n] = 0;
        state.voicesOf[n] = [];
    }

    // Context menu - right-click the module to change these settings live.
    // Each menu mirrors a `config` value above; onChange applies the choice.
    rack.registerContextMenu({
        type: "options",
        label: "Chord",
        // The value of an option is its position in CHORD_INTERVALS.
        options: [["Major triad", 0], ["Minor triad", 1], ["Minor seventh", 2], ["Power chord", 3], ["Octave doubling", 4], ["Three octaves", 5]],
        onGetValue: function() {
            return chordIndex();
        },
        onChange: function(chord, label) {
            config.intervals = CHORD_INTERVALS[chord];
            rack.setConfig("intervals", config.intervals);
            rack.log("Chord: ", label, " (", config.intervals.length, " voices)");
        }
    });

    rack.registerContextMenu({
        type: "options",
        label: "#midichannel+all",
        onGetValue: function() {
            return config.channel;
        },
        onChange: function(value, label) {
            config.channel = value;
            rack.setConfig("channel", config.channel);
            rack.log("Channel: ", label);
        }
    });

    rack.log("Chord harmonizer initialized");
    rack.log("Voices per note: ", config.intervals.length);
};

// Callbacks
rack.onUnload = function() {
    for (let n = 0; n < 128; n++) {
        if (state.refCount[n] > 0) {
            let off = midi.create();
            midi.setNoteOff(off, 1, n);
            midiOut.send(off);
        }
    }
};

midi.onMessage = function(midiPort, msg) {
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
                    let v = offset === 0 ? vel : Math.floor(vel * config.harmonyVelocity + 0.5);
                    if (v < 1) v = 1;

                    let on = midi.create();
                    midi.setNoteOn(on, ch, target, v);
                    midiOut.send(on);
                }
                state.refCount[target] = state.refCount[target] + 1;
                voices[voices.length] = target;
            }

            state.voicesOf[note] = voices;

            return;
        }
    }

    if (midi.isNoteRelease(msg)) {
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
