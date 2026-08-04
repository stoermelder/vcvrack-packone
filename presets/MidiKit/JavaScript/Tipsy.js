/**
 * @target stoermelder MIDI-KIT
 * @engine QuickJs
 * @author stoermelder
 * @description Sends a text message via the Tipsy protocol on the trigger output whenever a trigger is received on the trigger input. Connect the trigger output to a module with Tipsy support (e.g. Transit).
 */

rack.onTrigger = function(trigPort) {
    // Send a text message; the mime type defaults to "text/plain"
    trig.sendTipsy("Hello Tipsy!");

    // Send with an explicit mime type (e.g. JSON data)
    // let config = JSON.stringify({ label: "My snapshot", value: 42 });
    // trig.sendTipsy(config, "application/json");
}
