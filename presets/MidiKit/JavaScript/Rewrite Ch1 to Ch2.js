/**
 * @target stoermelder MIDI-KIT
 * @engine QuickJs@v1
 * @author stoermelder
 * @description Rewrites all MIDI messages on channel 1 to channel 2
 */

midi.onMessage = function(midiInput, msg) {
    if (midi.getChannel(msg) === 1) {
        midi.setChannel(msg, 2);
    }
    midiOut.selectPort(1);
    midiOut.send(msg);
}