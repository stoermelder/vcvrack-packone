/**
 * @target stoermelder MIDI-KIT
 * @engine Elk
 * @author stoermelder
 * @description Rewrites all MIDI messages on channel 1 to channel 2
 */

onMidiMessage = function(midiInput, msg) {
    if (midi.getChannel(msg) === 1) {
        midi.setChannel(msg, 2);
    }
    midi.selectPort(1);
    midiOut.send(msg);
};