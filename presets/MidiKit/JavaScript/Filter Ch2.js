/**
 * @target stoermelder MIDI-KIT
 * @engine Elk
 * @author stoermelder
 * @description Drops all incoming MIDI messages except for MIDI channel 2
 */

onMidiMessage = function(midiPort, msg) {
    if (midi.getChannel(msg) === 2) {
        midiOut.send(msg);
    }
};