/**
 * @target stoermelder MIDI-KIT
 * @engine QuickJs@v1
 * @author stoermelder
 * @description Flattens MPE (one note per member channel) to a single channel, folding per-note pitch bend into note numbers
 */

// MPE to single channel converter for MIDI-KIT
//
// MPE (MIDI Polyphonic Expression) spreads a chord across "member" channels -
// one voice per channel - so that pitch bend, channel pressure and CC 74
// (timbre/slide) can be applied per note. A non-MPE synth listening on a single
// channel receives the member-channel messages as unrelated monophonic voices,
// with every bend applying to whichever note last landed on that channel.
//
// This script collapses an MPE zone onto one output channel:
// - Note-On/Note-Off on a member channel are rewritten to config.outChannel.
// - Per-note pitch bend is quantised to semitones and folded into the note
//   number, so a bent note plays as the note it was bent to. The residual
//   fraction is dropped - a single-channel receiver has no way to express it.
// - Channel pressure and CC 74 are forwarded on the output channel only for the
//   member channel currently holding the most recently played note, so the
//   receiver sees one coherent expression stream instead of interleaved ones.
//
// Only the Lower Zone layout is handled: channel 1 is the master channel
// (bends there are global and pass through untouched), channels 2-16 are member
// channels. Adjust config.memberLow/memberHigh for a smaller zone.
//
// onUnload releases every member-channel note still sounding on the output
// channel when the script is replaced, the module is reset, or the module is
// removed - without it, a note held at that moment would hang forever.


// Configuration - change these values as needed
let config = {
    // Channel the flattened output is sent on (1-16)
    outChannel: 1,

    // First and last MPE member channel (Lower Zone default: 2-16)
    memberLow: 2,
    memberHigh: 16,

    // Pitch bend range of the sending MPE controller, in semitones.
    // MPE's default is 48; set this to match your controller or the fold
    // will be off by a factor.
    bendRange: 48,

    // Forward channel pressure as channel pressure on the output channel
    forwardPressure: true,

    // Forward CC 74 (timbre / slide) on the output channel
    forwardTimbre: true,

    // Log every fold decision. Noisy - for setup only.
    verbose: false
};

// Internal state.
// noteOfChannel[c] is the note number currently sounding on member channel c,
// or -1 when the channel is free. bendOfChannel[c] is that channel's last
// pitch bend in semitones. Index 0 is unused so channels stay 1-based.
let state = {
    noteOfChannel: [],
    bendOfChannel: [],
    playedOfChannel: [],
    lastChannel: -1,
    counter: 0
};

rack.onLoad = function() {
    for (let c = 0; c <= 16; c++) {
        state.noteOfChannel[c] = -1;
        state.bendOfChannel[c] = 0;
        state.playedOfChannel[c] = 0;
    }
    rack.log("MPE to single channel initialized");
    rack.log("Member channels: ", config.memberLow, "-", config.memberHigh);
    rack.log("Output channel: ", config.outChannel);
    rack.log("Bend range: ", config.bendRange, " semitones");
};

rack.onUnload = function() {
    for (let c = config.memberLow; c <= config.memberHigh; c++) {
        if (state.noteOfChannel[c] >= 0) {
            let off = midi.create();
            midi.setNoteOff(off, config.outChannel, state.noteOfChannel[c]);
            midiOut.send(off);
        }
    }
};

function isMemberChannel(ch) {
    return ch >= config.memberLow && ch <= config.memberHigh;
};

// Converts a raw pitch wheel reading to semitones, using config.bendRange.
// getPitchWheel returns 0..16383 with 8192 as centre.
function bendToSemitones(pitchWheel) {
    return (pitchWheel - 8192) / 8192 * config.bendRange;
};

// Rounds to the nearest integer.
function roundToInt(x) {
    return Math.round(x);
};

// Clamps a note number into the valid MIDI range so a large bend near the
// keyboard edges cannot produce an out-of-range note.
function clampNote(note) {
    return Math.max(0, Math.min(127, note));
};

// True if ch is the member channel holding the most recently played note.
// Expression messages from any other channel are dropped, because a single
// output channel can only carry one pressure/timbre stream at a time.
function isActiveChannel(ch) {
    return ch === state.lastChannel;
};

// Context menu - right-click the module to change these settings live.
// Each menu mirrors a `config` value above; onChange applies the choice.
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

rack.registerContextMenu({
    type: "boolean",
    label: "Forward channel pressure",
    onGetValue: function() {
        return config.forwardPressure;
    },
    onChange: function(checked) {
        config.forwardPressure = checked;
    }
});

