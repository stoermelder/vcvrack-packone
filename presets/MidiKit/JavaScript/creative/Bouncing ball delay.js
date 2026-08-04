/**
 * @target stoermelder MIDI-KIT
 * @engine QuickJs
 * @author stoermelder
 * @description Bouncing ball delay: each Note-On spawns a train of echoes that bounce faster and quieter until they settle
 */

// Bouncing ball delay for MIDI-KIT
//
// Every Note-On spawns a "ball": the note is repeated as a train of echoes,
// each one arriving sooner than the last (gravity) and quieter than the last
// (damping), until the velocity drops below the minimum threshold and the ball
// settles - the sound of a ball bearing dropped on a table.
//
// The original Note-On (and its release) passes through untouched; the echo
// train runs on top and is fully pre-scheduled, so releasing the note does not
// stop a train that has already started - just like a real delay keeps ringing
// after the source stops. The whole train is computed and scheduled with
// midiOut.sendAfterMs() when the Note-On arrives, using the param values at
// that moment.
//
// param 1 - Gravity: how much the gap between bounces shrinks each bounce.
//   The interval is multiplied by (1 - Gravity) per bounce, so 0 gives a
//   uniform (regular) delay and higher values make the bounces bunch up.
// param 2 - Bounciness: how much of the velocity survives each bounce (the
//   damping). 1 keeps the echoes at full velocity forever (the train then
//   stops only at the echo cap), lower values fade the ball out quickly.
// param 3 - Min velocity: echoes stop once their velocity falls below this
//   threshold - the "settle" point.
//
// The echo interval starts at config.initialInterval ms and each echo's gate
// is config.gateMs long (capped at half the current interval so echoes never
// bleed into the next bounce). Each ball is capped at config.maxEchoes echoes
// because the engine allows 32 live message handles per callback and each echo
// needs two (a Note-On and a Note-Off).

// Configuration - change these values as needed
let config = {
    // Delay of the first echo after the note, in milliseconds.
    initialInterval: 250,

    // Length of each echo's gate in milliseconds (capped at half the current
    // bounce interval).
    gateMs: 40,

    // Safety cap on echoes per ball - each echo uses two message handles
    // (Note-On + Note-Off) against the engine's 32-handle per-callback limit.
    maxEchoes: 12
};

param.enable(1);
param.enable(2);
param.enable(3);

param.getName = function(i) {
    if (i === 1) return "Gravity";
    if (i === 2) return "Bounciness";
    if (i === 3) return "Min velocity";
    return "";
};

// Interval shrink per bounce: 0..0.4, so intervals stay at 100%..60%.
function gravityParam() {
    let g = param.getValue(1) * 0.4;
    if (g > 0.4) g = 0.4;
    return g;
};

// Velocity retention per bounce: 0..1.
function bouncinessParam() {
    return param.getValue(2);
};

// Echoes settle below this velocity: 1..127.
function minVelocityParam() {
    let m = Math.round(param.getValue(3) * 126) + 1;
    if (m < 1) m = 1;
    if (m > 127) m = 127;
    return m;
};

param.getValueFormat = function(i) {
    if (i === 1) return number.toString(gravityParam());
    if (i === 2) return Math.round(bouncinessParam() * 100) + " %";
    if (i === 3) return number.toString(minVelocityParam());
    return number.toString(param.getValue(i));
};

rack.onLoad = function() {
    rack.log("Bouncing ball delay initialized");
    rack.log("Gravity: ", number.toString(gravityParam()), " | Bounciness: ", Math.round(bouncinessParam() * 100), "% | Min velocity: ", minVelocityParam());
};

// Schedules the echo train for one ball started by the given Note-On. The
// whole train is pre-computed here: each bounce's Note-On and Note-Off is
// placed with sendAfterMs(), the gap shrinking by (1 - gravity) and the
// velocity by bounciness until it settles below the min-velocity threshold.
function spawnBall(ch, note, vel) {
    let interval = config.initialInterval;
    let velocity = vel;
    let t = interval;
    let count = 0;

    while (velocity >= minVelocityParam() && count < config.maxEchoes) {
        // Cap the gate at half the current interval so this echo's Note-Off
        // lands before the next bounce's Note-On.
        let gate = Math.min(config.gateMs, Math.max(interval * 0.5, 1));

        let on = midi.create();
        midi.setNoteOn(on, ch, note, Math.round(velocity));
        midiOut.sendAfterMs(on, t);

        let off = midi.create();
        midi.setNoteOff(off, ch, note);
        midiOut.sendAfterMs(off, t + gate);

        velocity *= bouncinessParam();
        interval *= (1 - gravityParam());
        t += interval;
        count++;
    }
};

rack.onMidiMessage = function(midiPort, msg) {
    if (midi.isNoteOn(msg) && midi.getValue(msg) > 0) {
        let ch = midi.getChannel(msg);
        let note = midi.getNote(msg);
        let vel = midi.getValue(msg);
        // Dry note first (so it leads the echoes), then the echo train.
        midiOut.send(msg);
        spawnBall(ch, note, vel);
        return;
    }
    // Everything else (Note-Off, CC, pitch bend, clock, ...) passes through.
    midiOut.send(msg);
};
