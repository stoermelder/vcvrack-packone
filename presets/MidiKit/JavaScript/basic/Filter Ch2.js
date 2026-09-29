/**
 * @target stoermelder MIDI-KIT
 * @engine QuickJs@v1
 * @author stoermelder
 * @description Drops all incoming MIDI messages except for MIDI channel 2
 */

// Messages are only forwarded when the script calls midiOut.send(), so filtering
// is just an `if` around the send: whatever is not sent is dropped.
// midi.getChannel() returns 1-16, or -1 for messages without a channel (clock,
// start/stop, SysEx), so those are dropped here as well.

midi.onMessage = function(midiPort, msg) {
    if (midi.getChannel(msg) === 2) {
        midiOut.send(msg);
    }
}
