#include "MidiKit.test.hpp"

// Tests for trig.sendTipsy() API.
//
// sendTipsy(data, [mimeType]) queues a Tipsy protocol message for output on
// the module's trigger CV output. The payload is enqueued on the script
// (worker) thread via the handler; the actual encoding runs on the audio thread
// in MidiKitModule::processTipsyOutput(), which initiates the next pending
// message when the encoder is idle and drains one encoded float per call. The
// first float of every message is the Tipsy message-begin sentinel (3.1f).

static midi::Message noteOn(int ch, int note, int vel) {
	midi::Message msg;
	msg.setSize(3);
	msg.setStatus(0x9);
	msg.setChannel(ch);
	msg.setNote(note);
	msg.setValue(vel);
	return msg;
}

// Drains every pending Tipsy message through processTipsyOutput() and returns
// the voltages, in order, as they land on the module's trigger output.
static std::vector<float> drainTipsy(MidiKitModule* m) {
	std::vector<float> out;
	bool any;
	do {
		any = m->processTipsyOutput(0);
		if (any) {
			out.push_back(m->outputs[MidiKitModule::OUTPUT_TRIG].getVoltage(0));
		}
	}
    while (any || !m->tipsyOut.outQueue.empty());
	return out;
}

TEST_CASE("sendTipsy queues and outputs the encoded stream on the trigger CV", "[MidiKit][Tipsy]") {
	// JavaScript (data first, mime defaults to "text/plain")
	const char* JS_SCRIPT = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(midiPort, msg) {
	trig.sendTipsy("Hello Tipsy!");
};
)";

	MidiKitModule* m = createModule();
	m->loadScript(JS_SCRIPT);
	REQUIRE(m->host.getActiveEngine() != nullptr);

	midi::Message in = noteOn(1, 60, 100);
	m->host.getActiveEngine()->processInMessage(0, in);
	m->host.getActiveEngine()->process(); // SyncTaskWorker: runs midi.onMessage inline

	// sendTipsy must have queued the payload for the audio thread.
	REQUIRE(m->tipsyOut.outQueue.size() > 0);

	std::vector<float> voltages = drainTipsy(m);
	REQUIRE(voltages.size() > 0);
	// The protocol always begins with the message-begin sentinel.
	REQUIRE(voltages[0] == tipsy::kMessageBeginSentinel);
	// The trigger output reflects the last drained voltage.
	REQUIRE(m->outputs[MidiKitModule::OUTPUT_TRIG].getVoltage(0) == voltages.back());
	// Everything was drained: no pending messages, encoder idle.
	REQUIRE(m->tipsyOut.outQueue.empty());
	REQUIRE(m->tipsyOut.encoder.isDormant());
	Test::destroyModule(m);

	// Lua (explicit mime, reversed argument order — must match JS output)
	const char* LUA_SCRIPT = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
	trig.sendTipsy("Hello Tipsy!", "text/plain")
end
)";

	m = createModule();
	m->loadScript(LUA_SCRIPT);
	REQUIRE(m->host.getActiveEngine() != nullptr);

	m->host.getActiveEngine()->processInMessage(0, in);
	m->host.getActiveEngine()->process();

	REQUIRE(m->tipsyOut.outQueue.size() > 0);
	std::vector<float> luaVoltages = drainTipsy(m);
	REQUIRE(luaVoltages == voltages);
	Test::destroyModule(m);
}

