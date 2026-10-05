/**
 * @target stoermelder MIDI-KIT
 * @engine QuickJs@v1
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
    divisor: rack.getConfig("divisor", 6),

    // Also emit a trigger on trigger output 1 for every forwarded tick
    emitTrigger: rack.getConfig("emitTrigger", true),

    // Forward all non-clock messages (notes, CC, ...) unchanged
    passThroughOther: rack.getConfig("passThroughOther", true)
};

// Internal state
let state = {
    tickCount: 0,
    running: false
};

function resetPhase() {
    state.tickCount = 0;
};

// Setup
rack.onLoad = function() {
    // Context menu - right-click the module to change these settings live.
    // Each menu mirrors a `config` value above; onChange applies the choice.
    rack.registerContextMenu({
        type: "options",
        label: "Divisor",
        options: [["1 (24 ppqn)", 1], ["2 (12 ppqn)", 2], ["3 (8 ppq)", 3], ["6 (16th)", 6], ["12 (8th)", 12], ["24 (quarter)", 24]],
        onGetValue: function() {
            return config.divisor;
        },
        onChange: function(divisor) {
            config.divisor = divisor;
            rack.setConfig("divisor", config.divisor);
            rack.log("Divisor: ", config.divisor, " (24 ppqn / ", config.divisor, ")");
        }
    });

    rack.registerContextMenu({
        type: "boolean",
        label: "Emit trigger output",
        onGetValue: function() {
            return config.emitTrigger;
        },
        onChange: function(checked) {
            config.emitTrigger = checked;
            rack.setConfig("emitTrigger", config.emitTrigger);
            rack.log("Emit trigger: ", checked);
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

    rack.log("Clock divider initialized");
    rack.log("Divisor: ", config.divisor, " (24 ppqn / ", config.divisor, ")");
};

// Callbacks
midi.onMessage = function(midiPort, msg, msgType) {
    if (msgType === midi.START) {
        resetPhase();
        state.running = true;
        midiOut.send(msg);
        return;
    }

    if (msgType === midi.CONTINUE) {
        // Continue resumes mid-bar, but restarting the phase here keeps the
        // divided clock aligned to the resume point rather than to a stale count.
        resetPhase();
        state.running = true;
        midiOut.send(msg);
        return;
    }

    if (msgType === midi.STOP) {
        state.running = false;
        midiOut.send(msg);
        return;
    }

    if (msgType === midi.CLOCK) {
        state.tickCount++;
        if (state.tickCount < config.divisor) {
            // Swallowed - this is the division itself
            return;
        }
        state.tickCount = 0;

        midiOut.send(msg);
        if (config.emitTrigger) {
            trig.setTrigger(1);
        }
        return;
    }

    if (config.passThroughOther) {
        midiOut.send(msg);
    }
};
