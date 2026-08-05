/**
 * @target stoermelder MIDI-KIT
 * @engine QuickJs@v1
 * @author stoermelder
 * @description Delays every Note-On message on channel 1 for two clock ticks on the clock input
 */

rack.onMidiMessage = function(midiInput, msg) {
    if (midi.isNoteOn(msg)) {
        if (midi.getChannel(msg) === 1) {
            midiOut.sendAfterTrigger(msg, 2);
        }
    }
}