// The string path used to JS_FreeCString() the data buffer before
// sendTipsyOut() copied it (audit #1: use-after-free). Passing an explicit
// mimeType makes JS_ToCString allocate between the free and the copy, so the
// bug would corrupt the queued payload — check the queued bytes directly,
// including an embedded NUL.
TEST_CASE("trig.sendTipsy string payload arrives intact in the out-queue", "[MidiKit][Tipsy]") {
	const char* JS_SCRIPT = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(midiPort, msg) {
	trig.sendTipsy("binary\x00payload", "application/octet-stream");
};
)";

	MidiKitModule* m = createModule();
	m->loadScript(JS_SCRIPT);
	REQUIRE(m->host.getActiveEngine() != nullptr);

	// loadScript() queues a discard sentinel; consume it so the queue starts empty.
	m->processTipsyOutput(0);
	REQUIRE(m->tipsyOut.outQueue.empty());

	midi::Message in = noteOn(1, 60, 100);
	m->host.getActiveEngine()->processInMessage(0, in);
	m->host.getActiveEngine()->process(); // SyncTaskWorker: runs midi.onMessage inline

	REQUIRE(m->tipsyOut.outQueue.size() == 1);
	auto p = m->tipsyOut.outQueue.shift();
	// The 14-byte payload (with embedded NUL) and 24-byte mime were copied intact.
	REQUIRE(p.dataSize == 14);
	REQUIRE(std::string(reinterpret_cast<const char*>(p.data), p.dataSize) == std::string("binary\0payload", 14));
	REQUIRE(p.mimeSize == 24);
	REQUIRE(std::string(p.mime, p.mimeSize) == "application/octet-stream");
	Test::destroyModule(m);
}

TEST_CASE("sendTipsy rejects invalid arguments", "[MidiKit][Tipsy]") {
	const char* JS_SCRIPT = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(midiPort, msg) {
	trig.sendTipsy("x");
};
)";

	MidiKitModule* m = createModule();
	m->loadScript(JS_SCRIPT);
	REQUIRE(m->host.getActiveEngine() != nullptr);

	// loadScript() queues a discard sentinel; consume it so the queue starts empty.
	m->processTipsyOutput(0);
	REQUIRE(m->tipsyOut.outQueue.empty());

	const unsigned char* data = reinterpret_cast<const unsigned char*>("data");
	// A missing mime type or payload is rejected.
	REQUIRE_FALSE(m->sendTipsyOut(nullptr, data, 4));
	REQUIRE_FALSE(m->sendTipsyOut("text/plain", nullptr, 4));
	// An empty mime type is rejected: it would be indistinguishable from the
	// discard sentinel sendTipsyOutReset() enqueues.
	REQUIRE_FALSE(m->sendTipsyOut("", data, 4));
	REQUIRE(m->tipsyOut.outQueue.empty());

	// A valid call succeeds and queues the payload.
	REQUIRE(m->sendTipsyOut("text/plain", data, 4));
	REQUIRE(m->tipsyOut.outQueue.size() == 1);
	Test::destroyModule(m);
}

TEST_CASE("sendTipsy drops messages when the pending queue overflows", "[MidiKit][Tipsy]") {
	const char* JS_SCRIPT = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(midiPort, msg) {
	trig.sendTipsy("x");
};
)";

	MidiKitModule* m = createModule();
	m->loadScript(JS_SCRIPT);
	REQUIRE(m->host.getActiveEngine() != nullptr);

	// loadScript() queues a discard sentinel; consume it so the queue starts empty.
	m->processTipsyOutput(0);
	REQUIRE(m->tipsyOut.outQueue.empty());

	const unsigned char* data = reinterpret_cast<const unsigned char*>("data");

	// The queue holds 8, but sendTipsyOut() keeps the last slot free for a discard
	// sentinel, so only 7 messages can be queued.
	for (int i = 0; i < 7; i++) {
		REQUIRE(m->sendTipsyOut("text/plain", data, 4));
	}
	REQUIRE(m->tipsyOut.outQueue.size() == 7);

	// An 8th message overflows: it is rejected and nothing is queued.
	REQUIRE_FALSE(m->sendTipsyOut("text/plain", data, 4));
	REQUIRE(m->tipsyOut.outQueue.size() == 7);

	// The reserved slot is still available to a discard, even when full.
	m->sendTipsyOutReset();
	REQUIRE(m->tipsyOut.outQueue.full());

	// Draining frees slots so new messages can be queued again. The pending
	// messages are ahead of the sentinel, so the discard drops them all.
	drainTipsy(m);
	REQUIRE(m->tipsyOut.outQueue.empty());
	REQUIRE(m->sendTipsyOut("text/plain", data, 4));
	REQUIRE(m->tipsyOut.outQueue.size() == 1);
	Test::destroyModule(m);
}

