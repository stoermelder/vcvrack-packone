/**
 * @target stoermelder MIDI-KIT
 * @engine QuickJs
 * @author stoermelder
 * @description Passes all incoming MIDI messages to the default MIDI output port.
 */

rack.onMidiMessage = function(midiPort, msg) {
    midiOut.send(msg);
}
