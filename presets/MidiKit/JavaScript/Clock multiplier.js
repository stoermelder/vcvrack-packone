/**
 * @target stoermelder MIDI-KIT
 * @engine QuickJs@v1
 * @author stoermelder
 * @description Multiplies a clock on trigger input 1 into a MIDI clock (24 ppqn), placing every pulse on its own sample
 */

// Clock multiplier for MIDI-KIT
//
// Turns a clock on trigger input 1 into a 24 ppqn MIDI clock for hardware. A
// Rack clock usually ticks once per beat (quarter note), where MIDI clock needs
// 24, so the default multiplier is 24. Gear that syncs to MIDI clock shows output
// jitter immediately, which makes this the case sample-accurate timing is for.
//
// midiOut.enableTiming() is what makes it work: with it, every message leaves
// with an engine frame and Rack's output thread transmits it at that frame,
// instead of MIDI-KIT writing it from the audio thread at whatever block
// boundary came next. The price is one audio block of output latency, constant
// for every pulse, which a clock does not mind.
//
// How the pulses are placed:
//   - The pulse for the input edge goes out on the edge's own frame: send()
//     defaults to the frame of the event it runs in.
//   - The pulses in between are spaced evenly over the next input period. That
//     period is not known yet, so the previous one is used. A clock that speeds
//     up or slows down needs one period to catch up, as with any clock
//     multiplier. Each pulse is scheduled with sendAtFrame() from the edge's
//     frame, rack.getEventFrame().
//
// An interval much longer than the previous one (the clock was stopped) is
// treated as a restart: that edge sends its own pulse only, and the next edge
// measures the new period.
//
// The multiplier is the number of MIDI clock pulses sent per input tick. Pick it
// by what one tick of your clock stands for:
//
//     24  quarter notes (one tick per beat)
//     12  8th notes
//      8  8th triplets
//      6  16th notes
//      4  16th triplets
//      3  32nd notes
//      2  32nd triplets
//      1  the input already runs at 24 ppqn

// Configuration - change these values as needed
let config = {
    // MIDI clock pulses per input tick; 24 for a clock ticking once per beat
    ratio: rack.getConfig("ratio", 24)
};

// Internal state
let state = {
    lastEdge: -1,   // frame of the previous input edge
    period: 0       // frames between the last two edges, 0 = not known
};

// An interval this many times the previous one is a restart, not a slow tempo
let RESTART_FACTOR = 4;

// Context menu choices
let RATIOS = [1, 2, 3, 4, 6, 8, 12, 24];
let RATIO_LABELS = ["1x", "2x", "3x", "4x", "6x", "8x", "12x", "24x"];

function ratioIndex() {
    for (let i = 0; i < RATIOS.length; i++) {
        if (RATIOS[i] === config.ratio) return i;
    }
    return 0;
};

function clockPulse() {
    let m = midi.create();
    midi.setRaw(m, "f8");
    return m;
};

// Setup
rack.onLoad = function() {
    midiOut.enableTiming();
    trig.enableIn(1);

    // Context menu - right-click the module to change this setting live.
    rack.registerContextMenu({
        type: "options",
        label: "Multiplier",
        options: RATIO_LABELS,
        onGetValue: function() {
            return ratioIndex();
        },
        onChange: function(idx) {
            config.ratio = RATIOS[idx];
            rack.setConfig("ratio", config.ratio);
            rack.log("Multiplier: ", RATIO_LABELS[idx]);
        }
    });

    rack.log("Clock multiplier initialized");
    rack.log("Multiplier: ", RATIO_LABELS[ratioIndex()]);
};

// Callbacks
trig.onTrigger = function(trigPort, channel) {
    let edge = rack.getEventFrame();
    let previous = state.lastEdge;
    state.lastEdge = edge;

    if (previous >= 0) {
        let interval = edge - previous;
        if (state.period > 0 && interval > RESTART_FACTOR * state.period) {
            state.period = 0;
        }
        else if (interval > 0) {
            state.period = interval;
        }
    }

    // One pulse message serves all of them: sending copies it.
    let pulse = clockPulse();

    // The pulse for the edge itself, on the edge's frame
    midiOut.send(pulse);

    // The pulses up to the next edge, spaced over the previous period
    if (state.period > 0) {
        for (let k = 1; k < config.ratio; k++) {
            midiOut.sendAtFrame(pulse, edge + Math.round(k * state.period / config.ratio));
        }
    }
};