TEST_CASE("sendTipsyOutReset drops queued messages but completes the current one", "[MidiKit][Tipsy]") {
	const char* JS_SCRIPT = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(midiPort, msg) {
	trig.sendTipsy("x");
};
)";

	MidiKitModule* m = createModule();
	m->loadScript(JS_SCRIPT);
	REQUIRE(m->host.getActiveEngine() != nullptr);

	// loadScript() queues a discard sentinel; consume it so the queue starts empty.
	m->processTipsyOutput(0);
	REQUIRE(m->tipsyOut.outQueue.empty());

	const unsigned char* data = reinterpret_cast<const unsigned char*>("data");
	REQUIRE(m->sendTipsyOut("text/plain", data, 4));
	REQUIRE(m->sendTipsyOut("text/plain", data, 4));

	// Start encoding the first message, then discard mid-stream.
	REQUIRE(m->processTipsyOutput(0));
	REQUIRE_FALSE(m->tipsyOut.encoder.isDormant());
	m->sendTipsyOutReset();

	// The in-flight message still finishes: voltages keep coming until the
	// encoder goes dormant of its own accord.
	int emitted = 1;
	while (!m->tipsyOut.encoder.isDormant()) {
		REQUIRE(m->processTipsyOutput(0));
		emitted++;
	}
	REQUIRE(emitted > 1);

	// The queued message behind it was dropped rather than emitted, and the
	// sentinel was consumed with it.
	REQUIRE_FALSE(m->processTipsyOutput(0));
	REQUIRE(m->tipsyOut.outQueue.empty());

	// A message queued after the discard survives.
	REQUIRE(m->sendTipsyOut("text/plain", data, 4));
	std::vector<float> voltages = drainTipsy(m);
	REQUIRE(voltages.size() > 0);
	REQUIRE(voltages[0] == tipsy::kMessageBeginSentinel);
	Test::destroyModule(m);
}

TEST_CASE("two discards in a row drop both batches", "[MidiKit][Tipsy]") {
	// The case a single boolean flag would fail: the second discard must not be
	// swallowed by the first, or the second batch plays out under a script that
	// has already been replaced.
	const char* JS_SCRIPT = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(midiPort, msg) {
	trig.sendTipsy("x");
};
)";

	MidiKitModule* m = createModule();
	m->loadScript(JS_SCRIPT);
	REQUIRE(m->host.getActiveEngine() != nullptr);

	// loadScript() queues a discard sentinel; consume it so the queue starts empty.
	m->processTipsyOutput(0);
	REQUIRE(m->tipsyOut.outQueue.empty());

	const unsigned char* data = reinterpret_cast<const unsigned char*>("data");
	REQUIRE(m->sendTipsyOut("text/plain", data, 4));
	m->sendTipsyOutReset();
	REQUIRE(m->sendTipsyOut("text/plain", data, 4));
	m->sendTipsyOutReset();

	// Nothing is emitted: both batches sit ahead of an unconsumed sentinel.
	REQUIRE_FALSE(m->processTipsyOutput(0));
	REQUIRE(m->tipsyOut.outQueue.empty());
	REQUIRE(m->tipsyOut.encoder.isDormant());
	Test::destroyModule(m);
}

TEST_CASE("sendTipsy handles empty data", "[MidiKit][Tipsy]") {
	const char* JS_SCRIPT = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(midiPort, msg) {
	trig.sendTipsy("");
};
)";

	MidiKitModule* m = createModule();
	m->loadScript(JS_SCRIPT);
	REQUIRE(m->host.getActiveEngine() != nullptr);

	midi::Message in = noteOn(1, 60, 100);
	m->host.getActiveEngine()->processInMessage(0, in);
	m->host.getActiveEngine()->process();

	// Empty payload still encodes the header and end sentinel.
	REQUIRE(m->tipsyOut.outQueue.size() > 0);
	std::vector<float> voltages = drainTipsy(m);
	REQUIRE(voltages.size() > 0);
	REQUIRE(voltages[0] == tipsy::kMessageBeginSentinel);
	Test::destroyModule(m);
}

