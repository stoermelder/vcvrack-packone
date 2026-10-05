/**
 * @target stoermelder MIDI-KIT
 * @engine QuickJs@v1
 * @author stoermelder
 * @description Duplicates all CC messages on channel 1 on channel 2
 */

// Sending a second message needs a new handle: midi.create() makes an empty
// message which is filled with midi.setCc(msg, channel, cc, value). The original
// message is forwarded as well, so every CC on channel 1 leaves twice - once on
// channel 1 and once on channel 2. A handle can only be sent once per callback,
// which is why the copy is a separate message.

midi.onMessage = function(midiPort, msg, msgType) {
    if (midi.getChannel(msg) === 1) {
        // Build the copy from the incoming CC's number and value
        if (msgType === midi.CC) {
            let msg2 = midi.create();
            midi.setCc(msg2, 2, midi.getControl(msg), midi.getValue(msg));
            midiOut.send(msg2);
        }
    }
    midiOut.send(msg);
}