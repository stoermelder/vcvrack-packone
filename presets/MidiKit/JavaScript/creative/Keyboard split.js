/**
 * @target stoermelder MIDI-KIT
 * @engine QuickJs@v1
 * @author stoermelder
 * @description Keyboard splitter: routes notes below a split point to channel A and the rest to channel B, with CC-switchable presets
 */

// Keyboard split for MIDI-KIT
//
// Splits the keyboard at a note-number split point: every Note-On below the
// split is rewritten to channel A, every Note-On at or above it to channel B -
// the classic way to drive two synths (bass on A, pad on B) from one keyboard.
// Note-Offs are rewritten by the same rule, so each side's notes are released
// on the channel they were played on.
//
// The routing lives in presets. A preset is a split point plus the two output
// channels, and is activated when a Control Change arrives whose number
// matches the preset's `cc` with any value > 0 - the MIDI "program change"
// idiom, where a footswitch or a sequencer CC flips the whole split live.
// Presets are defined in config.presets below (edit the file), and can also
// be switched from the module's right-click menu.
//
// The trigger CCs are consumed so they never reach the synths; every other
// non-note message passes through unchanged.

// Configuration - change these values as needed
let config = {
    // Presets, in order. Each is activated when a CC with number `cc` arrives
    // with value > 0. Notes below `splitPoint` go to `channelA`, notes at or
    // above it to `channelB`. CC numbers should be unique across presets.
    presets: [
        { cc: 70, channelA: 1, channelB: 2, splitPoint: 60 },
        { cc: 71, channelA: 3, channelB: 4, splitPoint: 48 },
        { cc: 72, channelA: 5, channelB: 6, splitPoint: 72 }
    ],

    // Preset active at load (index into config.presets, 0 = first).
    initialPreset: 0,

    // Control CCs are matched on this channel; 0 = any channel
    controlChannel: 0
};

// Internal state.
// active: index into config.presets currently routing the notes.
let state = {
    active: 0
};

// A short label for preset i, used in logs and the context menu.
function presetLabel(i) {
    let p = config.presets[i];
    return (i + 1) + ": CC" + p.cc + " | A" + p.channelA + " B" + p.channelB + " | split " + p.splitPoint;
};

// The output channel for a note: below the split -> channelA, else channelB.
function targetChannel(note) {
    let p = config.presets[state.active];
    return note < p.splitPoint ? p.channelA : p.channelB;
};

rack.onLoad = function() {
    state.active = config.initialPreset;
    if (state.active < 0 || state.active >= config.presets.length) state.active = 0;
    rack.log("Keyboard split initialized");
    rack.log("Presets: ", config.presets.length);
    rack.log("Active preset: ", presetLabel(state.active));
};

// Context menu - right-click the module to switch the active preset manually.
let PRESET_LABELS = [];
for (let i = 0; i < config.presets.length; i++) PRESET_LABELS[PRESET_LABELS.length] = presetLabel(i);

rack.registerContextMenu({
    type: "options",
    label: "Preset",
    options: PRESET_LABELS,
    onGetValue: function() {
        return state.active;
    },
    onChange: function(idx) {
        state.active = idx;
        rack.log("Preset: ", PRESET_LABELS[idx]);
    }
});

rack.onMidiMessage = function(midiPort, msg) {
    let ch = midi.getChannel(msg);

    // Control CC: switch the active preset when a CC whose number matches one
    // of the presets arrives with value > 0, then consume it so it never
    // reaches the synths.
    if (midi.isCc(msg) && (config.controlChannel === 0 || ch === config.controlChannel)) {
        let cc = midi.getNote(msg);
        for (let i = 0; i < config.presets.length; i++) {
            if (config.presets[i].cc === cc) {
                if (midi.getValue(msg) > 0) {
                    state.active = i;
                    rack.log("Preset: ", presetLabel(i));
                }
                return;
            }
        }
    }

    let vel = midi.isNoteOn(msg) ? midi.getValue(msg) : 0;
    let isOn = midi.isNoteOn(msg) && vel > 0;
    // Velocity 0 is the running-status spelling of a Note-Off.
    let isOff = midi.isNoteOff(msg) || (midi.isNoteOn(msg) && vel === 0);

    if (isOn || isOff) {
        let note = midi.getNote(msg);
        midi.setChannel(msg, targetChannel(note));
        midiOut.send(msg);
        return;
    }

    // Everything else (non-trigger CCs, pitch bend, clock, ...) passes through.
    midiOut.send(msg);
};
