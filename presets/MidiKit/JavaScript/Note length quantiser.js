/**
 * @target stoermelder MIDI-KIT
 * @engine QuickJs@v1
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
// Requires a clock on the module's trigger input: the length is counted in
// ticks of that input, not in milliseconds, so it follows tempo.
// Feed the same clock that drives the rest of the patch. With a 24 ppqn MIDI
// clock routed to the trigger input:
//
//     lengthTicks  6 -> 16th note
//     lengthTicks 12 -> 8th note
//     lengthTicks 24 -> quarter note
//
// Retriggering the same note while it is still sounding is handled by
// cancelling its pending scheduled Note-Off with midiOut.cancel() and sending a
// Note-Off immediately instead, so the note is re-articulated and the new note
// gets its full length: the old release can no longer fire later and cut it short.


// Configuration - change these values as needed
let config = {
    // Fixed note length, in ticks of the trigger input's clock
    lengthTicks: rack.getConfig("lengthTicks", 12),

    // Only quantise this channel; set to 0 to quantise every channel
    channel: rack.getConfig("channel", 0),

    // Forward non-note messages (CC, pitch bend, clock, ...) unchanged
    passThroughOther: rack.getConfig("passThroughOther", true),

    // Log each quantised note
    verbose: rack.getConfig("verbose", false)
};

// Internal state.
// sounding[n] is true from the first Note-On of note number n on: the note may still
// be sounding, with a scheduled Note-Off pending.
let state = {
    sounding: []
};

function matchesChannel(ch) {
    return config.channel === 0 || ch === config.channel;
};

// Builds and schedules the Note-Off that ends a quantised note.
function scheduleNoteOff(ch, note) {
    let off = midi.create();
    midi.setNoteOff(off, ch, note);
    midiOut.sendAfterTrigger(off, config.lengthTicks);
};

// Setup
rack.onLoad = function() {
    // The scheduled Note-Offs are counted in ticks of the trigger input's clock,
    // so that clock must be enabled — without trig.enableIn() the module does not
    // process the trigger input at all and the scheduled sends never fire.
    trig.enableIn(1, 1);

    for (let n = 0; n < 128; n++) {
        state.sounding[n] = false;
    }

    // Context menu - right-click the module to change these settings live.
    // Each menu mirrors a `config` value above; onChange applies the choice.
    rack.registerContextMenu({
        type: "options",
        label: "Note length",
        options: [["6 (16th)", 6], ["12 (8th)", 12], ["24 (quarter)", 24], ["48 (half)", 48]],
        onGetValue: function() {
            return config.lengthTicks;
        },
        onChange: function(ticks) {
            config.lengthTicks = ticks;
            rack.setConfig("lengthTicks", config.lengthTicks);
            rack.log("Length: ", config.lengthTicks, " ticks");
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

    rack.registerContextMenu({
        type: "boolean",
        label: "Pass through other messages",
        onGetValue: function() {
            return config.passThroughOther;
        },
        onChange: function(checked) {
            config.passThroughOther = checked;
            rack.setConfig("passThroughOther", config.passThroughOther);
        }
    });

    rack.registerContextMenu({
        type: "boolean",
        label: "Log quantised notes",
        onGetValue: function() {
            return config.verbose;
        },
        onChange: function(checked) {
            config.verbose = checked;
            rack.setConfig("verbose", config.verbose);
        }
    });

    rack.log("Note length quantiser initialized");
    rack.log("Length: ", config.lengthTicks, " ticks");
    if (config.channel === 0) {
        rack.log("Channel: all");
    }
    else {
        rack.log("Channel: ", config.channel);
    }
};

// Callbacks

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

midi.onMessage = function(midiPort, msg) {
    let ch = midi.getChannel(msg);

    // A Note-On with velocity 0 is how most keyboards send a release.
    if (midi.isNoteOn(msg) && !midi.isNoteRelease(msg) && matchesChannel(ch)) {
        let note = midi.getNote(msg);

        // Cancel the note's pending scheduled Note-Off on every Note-On, so that
        // it cannot clip the new note when its tick comes. A cancel that matches
        // nothing costs nothing.
        let cut = midi.create();
        midi.setNoteOff(cut, ch, note);
        midiOut.cancel(cut);

        // Same note possibly still sounding: release it now so the
        // re-articulation is clean.
        if (state.sounding[note]) {
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

    if (midi.isNoteRelease(msg) && matchesChannel(ch)) {
        // Dropped on purpose: the scheduled Note-Off is what ends the note.
        // state.sounding is not cleared here: the note keeps sounding until
        // that scheduled release fires, however early the key is lifted, and
        // the script has no callback for that. The flag therefore means "may
        // still be sounding" - a retrigger or the unload then sends a release
        // that is redundant for a note that has already ended, which is harmless.
        return;
    }

    if (config.passThroughOther) {
        midiOut.send(msg);
    }
};
