/**
 * @target stoermelder MIDI-KIT
 * @engine QuickJs@v1
 * @author stoermelder
 * @description Drops all incoming MIDI messages except for MIDI channel 2
 */

rack.onMidiMessage = function(midiPort, msg) {
    if (midi.getChannel(msg) === 2) {
        midiOut.send(msg);
    }
}
