/**
 * @target stoermelder MIDI-KIT
 * @engine QuickJs
 * @author stoermelder
 * @description Delays every Note-On message on channel 1 for 1500ms
 */

rack.onMidiMessage = function(midiInput, msg) {
    if (midi.isNoteOn(msg) && midi.getChannel(msg) === 1) {
        midiOut.sendAfterMs(msg, 1500);
    }
}