TEST_CASE("sendTipsy output is reset when a script is reloaded", "[MidiKit][Tipsy]") {
	const char* JS_SCRIPT_1 = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(midiPort, msg) {
	trig.sendTipsy("First message");
};
)";

	const char* JS_SCRIPT_2 = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(midiPort, msg) {
	trig.sendTipsy('{"key":"value"}', "application/json");
};
)";

	MidiKitModule* m = createModule();
	m->loadScript(JS_SCRIPT_1);
	REQUIRE(m->host.getActiveEngine() != nullptr);

	midi::Message in = noteOn(1, 60, 100);
	m->host.getActiveEngine()->processInMessage(0, in);
	m->host.getActiveEngine()->process();
	REQUIRE(m->tipsyOut.outQueue.size() > 0);

	// Loading a script requests a discard, which queues a sentinel rather than
	// clearing (clear() writes the consumer's index and the worker must not
	// touch it). The stale message is still queued until the audio thread runs.
	m->loadScript(JS_SCRIPT_2);
	REQUIRE(m->tipsyOut.encoder.isDormant());

	// The audio thread drops the stale message instead of emitting it.
	REQUIRE_FALSE(m->processTipsyOutput(0));
	REQUIRE(m->tipsyOut.outQueue.empty());

	// The new script's sendTipsy works normally afterwards.
	m->host.getActiveEngine()->processInMessage(0, in);
	m->host.getActiveEngine()->process();
	REQUIRE(m->tipsyOut.outQueue.size() > 0);
	std::vector<float> voltages = drainTipsy(m);
	REQUIRE(voltages.size() > 0);
	REQUIRE(voltages[0] == tipsy::kMessageBeginSentinel);
	Test::destroyModule(m);
}

TEST_CASE("bundled Tipsy output example scripts work", "[MidiKit][Tipsy]") {
	// Reads one of the bundled example scripts and loads it into the module.
	auto runExample = [](const std::string& path) {
		std::ifstream f(path);
		REQUIRE(f.good());
		std::stringstream ss;
		ss << f.rdbuf();

		MidiKitModule* m = createModule();
		m->loadScript(ss.str());
		REQUIRE(m->host.getActiveEngine() != nullptr);

		// trig.onTrigger(1) must enqueue a Tipsy message on the trigger output.
		m->host.getActiveEngine()->processInTick(0, 0);
		m->host.getActiveEngine()->process(); // SyncTaskWorker: runs trig.onTrigger inline
		REQUIRE(m->tipsyOut.outQueue.size() > 0);

		std::vector<float> voltages = drainTipsy(m);
		REQUIRE(voltages.size() > 0);
		REQUIRE(voltages[0] == tipsy::kMessageBeginSentinel);
		Test::destroyModule(m);
	};

	runExample("presets/MidiKit/JavaScript/Tipsy.js");
	runExample("presets/MidiKit/Lua/Tipsy.lua");
}


// ─── Tipsy input ─────────────────────────────────────────────────────────────
//
// trig.enableTipsyIn() routes the trigger input into the module's decoder.
// Decoding runs per sample on the audio thread in processTipsyInput(); completed
// messages go to the active engine's tipsyInQueue, which process() drains on
// the worker to trig.onTipsyMessage().

// Feeds `voltages` into the trigger input one sample at a time, stepping the
// decoder for each. Returns how many messages completed.
static int feedTipsy(MidiKitModule* m, int port, const std::vector<float>& voltages) {
	int completed = 0;
	for (float v : voltages) {
		m->inputs[MidiKitModule::INPUT_TRIG + port].setVoltage(v, 0);
		if (m->processTipsyInput()) completed++;
	}
	return completed;
}

// Encodes one message through the OUTPUT path and returns the voltages, so the
// input tests can be driven by the encoder rather than hand-built streams.
static std::vector<float> encodeTipsy(MidiKitModule* m, const char* mime, const std::string& data) {
	REQUIRE(m->sendTipsyOut(mime, reinterpret_cast<const unsigned char*>(data.data()), (uint32_t)data.size()));
	return drainTipsy(m);
}

