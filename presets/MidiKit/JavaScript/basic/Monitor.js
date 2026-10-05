/**
 * @target stoermelder MIDI-KIT
 * @engine QuickJs@v1
 * @author stoermelder
 * @description Logs every incoming message except clock and active sensing, and forwards it
 */

// midi.toString(msg) turns any message into one line of text, in the wording of
// the MIDI-MON module. Clock arrives 24 times per beat and many keyboards send
// active sensing about three times per second, so both would drown the log; they
// are skipped here but still forwarded, like everything else. Open the log from
// the module's context menu to see the lines.

midi.onMessage = function(midiPort, msg, msgType) {
    if (msgType !== midi.CLOCK && msgType !== midi.ACTIVE_SENSING) {
        rack.log(midiPort + ": " + midi.toString(msg));
    }
    midiOut.send(msg);
}
