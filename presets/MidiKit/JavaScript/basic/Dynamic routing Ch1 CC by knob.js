/**
 * @target stoermelder MIDI-KIT
 * @engine QuickJs@v1
 * @author stoermelder
 * @description Routes incoming CC messages on MIDI channel 1 to a MIDI channel set by parameter 1 on the panel
 */

// param.enable(1) makes panel knob 1 available to the script; param.getValue(1)
// returns its position as 0..1. Scaling that to 1-16 and applying it with
// midi.setChannel() routes every CC on channel 1 to the channel chosen by the
// knob. All other messages are forwarded unchanged.

rack.onLoad = function() {
    param.enable(1);
};

midi.onMessage = function(midiPort, msg) {
    if (midi.isCc(msg) && midi.getChannel(msg) === 1) {
        // 0..1 -> channel 1-16
        let ch = Math.ceil(param.getValue(1) * 16);
        midi.setChannel(msg, ch);
    }
    midiOut.send(msg);
}