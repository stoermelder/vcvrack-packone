/**
 * @target stoermelder MIDI-KIT
 * @engine QuickJs@v1
 * @author stoermelder
 * @description Delays every Note-On message on channel 1 for 1500ms
 */

// midiOut.sendAfterMs(msg, ms) schedules a message instead of sending it right
// away. Only Note-Ons on channel 1 are delayed - and, since nothing else is sent,
// everything else is dropped. Add a midiOut.send(msg) for the other messages to
// let them through immediately.

midi.onMessage = function(midiInput, msg) {
    if (midi.isNoteOn(msg) && midi.getChannel(msg) === 1) {
        midiOut.sendAfterMs(msg, 1500);
    }
}
