/**
 * @target stoermelder MIDI-KIT
 * @engine QuickJs@v1
 * @author stoermelder
 * @description Rewrites all MIDI messages on channel 1 to channel 2
 */

// Messages can be modified before they are sent: midi.setChannel() changes the
// channel of the incoming message in place, and everything else passes through
// unchanged. midiOut.selectPort(1) picks the output port (1-4) that all following
// midiOut calls use; port 1 is the default, so here it only shows how to select one.

midi.onMessage = function(midiInput, msg) {
    if (midi.getChannel(msg) === 1) {
        midi.setChannel(msg, 2);
    }
    midiOut.selectPort(1);
    midiOut.send(msg);
}