rack.registerContextMenu({
    type: "boolean",
    label: "Forward CC 74 (timbre)",
    onGetValue: function() {
        return config.forwardTimbre;
    },
    onChange: function(checked) {
        config.forwardTimbre = checked;
    }
});

midi.onMessage = function(midiPort, msg) {
    let ch = midi.getChannel(msg);

    // Master channel and anything outside the zone passes through untouched
    if (!isMemberChannel(ch)) {
        midiOut.send(msg);
        return;
    }

    if (midi.isNoteOn(msg)) {
        let note = midi.getNote(msg);
        // A Note-On resets the channel's bend: MPE senders emit the bend for a
        // new note after the Note-On, so carrying the previous note's bend over
        // would transpose the attack.
        state.bendOfChannel[ch] = 0;
        state.noteOfChannel[ch] = note;
        state.counter++;
        state.playedOfChannel[ch] = state.counter;
        state.lastChannel = ch;

        let out = midi.create();
        midi.setNoteOn(out, config.outChannel, note, midi.getValue(msg));
        midiOut.send(out);
        if (config.verbose) {
            rack.log("note on ch", ch, " note=", note);
        }
        return;
    }

    if (midi.isNoteOff(msg)) {
        // Release the note that is actually sounding on this channel, not the
        // one in the incoming message: the fold may have shifted it, and the
        // receiver only knows the shifted note.
        let sounding = state.noteOfChannel[ch];
        if (sounding < 0) sounding = midi.getNote(msg);

        state.noteOfChannel[ch] = -1;
        state.bendOfChannel[ch] = 0;
        state.playedOfChannel[ch] = 0;

        // Hand "most recent" over to whichever member channel is still holding
        // the newest remaining note, so expression keeps flowing after a release.
        if (state.lastChannel === ch) {
            let best = -1;
            let bestPlayed = 0;
            for (let c = config.memberLow; c <= config.memberHigh; c++) {
                if (state.noteOfChannel[c] >= 0 && state.playedOfChannel[c] > bestPlayed) {
                    bestPlayed = state.playedOfChannel[c];
                    best = c;
                }
            }
            state.lastChannel = best;
        }

        let out = midi.create();
        midi.setNoteOff(out, config.outChannel, sounding);
        midiOut.send(out);
        return;
    }

    if (midi.isPitchWheel(msg)) {
        let semis = bendToSemitones(midi.getPitchWheel(msg));
        let steps = roundToInt(semis);
        let prevSteps = roundToInt(state.bendOfChannel[ch]);
        state.bendOfChannel[ch] = semis;

        let sounding = state.noteOfChannel[ch];
        // Nothing sounding on this channel, or the bend has not crossed into a
        // new semitone yet: nothing to re-articulate.
        if (sounding < 0 || steps === prevSteps) {
            return;
        }

        // The receiver has no per-note bend, so a semitone crossing is expressed
        // as "release the old note, play the new one".
        let base = sounding - prevSteps;
        let target = clampNote(base + steps);

        let off = midi.create();
        midi.setNoteOff(off, config.outChannel, sounding);
        midiOut.send(off);

        let on = midi.create();
        midi.setNoteOn(on, config.outChannel, target, 100);
        midiOut.send(on);

        state.noteOfChannel[ch] = target;
        if (config.verbose) {
            rack.log("bend ch", ch, ": ", sounding, " -> ", target);
        }
        return;
    }

    if (midi.isChanPressure(msg)) {
        if (!config.forwardPressure || !isActiveChannel(ch)) return;
        let out = midi.create();
        // Channel pressure is a 2-byte message - the pressure value lives in
        // bytes[1], read back via getNote() (getValue() would be bytes[2],
        // which does not exist for a 2-byte message and reads as 0).
        midi.setChanPressure(out, config.outChannel, midi.getNote(msg));
        midiOut.send(out);
        return;
    }

    if (midi.isCc(msg) && midi.getNote(msg) === 74) {
        if (!config.forwardTimbre || !isActiveChannel(ch)) return;
        let out = midi.create();
        midi.setCc(out, config.outChannel, 74, midi.getValue(msg));
        midiOut.send(out);
        return;
    }

    // Any other CC on a member channel is forwarded on the output channel
    if (midi.isCc(msg)) {
        let out = midi.create();
        midi.setCc(out, config.outChannel, midi.getNote(msg), midi.getValue(msg));
        midiOut.send(out);
    }
};
