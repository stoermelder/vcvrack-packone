/**
 * @target stoermelder MIDI-KIT
 * @engine Elk
 * @author stoermelder
 * @description Routes incoming CC messages on MIDI channel 1 to a MIDI channel set by parameter 1 on the panel
 */

param.enable(1);

param.getName = function(port) {
    if (port === 1) return "MIDI Channel";
    return "";
};

param.getValueFormat = function(port) {
    if (port === 1) return number.toString(number.ceil(param.getValue(1) * 16));
    return number.toString(param.getValue(i));
};

onMidiMessage = function(midiPort, msg) {
    if (midi.isCc(msg) && midi.getChannel(msg) === 1) {
        let ch = number.ceil(param.getValue(1) * 16);
        midi.setChannel(msg, ch);
    }
    midiOut.send(msg);
};