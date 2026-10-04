/**
 * @target stoermelder MIDI-KIT
 * @engine QuickJs@v1
 * @author stoermelder
 * @description Delays every note (Note-On and Note-Off) on channel 1 for two clock ticks on the clock input
 */

// Like the millisecond delay, but musical: midiOut.sendAfterTrigger(msg, ticks)
// waits for a number of rising edges on a trigger input (default: trigger input 1,
// channel 1), so the delay follows the tempo of a clock patched into it. Only
// notes on channel 1 are delayed, Note-Offs along with their Note-Ons so no note
// hangs (a Note-On with velocity 0 counts as a Note-On); everything else is dropped.

rack.onLoad = function() {
    // The delayed notes are counted in ticks of the trigger input's clock, so
    // that clock must be enabled — without trig.enableIn() the module does not
    // process the trigger input at all and the delayed sends never fire.
    trig.enableIn(1, 1);
};

midi.onMessage = function(midiInput, msg) {
    if (midi.isNoteOn(msg) || midi.isNoteOff(msg)) {
        if (midi.getChannel(msg) === 1) {
            midiOut.sendAfterTrigger(msg, 2);
        }
    }
}