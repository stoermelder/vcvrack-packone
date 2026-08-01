/**
 * @target stoermelder MIDI-KIT
 * @engine Elk
 * @author stoermelder
 * @description NRPN test generator for nrpn_to_cc.js - sweeps a 14-bit value driven by MIDI clock
 */

// Companion script for a second MIDI-KIT instance.
// Emits a spec-compliant NRPN message (CC 98/99 = parameter number, CC 38/6 = 14-bit
// data entry value) built with midi.createNRPN()/midi.setNRPN().
// Feed MIDI clock (0xF8, e.g. from a DAW or another MIDI-KIT clock source) into this
// instance's MIDI IN. On every step it sends the next value of a sweeping 14-bit
// value for the configured NRPN number.
// Route this instance's MIDI OUT into the MIDI IN of the instance running
// nrpn_to_cc.js (e.g. via a virtual MIDI loopback port) to see the conversion happen.


// Configuration - change these values as needed
let config = {
    // MIDI channel used for the NRPN message (1-16, default: 1)
    channel: 1,

    // NRPN parameter number to send (0-16383, default: 0)
    nrpnNumber: 0,

    // Clock ticks per step (24 ppqn, default: 8)
    ticksPerStep: 8,

    // Amount the 14-bit value changes per step
    stepSize: 16,

    // Maximum 14-bit value (0-16383)
    maxValue: 16383
};

// Internal state
let state = {
    tickCount: 0,
    value: 0,
    direction: 1
};

onLoad = function() {
    log("NRPN generator initialized");
    log("Channel: " + number.toString(config.channel));
    log("NRPN number: " + number.toString(config.nrpnNumber));
    log("Ticks per step: " + number.toString(config.ticksPerStep));
};

let sendNrpn = function() {
    let nrpn = midi.createNRPN();
    midi.setNRPN(nrpn, config.channel, config.nrpnNumber, state.value);
    midiOut.send(nrpn);

    log("Sent nrpn #" + number.toString(config.nrpnNumber) + " = " + number.toString(state.value));
};

let advanceValue = function() {
    state.value = state.value + state.direction * config.stepSize;
    if (state.value >= config.maxValue) {
        state.value = config.maxValue;
        state.direction = -1;
    }
    else if (state.value <= 0) {
        state.value = 0;
        state.direction = 1;
    }
};

onMidiMessage = function(midiPort, msg) {
    if (midi.isClock(msg)) {
        state.tickCount++;
        if (state.tickCount >= config.ticksPerStep) {
            state.tickCount = 0;
            advanceValue();
            sendNrpn();
        }
    }
    else if (midi.isStart(msg) || midi.isContinue(msg)) {
        state.tickCount = 0;
    }
};
