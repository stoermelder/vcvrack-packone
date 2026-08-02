/**
 * @target stoermelder MIDI-KIT
 * @engine QuickJs
 * @author stoermelder
 * @description NRPN to 14-bit CC converter
 */

// NRPN to CC Converter for MidiKit
// Converts NRPN (Non-Registered Parameter Number) messages to 14-bit CC messages
//
// A spec-compliant NRPN message is sent as 4 CC messages on the same channel:
// - CC 98 (0x62): NRPN parameter number, LSB
// - CC 99 (0x63): NRPN parameter number, MSB
// - CC 38 (0x26): Data Entry value, LSB
// - CC 6  (0x06): Data Entry value, MSB
//
// This script accumulates all four bytes, and once a full NRPN message has been
// received it looks up the NRPN parameter number in config.map and forwards the
// 14-bit value as a 14-bit CC pair (CC ccNumber = MSB, CC ccNumber + 32 = LSB).
// NRPN numbers not listed in config.map are ignored.


// Configuration - change these values as needed
let config = {
    // Mapping of NRPN parameter numbers to output CC numbers.
    // Add or remove entries as needed; each nrpnNumber may appear only once.
    map: [
        { nrpnNumber: 0, ccNumber: 0 },
        { nrpnNumber: 1, ccNumber: 1 },
        { nrpnNumber: 2, ccNumber: 2 }
    ],

    // Optional: CC channel (1-16, default: 1)
    ccChannel: 1
};

// Internal state
let state = {
    numberLsb: 0,
    numberMsb: 0,
    valueLsb: 0,
    valueMsb: 0,
    hasNumberLsb: false,
    hasNumberMsb: false,
    hasValueLsb: false,
    hasValueMsb: false
};

function onLoad() {
    rack.log("NRPN to CC converter initialized");
    rack.log("Mapped NRPN numbers: ", config.map.length);
    rack.log("Channel: ", config.ccChannel);
};

// Returns the CC number mapped to nrpnNumber, or -1 if not mapped
function findCcNumber(nrpnNumber) {
    let ccNumber = -1;
    for (let i = 0; i < config.map.length; i++) {
        if (config.map[i].nrpnNumber === nrpnNumber) {
            ccNumber = config.map[i].ccNumber;
            break;
        }
    }
    return ccNumber;
};

function resetState() {
    state.hasNumberLsb = false;
    state.hasNumberMsb = false;
    state.hasValueLsb = false;
    state.hasValueMsb = false;
};

// Called when a MIDI message is received
function onMidiMessage(midiPort, msg) {
    if (!midi.isCc(msg)) {
        return;
    }

    let cc = midi.getNote(msg); // CC number
    let value = midi.getValue(msg); // CC value (0-127)

    if (cc === 98) { // NRPN number LSB
        state.numberLsb = value;
        state.hasNumberLsb = true;
    }
    else if (cc === 99) { // NRPN number MSB
        state.numberMsb = value;
        state.hasNumberMsb = true;
    }
    else if (cc === 38) { // Data entry LSB
        state.valueLsb = value;
        state.hasValueLsb = true;
    }
    else if (cc === 6) { // Data entry MSB
        state.valueMsb = value;
        state.hasValueMsb = true;
    }
    else {
        return;
    }

    // Once all four bytes of the NRPN message have arrived, convert and forward
    if (state.hasNumberLsb && state.hasNumberMsb && state.hasValueLsb && state.hasValueMsb) {
        let nrpnNumber = (state.numberMsb << 7) | state.numberLsb;
        let nrpnValue = (state.valueMsb << 7) | state.valueLsb;
        resetState();

        let ccNumber = findCcNumber(nrpnNumber);
        if (ccNumber < 0) {
            // NRPN number not present in config.map, ignore
            return;
        }

        rack.log("nrpn #", nrpnNumber, ": value=", nrpnValue, " -> cc", ccNumber);

        let ccMsb = midi.create();
        let ccLsb = midi.create();
        midi.setCc14bit(ccMsb, ccLsb, config.ccChannel, ccNumber, nrpnValue / 128);
        midiOut.send(ccMsb);
        midiOut.send(ccLsb);
    }
};
