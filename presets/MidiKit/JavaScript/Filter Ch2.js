/**
 * @target stoermelder MIDI-KIT
 * @engine QuickJs
 * @author stoermelder
 * @description Drops all incoming MIDI messages except for MIDI channel 2
 */

function onMidiMessage(midiPort, msg) {
    if (midi.getChannel(msg) === 2) {
        midiOut.send(msg);
    }
}
