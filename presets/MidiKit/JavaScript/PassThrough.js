/**
 * @target stoermelder MIDI-KIT
 * @engine QuickJs@v1
 * @author stoermelder
 * @description Passes all incoming MIDI messages to the default MIDI output port.
 */

midi.onMessage = function(midiPort, msg) {
    midiOut.send(msg);
}