TEST_CASE("Tipsy input round-trips an encoded message to trig.onTipsyMessage", "[MidiKit][Tipsy]") {
	// The script echoes what it receives into the log, so the test can assert on
	// the decoded mime type and payload without extra plumbing.
	const char* JS_SCRIPT = R"(/**
 * @engine QuickJs@v1
 */
trig.onTipsyMessage = function(data, mimeType) {
	rack.log("got:" + mimeType + ":" + data);
};
)";

	MidiKitModule* m = createModule();
	m->loadScript(JS_SCRIPT);
	REQUIRE(m->host.getActiveEngine() != nullptr);
	m->processTipsyInput();   // no trigger claimed yet: must be a no-op

	// Claim the trigger input and connect it.
	m->enableTipsyIn(0);
	m->inputs[MidiKitModule::INPUT_TRIG].channels = 1;

	std::vector<float> voltages = encodeTipsy(m, "text/plain", "Hello Tipsy!");
	REQUIRE(voltages.size() > 0);

	// Exactly one message completes, at the end of the stream.
	REQUIRE(feedTipsy(m, 0, voltages) == 1);
	REQUIRE(m->host.getActiveEngine()->tipsyInQueue.size() == 1);

	// The worker dispatches it into the script.
	m->host.getActiveEngine()->process();
	REQUIRE(m->host.getActiveEngine()->tipsyInQueue.empty());
	REQUIRE(drainLog(m).find("got:text/plain:Hello Tipsy!") != std::string::npos);
	Test::destroyModule(m);
}

TEST_CASE("Tipsy input round-trips under Lua", "[MidiKit][Tipsy]") {
	const char* LUA_SCRIPT = R"(--[[
@engine minilua@v1
--]]
trig.onTipsyMessage = function(data, mimeType)
	rack.log("got:" .. mimeType .. ":" .. data)
end
)";

	MidiKitModule* m = createModule();
	m->loadScript(LUA_SCRIPT);
	REQUIRE(m->host.getActiveEngine() != nullptr);

	m->enableTipsyIn(0);
	m->inputs[MidiKitModule::INPUT_TRIG].channels = 1;

	std::vector<float> voltages = encodeTipsy(m, "application/json", "{\"key\":42}");
	REQUIRE(feedTipsy(m, 0, voltages) == 1);

	m->host.getActiveEngine()->process();
	REQUIRE(drainLog(m).find("got:application/json:{\"key\":42}") != std::string::npos);
	Test::destroyModule(m);
}

TEST_CASE("Tipsy input ignores the stream until the trigger is claimed", "[MidiKit][Tipsy]") {
	const char* JS_SCRIPT = R"(/**
 * @engine QuickJs@v1
 */
trig.onTipsyMessage = function(data, mimeType) {
	rack.log("got:" + data);
};
)";

	MidiKitModule* m = createModule();
	m->loadScript(JS_SCRIPT);
	REQUIRE(m->host.getActiveEngine() != nullptr);

	m->inputs[MidiKitModule::INPUT_TRIG].channels = 1;
	std::vector<float> voltages = encodeTipsy(m, "text/plain", "unclaimed");

	// Decoding is off until a script claims the trigger input.
	REQUIRE(feedTipsy(m, 0, voltages) == 0);
	REQUIRE(m->host.getActiveEngine()->tipsyInQueue.empty());

	// Once claimed, the same stream decodes; disabling releases it again.
	m->enableTipsyIn(0);
	REQUIRE(feedTipsy(m, 0, voltages) == 1);
	REQUIRE(m->host.getActiveEngine()->tipsyInQueue.size() == 1);
	m->host.getActiveEngine()->process();
	REQUIRE(m->host.getActiveEngine()->tipsyInQueue.empty());

	m->enableTipsyIn(-1);
	REQUIRE(feedTipsy(m, 0, voltages) == 0);
	REQUIRE(m->host.getActiveEngine()->tipsyInQueue.empty());
	Test::destroyModule(m);
}

