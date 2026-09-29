/**
 * @target stoermelder MIDI-KIT
 * @engine QuickJs@v1
 * @author stoermelder
 * @description Routes incoming CC messages on MIDI channel 1 to a MIDI channel set by parameter 1 on the panel
 */

// Same routing as "Dynamic routing Ch1 CC by knob", plus panel feedback:
// param.getName() supplies the tooltip title of a knob and param.getValueFormat()
// the text of its value, so the tooltip shows the channel (1-16) instead of the
// raw 0..1 position. Both are called by the module whenever a tooltip is shown.

rack.onLoad = function() {
    param.enable(1);
};

param.getName = function(port) {
    if (port === 1) return "MIDI Channel";
    return "";
};

// The displayed value uses the same 0..1 -> 1-16 scaling as the routing below
param.getValueFormat = function(port) {
    if (port === 1) return number.toString(Math.ceil(param.getValue(1) * 16));
    return number.toString(param.getValue(port));
};

midi.onMessage = function(midiPort, msg) {
    if (midi.isCc(msg) && midi.getChannel(msg) === 1) {
        let ch = Math.ceil(param.getValue(1) * 16);
        midi.setChannel(msg, ch);
    }
    midiOut.send(msg);
}