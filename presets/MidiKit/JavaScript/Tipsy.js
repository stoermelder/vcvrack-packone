/**
 * @target stoermelder MIDI-KIT
 * @engine QuickJs@v1
 * @author stoermelder
 * @description Sends a text message via the Tipsy protocol on the trigger output whenever a trigger is received on the trigger input. Connect the trigger output to a module with Tipsy support (e.g. Transit).
 */

// One message per trigger pulse, clocked by channel 1 only — enabling just
// channel 1 gates trig.onTrigger to it (other poly channels are ignored).
trig.enableIn(1, 1);

trig.onTrigger = function(trigPort, channel) {
    // Send a text message; the mime type defaults to "text/plain"
    trig.sendTipsy("Hello Tipsy!");

    // Send with an explicit mime type (e.g. JSON data)
    // let config = JSON.stringify({ label: "My snapshot", value: 42 });
    // trig.sendTipsy(config, "application/json");
}