TEST_CASE("a Tipsy-claimed trigger reads as 0 and CV inputs stay live", "[MidiKit][Tipsy]") {
	const char* JS_SCRIPT = R"(/**
 * @engine QuickJs@v1
 */
trig.onTipsyMessage = function(data, mimeType) {};
)";

	MidiKitModule* m = createModule();
	m->loadScript(JS_SCRIPT);
	REQUIRE(m->host.getActiveEngine() != nullptr);

	// CV inputs are never touched by Tipsy decoding.
	m->inputs[MidiKitModule::INPUT + 1].channels = 1;
	m->enableInput(1);
	m->inputs[MidiKitModule::INPUT + 1].setVoltage(5.f, 0);
	REQUIRE(m->getInputVoltage(1, 0) == 5.f);

	// Only channel 0 of the trigger input carries the Tipsy stream: while
	// claimed, channel 0 reads as 0 — the raw encoded voltages are protocol,
	// not a gate a script should act on. Other channels are unaffected.
	m->inputs[MidiKitModule::INPUT_TRIG].channels = 2;
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(5.f, 0);
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(3.f, 1);
	REQUIRE(m->getTrigVoltage(0, 0) == 5.f);
	REQUIRE(m->getTrigVoltage(0, 1) == 3.f);
	m->enableTipsyIn(0);
	REQUIRE(m->getTrigVoltage(0, 0) == 0.f);
	// Channel 1 is not carrying the stream — it still reads normally.
	REQUIRE(m->getTrigVoltage(0, 1) == 3.f);

	// Releasing restores the trigger reading; the CV input was never masked.
	m->enableTipsyIn(-1);
	REQUIRE(m->getTrigVoltage(0, 0) == 5.f);
	REQUIRE(m->getTrigVoltage(0, 1) == 3.f);
	REQUIRE(m->getInputVoltage(1, 0) == 5.f);
	Test::destroyModule(m);
}

TEST_CASE("a Tipsy-claimed trigger input suppresses trig.onTrigger on channel 1 only", "[MidiKit][Tipsy]") {
	// The encoded Tipsy voltages swing across the trigger threshold constantly,
	// so while the trigger input is claimed they must not count as clock ticks
	// or fire trig.onTrigger on channel 1. Other channels are ordinary gates
	// and keep firing normally. Both channels are enabled (as the script's
	// trig.enableIn() calls do) so each would fire were it not for the claim.
	const char* JS_SCRIPT = R"(/**
 * @engine QuickJs@v1
 */
trig.enableIn(1, 1);
trig.enableIn(1, 2);

trig.onTrigger = function(trigPort, channel) {
	rack.log("trigger" + number.toString(channel));
};
trig.onTipsyMessage = function(data, mimeType) {};
)";

	MidiKitModule* m = createModule();
	m->loadScript(JS_SCRIPT);
	REQUIRE(m->host.getActiveEngine() != nullptr);

	m->inputs[MidiKitModule::INPUT_TRIG].channels = 2;

	// Pin both SchmittTriggers low first (a fresh trigger starts uninitialized;
	// the first low call locks it to LOW so a later rise is a real edge).
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f, 0);
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f, 1);
	m->process(Test::makeProcessArgs(1));
	m->host.getActiveEngine()->process();

	// Claim the trigger input for Tipsy: a rising edge on channel 1 must not
	// count a tick or fire trig.onTrigger there...
	m->enableTipsyIn(0);
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f, 0);
	m->process(Test::makeProcessArgs(2));
	m->host.getActiveEngine()->process();
	REQUIRE(m->triggersIn.triggerTick[0][0] == 0);
	REQUIRE(drainLog(m).find("trigger") == std::string::npos);

	// ...but channel 2 is an ordinary gate and still fires.
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f, 1);
	m->process(Test::makeProcessArgs(3));
	m->host.getActiveEngine()->process();
	REQUIRE(m->triggersIn.triggerTick[0][1] == 1);
	REQUIRE(drainLog(m).find("trigger2") != std::string::npos);

	// Releasing restores normal trigger behavior on channel 1.
	m->enableTipsyIn(-1);
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f, 0);
	m->process(Test::makeProcessArgs(4));
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f, 0);
	m->process(Test::makeProcessArgs(5));
	m->host.getActiveEngine()->process();
	REQUIRE(m->triggersIn.triggerTick[0][0] == 1);
	REQUIRE(drainLog(m).find("trigger1") != std::string::npos);
	Test::destroyModule(m);
}

