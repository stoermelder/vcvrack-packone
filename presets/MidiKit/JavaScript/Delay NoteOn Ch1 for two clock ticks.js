/**
 * @target stoermelder MIDI-KIT
 * @engine QuickJs@v1
 * @author stoermelder
 * @description Delays every Note-On message on channel 1 for two clock ticks on the clock input
 */

// The delayed Note-Ons are counted in ticks of the trigger input's clock, so
// that clock must be enabled — without trig.enableIn() the module does not
// process the trigger input at all and the delayed sends never fire.
trig.enableIn(1, 1);

midi.onMessage = function(midiInput, msg) {
    if (midi.isNoteOn(msg)) {
        if (midi.getChannel(msg) === 1) {
            midiOut.sendAfterTrigger(msg, 2);
        }
    }
}