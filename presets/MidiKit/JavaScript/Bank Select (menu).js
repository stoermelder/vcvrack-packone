/**
 * @target stoermelder MIDI-KIT
 * @engine QuickJs@v1
 * @author stoermelder
 * @description Bank Select + Program Change from the context menu only, e.g. for the Arturia Microfreak
 */

// Selects a program on any device that uses MIDI Bank Select (CC 0 = MSB,
// CC 32 = LSB) followed by a Program Change, driven entirely by the
// module's right-click menu: no knobs, no trigger inputs and no MIDI input
// is used. The example is the Arturia Microfreak: it has 512 presets, but a
// Program Change only spans 0-127, so the presets are organised in banks
// of 128:
//
//   Bank 0 => presets   0-127
//   Bank 1 => presets 128-255
//   Bank 2 => presets 256-383
//   Bank 3 => presets 384-511
//
// The bank number is sent as the 14-bit Bank Select value: CC 0 carries the
// MSB (the bank number itself), CC 32 the LSB (0). The number of banks is
// config.banks.
//
// The menu has four items: Channel, Bank, Program group (the program in
// steps of 16) and Program (the 16 programs of the chosen group). Picking
// an entry of Bank or Program sends Bank Select and the Program Change
// immediately, in that order. Program group only changes which programs the
// Program item lists, it sends nothing. The selection is saved with the
// patch, but nothing is sent when the patch loads.

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
const GROUP_SIZE = 16;
const GROUPS = PRESETS_PER_BANK / GROUP_SIZE;

const CHANNEL_LABELS = [];
for (let c = 1; c <= 16; c++) CHANNEL_LABELS[CHANNEL_LABELS.length] = String(c);

const BANK_LABELS = [];
for (let b = 0; b < config.banks; b++) BANK_LABELS[BANK_LABELS.length] = String(b);

const GROUP_LABELS = [];
for (let g = 0; g < GROUPS; g++) {
    GROUP_LABELS[GROUP_LABELS.length] = (g * GROUP_SIZE) + "-" + (g * GROUP_SIZE + GROUP_SIZE - 1);
}

// Current selection, restored from the patch
let bank = Math.min(config.banks - 1, rack.getConfig("bank", 0));
let program = Math.min(PRESETS_PER_BANK - 1, rack.getConfig("program", 0));

function sendPreset() {
    // Bank Select: CC 0 = MSB (bank), CC 32 = LSB (0), sent as an atomic pair
    const bankMsg = midi.createCc14bit();
    midi.setCc14bit(bankMsg, config.channel, 0, bank * 128);
    midiOut.send(bankMsg);

    const programMsg = midi.create();
    midi.setProgramChange(programMsg, config.channel, program);
    midiOut.send(programMsg);

    rack.log("Preset " + (bank * PRESETS_PER_BANK + program) + " (bank " + bank + ", program " + program + ", MIDI ch " + config.channel + ")");

    if (config.showOverlay) {
        rack.overlay(
            "Bank Select: preset " + (bank * PRESETS_PER_BANK + program),
            "Bank " + bank + ", program " + program,
            "MIDI ch " + config.channel);
    }
}

// The Program item lists the 16 programs of the current group with their
// absolute numbers. Registering it again under the same label replaces it in
// place, which is how the labels follow the group.
function registerProgramMenu() {
    const first = Math.floor(program / GROUP_SIZE) * GROUP_SIZE;
    const labels = [];
    for (let i = 0; i < GROUP_SIZE; i++) labels[labels.length] = String(first + i);
    rack.registerContextMenu({
        type: "options",
        label: "Program",
        options: labels,
        onGetValue: function() {
            return program % GROUP_SIZE;
        },
        onChange: function(idx) {
            program = Math.floor(program / GROUP_SIZE) * GROUP_SIZE + idx;
            rack.setConfig("program", program);
            sendPreset();
        }
    });
}

// Setup
rack.onLoad = function() {
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
        }
    });

    rack.registerContextMenu({
        type: "options",
        label: "Bank",
        options: BANK_LABELS,
        onGetValue: function() {
            return bank;
        },
        onChange: function(idx) {
            bank = idx;
            rack.setConfig("bank", bank);
            sendPreset();
        }
    });

    rack.registerContextMenu({
        type: "options",
        label: "Program group",
        options: GROUP_LABELS,
        onGetValue: function() {
            return Math.floor(program / GROUP_SIZE);
        },
        onChange: function(idx) {
            // Keep the position within the group
            program = idx * GROUP_SIZE + program % GROUP_SIZE;
            rack.setConfig("program", program);
            registerProgramMenu();
        }
    });

    registerProgramMenu();
};
