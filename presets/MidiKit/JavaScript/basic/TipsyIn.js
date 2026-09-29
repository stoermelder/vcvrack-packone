/**
 * @target stoermelder MIDI-KIT
 * @engine QuickJs@v1
 * @author stoermelder
 * @description Receives Tipsy messages on the trigger input and logs them. JSON payloads are parsed and forwarded as MIDI CC. Pair with Tipsy.js (or any Tipsy sender) patched into TRIG.
 */

// The receiving side of Tipsy: trig.enableTipsyIn() routes trigger input 1 into
// the Tipsy decoder, and every complete message is delivered to
// trig.onTipsyMessage(data, mimeType). Patch the output of a Tipsy sender
// (e.g. Tipsy.js) into TRIG. Received data is logged and, for a JSON payload with
// a numeric "value" field, forwarded as a MIDI CC.

rack.onLoad = function() {
    // Claim the trigger input for the Tipsy decoder. While claimed, the
    // trigger input no longer fires trig.onTrigger or counts ticks.
    trig.enableTipsyIn();
    rack.log("Listening for Tipsy on TRIG");
}

trig.onTipsyMessage = function(data, mimeType) {
    rack.log("Tipsy [" + mimeType + "] " + data);

    // A JSON payload can drive anything the script can reach — here the
    // "value" field is forwarded as CC 12 on the MIDI output.
    if (mimeType === "application/json") {
        let obj;
        try {
            obj = JSON.parse(data);
        }
        catch (e) {
            rack.log("  (not valid JSON: " + e + ")");
            return;
        }
        if (typeof obj.value === "number") {
            let msg = midi.create();
            midi.setCc(msg, 1, 12, Math.max(0, Math.min(127, obj.value | 0)));
            midiOut.send(msg);
        }
    }
}
