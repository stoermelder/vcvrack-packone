/**
 * @target stoermelder MIDI-KIT
 * @engine QuickJs@v1
 * @author stoermelder
 * @description Routes the MIDI channels of input 1 to up to four MIDI outputs, configurable per output
 */

// Splits one MIDI input into up to four outputs by MIDI channel. Each output
// gets a list of MIDI channels (1-16): a message arriving on input 1 is sent to
// every output whose list contains its channel. The channel itself is not
// changed.
//
// The default configuration below sends
//
//   channels 1, 2 => output 1
//   channels 3, 5 => output 2
//   channel  4    => output 3
//   output 4      => unused
//
// A channel may appear in several outputs (the message is copied to each of
// them), and a channel that appears in none is dropped, unless
// config.fallbackOutput names an output for it.
//
// Messages without a channel (MIDI clock, start/stop/continue, SysEx) are sent
// to every output if config.systemToAll is set, otherwise they are dropped.
//
// Only as many outputs as config.routes has entries are enabled; the others
// stay disabled and are not offered in the module's context menu.

// Configuration - change these values as needed
const config = {
    // routes[n] lists the MIDI channels (1-16) of input 1 that are sent to
    // output n + 1. At most four entries; an empty list leaves the output silent.
    routes: [
        [1, 2],   // output 1
        [3, 5],   // output 2
        [4],      // output 3
        []        // output 4
    ],

    // Output (1-4) for channels that are in no route, 0 to drop them
    fallbackOutput: 0,

    // Send channel-less messages (clock, start/stop, SysEx) to every output
    systemToAll: true
};

const MAX_OUTPUTS = 4;

// Built once in rack.onLoad() from config
let outputCount = 1;
let dest = [];        // dest[ch] = outputs (1-based) that MIDI channel ch (1-16) is sent to
let fallback = [];
let allOutputs = [];

rack.onLoad = function() {
    outputCount = Math.min(config.routes.length, MAX_OUTPUTS);
    if (config.routes.length > MAX_OUTPUTS) {
        rack.log("Channel router: only " + MAX_OUTPUTS + " outputs exist, ignoring the rest of config.routes");
    }
    midiOut.enablePorts(Math.max(1, outputCount));

    dest = [];
    for (let ch = 0; ch <= 16; ch++) dest.push([]);

    for (let out = 1; out <= outputCount; out++) {
        const channels = config.routes[out - 1];
        for (let i = 0; i < channels.length; i++) {
            const ch = channels[i];
            if (ch < 1 || ch > 16) {
                rack.log("Channel router: ignoring invalid channel " + ch + " for output " + out);
                continue;
            }
            if (dest[ch].indexOf(out) < 0) dest[ch].push(out);
        }
        rack.log("Channel router: output " + out + " <- channels " + (channels.length ? channels.join(", ") : "none"));
    }

    fallback = config.fallbackOutput >= 1 && config.fallbackOutput <= outputCount ? [config.fallbackOutput] : [];

    allOutputs = [];
    for (let out = 1; out <= outputCount; out++) allOutputs.push(out);
};

// Sends msg to each of the given outputs. A handle can only be sent once, so
// every output after the first gets its own copy.
function sendTo(msg, outputs) {
    for (let i = 0; i < outputs.length; i++) {
        const m = i === 0 ? msg : midi.clone(msg);
        midiOut.selectPort(outputs[i]);
        midiOut.send(m);
    }
}

midi.onMessage = function(midiPort, msg) {
    const ch = midi.getChannel(msg);
    if (ch < 0) {
        if (config.systemToAll) sendTo(msg, allOutputs);
        return;
    }
    sendTo(msg, dest[ch].length ? dest[ch] : fallback);
};
