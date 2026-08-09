/**
 * @target stoermelder MIDI-KIT
 * @engine QuickJs@v1
 * @author stoermelder
 * @description NRPN to 14-bit CC converter (assembled input)
 */

// NRPN to CC Converter for MidiKit — assembled-input version
//
// Same converter as "NRPN to CC", but using MIDI-KIT's assembled-input API
// instead of a hand-rolled state machine: a spec-compliant NRPN write
// (CC 99/98 number MSB/LSB, then CC 6/38 value MSB/LSB) is reassembled by the
// module into a single parameter change and delivered to midi.onNrpn. See the
// manual "NRPN to CC" example for the worked version that shows what each CC
// means.
//
// The parameter arrives decoded:
//   midi.getControl(msg)  -> the NRPN number (0-16383)
//   midi.getValue(msg)    -> the combined 14-bit value (0-16383)


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
}

// Context menu - right-click the module to change the output channel live.
// Each menu mirrors a `config` value above; onChange applies the choice.
let CHANNEL_LABELS = [];
for (let c = 1; c <= 16; c++) CHANNEL_LABELS[CHANNEL_LABELS.length] = String(c);

rack.registerContextMenu({
    type: "options",
    label: "CC channel",
    options: CHANNEL_LABELS,
    onGetValue: function() {
        return config.ccChannel - 1;
    },
    onChange: function(idx) {
        config.ccChannel = idx + 1;
        rack.log("CC channel: ", config.ccChannel);
    }
});

rack.onLoad = function() {
    rack.log("NRPN to CC converter initialized");
    rack.log("Mapped NRPN numbers: ", config.map.length);
    rack.log("Channel: ", config.ccChannel);
};

// Assemble NRPN parameter changes on MIDI input port 1 into midi.onNrpn.
midi.enableNrpnIn(1);

// No-op: NRPN parameter changes arrive in midi.onNrpn, not here. This only
// suppresses the "no midi.onMessage" load warning. Non-NRPN messages are
// dropped, exactly as in the manual "NRPN to CC" example.
midi.onMessage = function(midiPort, msg) {};

midi.onNrpn = function(midiPort, msg) {
    let nrpnNumber = midi.getControl(msg);
    let nrpnValue = midi.getValue(msg);

    let ccNumber = findCcNumber(nrpnNumber);
    if (ccNumber < 0) {
        // NRPN number not present in config.map, ignore
        return;
    }

    rack.log("nrpn #", nrpnNumber, ": value=", nrpnValue, " -> cc", ccNumber);

    let cc14 = midi.createCc14bit();
    midi.setCc14bit(cc14, config.ccChannel, ccNumber, nrpnValue / 128);
    // The pair (CC ccNumber = MSB, CC ccNumber + 32 = LSB) is sent atomically.
    midiOut.send(cc14);
};