TEST_CASE("Tipsy input resyncs after a malformed stream", "[MidiKit][Tipsy]") {
	const char* JS_SCRIPT = R"(/**
 * @engine QuickJs@v1
 */
trig.onTipsyMessage = function(data, mimeType) {
	rack.log("got:" + data);
};
)";

	MidiKitModule* m = createModule();
	m->loadScript(JS_SCRIPT);
	REQUIRE(m->host.getActiveEngine() != nullptr);

	m->enableTipsyIn(0);
	m->inputs[MidiKitModule::INPUT_TRIG].channels = 1;

	// Garbage that never opens a message decodes nothing and does not wedge.
	REQUIRE(feedTipsy(m, 0, {0.f, 1.f, -3.f, 2.5f, 0.1f}) == 0);
	REQUIRE(m->host.getActiveEngine()->tipsyInQueue.empty());

	// A valid message afterwards still decodes: the decoder resyncs on the next
	// message-begin sentinel.
	std::vector<float> voltages = encodeTipsy(m, "text/plain", "after noise");
	REQUIRE(feedTipsy(m, 0, voltages) == 1);
	m->host.getActiveEngine()->process();
	REQUIRE(drainLog(m).find("got:after noise") != std::string::npos);
	Test::destroyModule(m);
}

TEST_CASE("Tipsy input drops messages when the queue overflows", "[MidiKit][Tipsy]") {
	const char* JS_SCRIPT = R"(/**
 * @engine QuickJs@v1
 */
trig.onTipsyMessage = function(data, mimeType) {};
)";

	MidiKitModule* m = createModule();
	m->loadScript(JS_SCRIPT);
	REQUIRE(m->host.getActiveEngine() != nullptr);

	m->enableTipsyIn(0);
	m->inputs[MidiKitModule::INPUT_TRIG].channels = 1;

	// The in-queue holds 8. Feed 9 messages without draining: the 9th is
	// dropped rather than corrupting the queue.
	std::vector<float> voltages = encodeTipsy(m, "text/plain", "x");
	int completed = 0;
	for (int i = 0; i < 9; i++) {
		completed += feedTipsy(m, 0, voltages);
	}
	REQUIRE(completed == 8);
	REQUIRE(m->host.getActiveEngine()->tipsyInQueue.full());

	// Draining frees room again.
	m->host.getActiveEngine()->process();
	REQUIRE(m->host.getActiveEngine()->tipsyInQueue.empty());
	REQUIRE(feedTipsy(m, 0, voltages) == 1);
	Test::destroyModule(m);
}

TEST_CASE("bundled Tipsy input example scripts work", "[MidiKit][Tipsy]") {
	// Loads a bundled TipsyIn example, feeds it an encoded message, and checks
	// it reached trig.onTipsyMessage. Not in MidiKit.examples.test.cpp's
	// PRESETS[] table for the same reason the Tipsy sender isn't: it produces
	// no output from plain MIDI traffic, which is what that smoke test asserts.
	auto runExample = [](const std::string& path, const std::string& payload, const char* mime) {
		std::ifstream f(path);
		REQUIRE(f.good());
		std::stringstream ss;
		ss << f.rdbuf();

		MidiKitModule* m = createModule();
		m->loadScript(ss.str());
		REQUIRE(m->host.getActiveEngine() != nullptr);

		// The example claims the trigger input from rack.onLoad().
		REQUIRE(drainLog(m).find("Listening for Tipsy on TRIG") != std::string::npos);
		m->inputs[MidiKitModule::INPUT_TRIG].channels = 1;

		std::vector<float> voltages = encodeTipsy(m, mime, payload);
		REQUIRE(feedTipsy(m, 0, voltages) == 1);

		m->host.getActiveEngine()->process();
		std::string log = drainLog(m);
		REQUIRE(log.find("Tipsy [") != std::string::npos);
		REQUIRE(log.find(payload) != std::string::npos);
		Test::destroyModule(m);
	};

	runExample("presets/MidiKit/JavaScript/TipsyIn.js", "{\"value\":42}", "application/json");
	runExample("presets/MidiKit/Lua/TipsyIn.lua", "42", "text/plain");
}
