/**
 * @target stoermelder MIDI-KIT
 * @engine QuickJs
 * @author stoermelder
 * @description Replaces every note's held length with a fixed number of clock ticks, so all notes end on the grid
 */

// Note length quantiser for MIDI-KIT
//
// Notes played by hand end whenever the finger lifts, which leaves ragged note
// lengths - a problem for gate-driven gear, for arpeggiators that re-trigger on
// release, and for anything where a slightly-too-long note overlaps the next.
//
// This script discards the incoming Note-Off entirely and schedules its own,
// exactly config.lengthTicks clock ticks after the Note-On, using
// midiOut.sendAfterTrigger(). Every note then lasts the same musical duration
// regardless of how it was played.
//
// Requires a clock on the module's trigger input (config.trigPort): the length
// is counted in ticks of that input, not in milliseconds, so it follows tempo.
// Feed the same clock that drives the rest of the patch. With a 24 ppqn MIDI
// clock routed to the trigger input:
//
//     lengthTicks  6 -> 16th note
//     lengthTicks 12 -> 8th note
//     lengthTicks 24 -> quarter note
//
// Retriggering the same note while it is still sounding is handled by sending
// the pending Note-Off immediately, so the note is re-articulated rather than
// being cut short later by a stale scheduled release.


// Configuration - change these values as needed
let config = {
    // Fixed note length, in ticks of the trigger input's clock
    lengthTicks: 12,

    // Trigger input the length is counted on (1-based)
    trigPort: 1,

    // Only quantise this channel; set to 0 to quantise every channel
    channel: 0,

    // Forward non-note messages (CC, pitch bend, clock, ...) unchanged
    passThroughOther: true,

    // Log each quantised note
    verbose: false
};

// Internal state.
// sounding[n] is true while note number n has a scheduled Note-Off pending.
let state = {
    sounding: []
};

rack.onLoad = function() {
    for (let n = 0; n < 128; n++) {
        state.sounding[n] = false;
    }
    rack.log("Note length quantiser initialized");
    rack.log("Length: ", config.lengthTicks, " ticks on trigger input ", config.trigPort);
    if (config.channel === 0) {
        rack.log("Channel: all");
    }
    else {
        rack.log("Channel: ", config.channel);
    }
};

// Releases every note with a still-pending scheduled Note-Off. Without this,
// a note whose release hasn't fired yet at the moment the script is replaced,
// the module is reset, or the module is removed would hang forever - the
// scheduled Note-Off belongs to the old script state and is discarded with it.
// state.sounding isn't channel-indexed (only one note-length policy is active
// at a time), so this releases on config.channel if fixed, or channel 1 when
// config.channel is 0 (every channel) - the same best-effort choice
// Chord harmonizer makes for the same reason.
rack.onUnload = function() {
    let ch = config.channel === 0 ? 1 : config.channel;
    for (let n = 0; n < 128; n++) {
        if (state.sounding[n]) {
            let off = midi.create();
            midi.setNoteOff(off, ch, n);
            midiOut.send(off);
        }
    }
};

function matchesChannel(ch) {
    return config.channel === 0 || ch === config.channel;
};

// Builds and schedules the Note-Off that ends a quantised note.
function scheduleNoteOff(ch, note) {
    let off = midi.create();
    midi.setNoteOff(off, ch, note);
    midiOut.sendAfterTrigger(off, config.trigPort, config.lengthTicks);
};

rack.onMidiMessage = function(midiPort, msg) {
    let ch = midi.getChannel(msg);

    if (midi.isNoteOn(msg) && matchesChannel(ch)) {
        let note = midi.getNote(msg);

        // Same note still sounding: release it now so the re-articulation is
        // clean and its pending scheduled Note-Off cannot clip the new note.
        if (state.sounding[note]) {
            let cut = midi.create();
            midi.setNoteOff(cut, ch, note);
            midiOut.send(cut);
        }

        midiOut.send(msg);
        scheduleNoteOff(ch, note);
        state.sounding[note] = true;

        if (config.verbose) {
            rack.log("note ", note, " -> ", config.lengthTicks, " ticks");
        }
        return;
    }

    if (midi.isNoteOff(msg) && matchesChannel(ch)) {
        // Dropped on purpose: the scheduled Note-Off is what ends the note.
        // Note that state.sounding is cleared here rather than when the
        // scheduled release fires - the script has no callback for that - so a
        // note held longer than lengthTicks is already marked free by the time
        // the player lifts the key, which is the same moment it stopped sounding.
        state.sounding[midi.getNote(msg)] = false;
        return;
    }

    if (config.passThroughOther) {
        midiOut.send(msg);
    }
};
