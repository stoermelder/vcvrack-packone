/**
 * @target stoermelder MIDI-KIT
 * @engine Elk
 * @author stoermelder
 * @description Duplicates all CC messages on channel 1 on channel 2
 */

onMidiMessage = function(midiPort, msg) {
    if (midi.getChannel(msg) === 1) {
        if (midi.isCc(msg)) {
            let msg2 = midi.create();
            midi.setCc(msg2, 2, midi.getNote(msg), midi.getValue(msg));
            midiOut.send(msg2);
        }
    }
    midiOut.send(msg);
};