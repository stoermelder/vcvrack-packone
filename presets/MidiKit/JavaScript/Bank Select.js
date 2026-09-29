/**
 * @target stoermelder MIDI-KIT
 * @engine QuickJs@v1
 * @author stoermelder
 * @description Bank Select + Program Change by knobs (trigger 1) or next preset (trigger 2), e.g. for the Arturia Microfreak
 */

// Selects a program on any device that uses MIDI Bank Select (CC 0 = MSB,
// CC 32 = LSB) followed by a Program Change. The example is the Arturia
// Microfreak: it has 512 presets, but a Program Change only spans 0-127, so
// the presets are organised in banks of 128:
//
//   Bank 0 => presets   0-127
//   Bank 1 => presets 128-255
//   Bank 2 => presets 256-383
//   Bank 3 => presets 384-511
//
// The bank number is sent as the 14-bit Bank Select value: CC 0 carries the
// MSB (the bank number itself), CC 32 the LSB (0). The number of banks is
// config.banks; knob 1 is divided evenly into that many banks.
//
// Knob 1 selects the bank and knob 2 the program within it. Nothing is sent
// while turning the knobs: a trigger on trigger input 1 (channel 1) sends
// Bank Select and the Program Change, in that order.
//
// A trigger on trigger input 2 (channel 1) steps to the next preset instead:
// it continues from the last preset sent (or from the knobs, if none has
// been sent yet), moves on to the next bank after the last program of a bank
// and wraps around to bank 0 after the last bank. Every change is written to
// the module log.

// Configuration - change these values as needed
const config = {
    // MIDI channel (1-16) the device listens on
    channel: rack.getConfig("channel", 1),

    // Number of banks of 128 presets (the Microfreak has 4)
    banks: 4,

    // Show each sent preset in the on-panel overlay
    showOverlay: true
};

const PRESETS_PER_BANK = 128;
const CHANNEL_LABELS = [];
for (let c = 1; c <= 16; c++) CHANNEL_LABELS[CHANNEL_LABELS.length] = String(c);

// Absolute index (bank * 128 + program) of the preset sent last, -1 if none
let current = -1;

function bankIndex() {
    return Math.min(config.banks - 1, Math.floor(param.getValue(1) * config.banks));
}

function programNumber() {
    return Math.min(PRESETS_PER_BANK - 1, Math.floor(param.getValue(2) * PRESETS_PER_BANK));
}

function totalPresets() {
    return config.banks * PRESETS_PER_BANK;
}

function sendPreset(index) {
    const bank = Math.floor(index / PRESETS_PER_BANK);
    const program = index % PRESETS_PER_BANK;
    current = index;

    // Bank Select: CC 0 = MSB (bank), CC 32 = LSB (0), sent as an atomic pair
    const bankMsg = midi.createCc14bit();
    midi.setCc14bit(bankMsg, config.channel, 0, bank);
    midiOut.send(bankMsg);

    const programMsg = midi.create();
    midi.setProgramChange(programMsg, config.channel, program);
    midiOut.send(programMsg);

    rack.log("Preset " + index + " (bank " + bank + ", program " + program + ", MIDI ch " + config.channel + ")");

    if (config.showOverlay) {
        rack.overlay(
            "Bank Select: preset " + index,
            "Bank " + bank + ", program " + program,
            "MIDI ch " + config.channel);
    }
}

// Setup
rack.onLoad = function() {
    param.enable(1);
    param.enable(2);
    trig.enableIn(1, 1);
    trig.enableIn(2, 1);

    // Context menu - right-click the module to change the MIDI channel live.
    rack.registerContextMenu({
        type: "options",
        label: "Channel",
        options: CHANNEL_LABELS,
        onGetValue: function() {
            return config.channel - 1;
        },
        onChange: function(idx) {
            config.channel = idx + 1;
            rack.setConfig("channel", config.channel);
            rack.log("Channel: ", config.channel);
        }
    });
};

// Callbacks
param.getName = function(i) {
    if (i === 1) return "Bank";
    if (i === 2) return "Program in bank";
    return "";
};

param.getValueFormat = function(i) {
    if (i === 1) return number.toString(bankIndex());
    if (i === 2) return number.toString(programNumber());
    return number.toString(param.getValue(i));
};

trig.onTrigger = function(trigPort, channel) {
    if (trigPort === 2) {
        // Next preset: continue from the last one sent, or from the knobs
        const from = current >= 0 ? current : bankIndex() * PRESETS_PER_BANK + programNumber();
        sendPreset((from + 1) % totalPresets());
    }
    else {
        sendPreset(bankIndex() * PRESETS_PER_BANK + programNumber());
    }
};

// Pass all incoming MIDI through unchanged
midi.onMessage = function(midiPort, msg) {
    midiOut.send(msg);
};
