/**
 * @target stoermelder MIDI-KIT
 * @engine Elk
 * @author stoermelder
 * @description Divides incoming MIDI clock, forwarding every Nth tick and emitting a trigger on the trigger output
 */

// MIDI clock divider for MIDI-KIT
//
// MIDI clock runs at a fixed 24 pulses per quarter note (ppqn). Gear that wants
// a slower clock - a step sequencer running at eighth notes, an arpeggiator on
// quarters - needs the stream thinned out. This script forwards only every Nth
// clock tick, so downstream devices still see a valid 0xF8 stream, just slower.
//
// Start (0xFA), Continue (0xFB) and Stop (0xFC) are always forwarded untouched,
// and Start/Continue reset the divider phase so the divided clock always lands
// on the downbeat rather than wherever the previous run left off.
//
// Because 24 ppqn divides evenly, the useful divisors are musical:
//
//     divisor  1 -> 24 ppqn (no division)
//     divisor  2 -> 8th note triplet feel (12 ppqn)
//     divisor  3 ->  8 pulses per quarter
//     divisor  6 -> one pulse per 16th note
//     divisor 12 -> one pulse per 8th note
//     divisor 24 -> one pulse per quarter note
//
// The divided clock is also mirrored to the module's trigger output, so it can
// drive Rack clock inputs directly without a MIDI-to-CV round trip.


// Configuration - change these values as needed
let config = {
    // Forward every Nth clock tick (1 = pass everything through)
    divisor: 6,

    // Also emit a trigger on trigger output 1 for every forwarded tick
    emitTrigger: true,

    // Trigger output port used when emitTrigger is set
    trigPort: 1,

    // Show the running pulse count in the panel overlay
    showOverlay: true,

    // Forward all non-clock messages (notes, CC, ...) unchanged
    passThroughOther: true
};

// Internal state
let state = {
    tickCount: 0,
    pulseCount: 0,
    running: false
};

onLoad = function() {
    rack.log("Clock divider initialized");
    rack.log("Divisor: " + number.toString(config.divisor) + " (24 ppqn / " + number.toString(config.divisor) + ")");
};

let resetPhase = function() {
    state.tickCount = 0;
    state.pulseCount = 0;
};

onMidiMessage = function(midiPort, msg) {
    if (midi.isStart(msg)) {
        resetPhase();
        state.running = true;
        midiOut.send(msg);
        return;
    }

    if (midi.isContinue(msg)) {
        // Continue resumes mid-bar, but restarting the phase here keeps the
        // divided clock aligned to the resume point rather than to a stale count.
        resetPhase();
        state.running = true;
        midiOut.send(msg);
        return;
    }

    if (midi.isStop(msg)) {
        state.running = false;
        midiOut.send(msg);
        return;
    }

    if (midi.isClock(msg)) {
        state.tickCount++;
        if (state.tickCount < config.divisor) {
            // Swallowed - this is the division itself
            return;
        }
        state.tickCount = 0;
        state.pulseCount++;

        midiOut.send(msg);
        if (config.emitTrigger) {
            trig.setTrigger(config.trigPort);
        }
        if (config.showOverlay) {
            rack.overlay("Clock /" + number.toString(config.divisor), "pulse " + number.toString(state.pulseCount));
        }
        return;
    }

    if (config.passThroughOther) {
        midiOut.send(msg);
    }
};
