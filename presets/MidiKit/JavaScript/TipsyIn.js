/**
 * @target stoermelder MIDI-KIT
 * @engine QuickJs@v1
 * @author stoermelder
 * @description Receives Tipsy messages on the trigger input and logs them. JSON payloads are parsed and forwarded as MIDI CC. Pair with Tipsy.js (or any Tipsy sender) patched into TRIG.
 */

rack.onLoad = function() {
    // Claim the trigger input for the Tipsy decoder. While claimed, the
    // trigger input no longer fires rack.onTrigger or counts ticks.
    trig.enableTipsyIn();
    rack.log("Listening for Tipsy on TRIG");
}

rack.onTipsyMessage = function(data, mimeType) {
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
