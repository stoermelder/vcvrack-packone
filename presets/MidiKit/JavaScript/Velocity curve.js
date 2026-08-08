/**
 * @target stoermelder MIDI-KIT
 * @engine QuickJs@v1
 * @author stoermelder
 * @description Reshapes Note-On velocity with a knob-controlled curve, plus configurable floor and ceiling
 */

// Velocity curve for MIDI-KIT
//
// Keyboards differ wildly in how hard you have to hit them to reach velocity
// 127, and many synth patches only use a narrow slice of the range. This script
// remaps every Note-On velocity through an adjustable curve before passing it on.
//
// Parameter 1 on the panel sets the curve shape, live:
//
//     knob at 0.0   exponential - soft playing gets much quieter, the full
//                   range only opens up at the top of the key travel
//     knob at 0.5   linear - velocity passes through unchanged in shape
//     knob at 1.0   logarithmic - light touches already reach high velocities,
//                   useful for stiff keybeds
//
// The result is then scaled into config.minVelocity..config.maxVelocity, so a
// patch that sounds wrong below 40 can be given a floor without losing dynamics.
//
// Velocity 0 is left alone: a Note-On with velocity 0 is the standard "running
// status" spelling of a Note-Off, and lifting it off the floor would turn every
// release into a stuck note.


// Configuration - change these values as needed
let config = {
    // Panel parameter driving the curve (1-based)
    curveParam: 1,

    // Output velocity range. Incoming 1..127 is rescaled into this window.
    minVelocity: 1,
    maxVelocity: 127,

    // Curve strength at the knob extremes. This is the exponent handed to
    // number.rescale(), where 0 is linear; beyond about 3 the curve is so steep
    // that most of the key travel maps to a single velocity.
    curveAmount: 2,

    // Only process this channel; 0 = every channel
    channel: 0
};

param.enable(config.curveParam);

param.getName = function(port) {
    if (port === config.curveParam) return "Velocity curve";
    return "";
};

param.getValueFormat = function(port) {
    if (port === config.curveParam) {
        let v = param.getValue(config.curveParam);
        // Report the curve as a signed shape amount rather than a raw 0..1,
        // so the panel reads "-2.0 .. 0.0 .. +2.0" around linear (curveAmount=2).
        // Same sign as shapeVelocity()'s curve, so the readout matches the
        // curve actually applied.
        let shaped = -(v - 0.5) * 2 * config.curveAmount;
        return number.toString(shaped);
    }
    return "";
};

rack.onLoad = function() {
    rack.log("Velocity curve initialized");
    rack.log("Range: ", config.minVelocity, "-", config.maxVelocity);
    rack.log("Knob ", config.curveParam, " sets the curve (centre = linear)");
};

function matchesChannel(ch) {
    return config.channel === 0 || ch === config.channel;
};

// Maps velocity 1..127 through the curve and into the configured range.
// number.rescale takes an optional curve argument, which is exactly the
// exponential shaping wanted here - no manual pow() needed.
function shapeVelocity(vel) {
    let knob = param.getValue(config.curveParam);
    // Negated so a low knob (exponential) squashes soft notes toward the floor
    // and a high knob (logarithmic) lifts them toward the ceiling - matching
    // the header description. The sign was inverted until 2026-08-03, which
    // made knob 0.0 boost instead of squash.
    let curve = -(knob - 0.5) * 2 * config.curveAmount;
    let out = number.rescale(vel, 1, 127, config.minVelocity, config.maxVelocity, curve);

    // Guard the endpoints: rescale works in floats and can land a hair outside
    // the window, which would produce an invalid velocity byte.
    out = Math.floor(out + 0.5);
    out = Math.max(config.minVelocity, Math.min(config.maxVelocity, out));
    return out;
};

// Context menu - right-click the module to change these settings live.
// Each menu mirrors a `config` value above; onChange applies the choice.
let CHANNEL_LABELS = ["All"];
for (let c = 1; c <= 16; c++) CHANNEL_LABELS[CHANNEL_LABELS.length] = String(c);

rack.registerContextMenu({
    type: "options",
    label: "Channel",
    options: CHANNEL_LABELS,
    onGetValue: function() {
        return config.channel;
    },
    onChange: function(idx) {
        config.channel = idx;
        rack.log("Channel: ", CHANNEL_LABELS[idx]);
    }
});

midi.onMessage = function(midiPort, msg) {
    if (midi.isNoteOn(msg) && matchesChannel(midi.getChannel(msg))) {
        let vel = midi.getValue(msg);

        // Velocity 0 is a Note-Off in disguise - pass it through untouched
        if (vel === 0) {
            midiOut.send(msg);
            return;
        }

        let shaped = shapeVelocity(vel);
        midi.setValue(msg, shaped);
    }

    midiOut.send(msg);
};
