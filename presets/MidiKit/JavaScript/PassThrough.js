/**
 * @target stoermelder MIDI-KIT
 * @engine Elk
 * @author stoermelder
 * @description Passes all incoming MIDI messages to the default MIDI output port.
 */

onMidiMessage = function(midiPort, msg) {
    midiOut.send(msg);
};