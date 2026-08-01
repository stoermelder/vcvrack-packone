/**
 * @target stoermelder MIDI-KIT
 * @engine Elk
 * @author stoermelder
 * @description Routes incoming CC messages on MIDI channel 1 to a MIDI channel set by parameter 1 on the panel
 */

param.enable(1);

onMidiMessage = function(midiPort, msg) {
    if (midi.isCc(msg) && midi.getChannel(msg) === 1) {
        let ch = number.ceil(param.getValue(1) * 16);
        midi.setChannel(msg, ch);
    }
    midiOut.send(msg);
};