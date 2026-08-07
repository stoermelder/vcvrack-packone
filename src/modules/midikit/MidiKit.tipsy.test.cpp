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
    while (any || !m->tipsyOutQueue.empty());
	return out;
}

TEST_CASE("sendTipsy queues and outputs the encoded stream on the trigger CV", "[MidiKit][Tipsy]") {
	// JavaScript (data first, mime defaults to "text/plain")
	const char* JS_SCRIPT = R"(/**
 * @engine QuickJs@v1
 */
rack.onMidiMessage = function(midiPort, msg) {
	trig.sendTipsy("Hello Tipsy!");
};
)";

	MidiKitModule* m = createModule();
	m->loadScript(JS_SCRIPT);
	REQUIRE(m->activeEngine != nullptr);

	midi::Message in = noteOn(1, 60, 100);
	m->activeEngine->processInMessage(0, in);
	m->activeEngine->process(); // SyncTaskWorker: runs rack.onMidiMessage inline

	// sendTipsy must have queued the payload for the audio thread.
	REQUIRE(m->tipsyOutQueue.size() > 0);

	std::vector<float> voltages = drainTipsy(m);
	REQUIRE(voltages.size() > 0);
	// The protocol always begins with the message-begin sentinel.
	REQUIRE(voltages[0] == tipsy::kMessageBeginSentinel);
	// The trigger output reflects the last drained voltage.
	REQUIRE(m->outputs[MidiKitModule::OUTPUT_TRIG].getVoltage(0) == voltages.back());
	// Everything was drained: no pending messages, encoder idle.
	REQUIRE(m->tipsyOutQueue.empty());
	REQUIRE(m->tipsyOutEncoder.isDormant());
	Test::destroyModule(m);

	// Lua (explicit mime, reversed argument order — must match JS output)
	const char* LUA_SCRIPT = R"(--[[
@engine minilua@v1
--]]
rack.onMidiMessage = function(midiPort, msg)
	trig.sendTipsy("Hello Tipsy!", "text/plain")
end
)";

	m = createModule();
	m->loadScript(LUA_SCRIPT);
	REQUIRE(m->activeEngine != nullptr);

	m->activeEngine->processInMessage(0, in);
	m->activeEngine->process();

	REQUIRE(m->tipsyOutQueue.size() > 0);
	std::vector<float> luaVoltages = drainTipsy(m);
	REQUIRE(luaVoltages == voltages);
	Test::destroyModule(m);
}

TEST_CASE("sendTipsy rejects invalid arguments", "[MidiKit][Tipsy]") {
	const char* JS_SCRIPT = R"(/**
 * @engine QuickJs@v1
 */
rack.onMidiMessage = function(midiPort, msg) {
	trig.sendTipsy("x");
};
)";

	MidiKitModule* m = createModule();
	m->loadScript(JS_SCRIPT);
	REQUIRE(m->activeEngine != nullptr);

	// loadScript() queues a discard sentinel; consume it so the queue starts empty.
	m->processTipsyOutput(0);
	REQUIRE(m->tipsyOutQueue.empty());

	const unsigned char* data = reinterpret_cast<const unsigned char*>("data");
	// A missing mime type or payload is rejected.
	REQUIRE_FALSE(m->sendTipsyOut(nullptr, data, 4));
	REQUIRE_FALSE(m->sendTipsyOut("text/plain", nullptr, 4));
	// An empty mime type is rejected: it would be indistinguishable from the
	// discard sentinel sendTipsyOutReset() enqueues.
	REQUIRE_FALSE(m->sendTipsyOut("", data, 4));
	REQUIRE(m->tipsyOutQueue.empty());

	// A valid call succeeds and queues the payload.
	REQUIRE(m->sendTipsyOut("text/plain", data, 4));
	REQUIRE(m->tipsyOutQueue.size() == 1);
	Test::destroyModule(m);
}

TEST_CASE("sendTipsy drops messages when the pending queue overflows", "[MidiKit][Tipsy]") {
	const char* JS_SCRIPT = R"(/**
 * @engine QuickJs@v1
 */
rack.onMidiMessage = function(midiPort, msg) {
	trig.sendTipsy("x");
};
)";

	MidiKitModule* m = createModule();
	m->loadScript(JS_SCRIPT);
	REQUIRE(m->activeEngine != nullptr);

	// loadScript() queues a discard sentinel; consume it so the queue starts empty.
	m->processTipsyOutput(0);
	REQUIRE(m->tipsyOutQueue.empty());

	const unsigned char* data = reinterpret_cast<const unsigned char*>("data");

	// The queue holds 8, but sendTipsyOut() keeps the last slot free for a discard
	// sentinel, so only 7 messages can be queued.
	for (int i = 0; i < 7; i++) {
		REQUIRE(m->sendTipsyOut("text/plain", data, 4));
	}
	REQUIRE(m->tipsyOutQueue.size() == 7);

	// An 8th message overflows: it is rejected and nothing is queued.
	REQUIRE_FALSE(m->sendTipsyOut("text/plain", data, 4));
	REQUIRE(m->tipsyOutQueue.size() == 7);

	// The reserved slot is still available to a discard, even when full.
	m->sendTipsyOutReset();
	REQUIRE(m->tipsyOutQueue.full());

	// Draining frees slots so new messages can be queued again. The pending
	// messages are ahead of the sentinel, so the discard drops them all.
	drainTipsy(m);
	REQUIRE(m->tipsyOutQueue.empty());
	REQUIRE(m->sendTipsyOut("text/plain", data, 4));
	REQUIRE(m->tipsyOutQueue.size() == 1);
	Test::destroyModule(m);
}

TEST_CASE("sendTipsyOutReset drops queued messages but completes the current one", "[MidiKit][Tipsy]") {
	const char* JS_SCRIPT = R"(/**
 * @engine QuickJs@v1
 */
rack.onMidiMessage = function(midiPort, msg) {
	trig.sendTipsy("x");
};
)";

	MidiKitModule* m = createModule();
	m->loadScript(JS_SCRIPT);
	REQUIRE(m->activeEngine != nullptr);

	// loadScript() queues a discard sentinel; consume it so the queue starts empty.
	m->processTipsyOutput(0);
	REQUIRE(m->tipsyOutQueue.empty());

	const unsigned char* data = reinterpret_cast<const unsigned char*>("data");
	REQUIRE(m->sendTipsyOut("text/plain", data, 4));
	REQUIRE(m->sendTipsyOut("text/plain", data, 4));

	// Start encoding the first message, then discard mid-stream.
	REQUIRE(m->processTipsyOutput(0));
	REQUIRE_FALSE(m->tipsyOutEncoder.isDormant());
	m->sendTipsyOutReset();

	// The in-flight message still finishes: voltages keep coming until the
	// encoder goes dormant of its own accord.
	int emitted = 1;
	while (!m->tipsyOutEncoder.isDormant()) {
		REQUIRE(m->processTipsyOutput(0));
		emitted++;
	}
	REQUIRE(emitted > 1);

	// The queued message behind it was dropped rather than emitted, and the
	// sentinel was consumed with it.
	REQUIRE_FALSE(m->processTipsyOutput(0));
	REQUIRE(m->tipsyOutQueue.empty());

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
rack.onMidiMessage = function(midiPort, msg) {
	trig.sendTipsy("x");
};
)";

	MidiKitModule* m = createModule();
	m->loadScript(JS_SCRIPT);
	REQUIRE(m->activeEngine != nullptr);

	// loadScript() queues a discard sentinel; consume it so the queue starts empty.
	m->processTipsyOutput(0);
	REQUIRE(m->tipsyOutQueue.empty());

	const unsigned char* data = reinterpret_cast<const unsigned char*>("data");
	REQUIRE(m->sendTipsyOut("text/plain", data, 4));
	m->sendTipsyOutReset();
	REQUIRE(m->sendTipsyOut("text/plain", data, 4));
	m->sendTipsyOutReset();

	// Nothing is emitted: both batches sit ahead of an unconsumed sentinel.
	REQUIRE_FALSE(m->processTipsyOutput(0));
	REQUIRE(m->tipsyOutQueue.empty());
	REQUIRE(m->tipsyOutEncoder.isDormant());
	Test::destroyModule(m);
}

TEST_CASE("sendTipsy handles empty data", "[MidiKit][Tipsy]") {
	const char* JS_SCRIPT = R"(/**
 * @engine QuickJs@v1
 */
rack.onMidiMessage = function(midiPort, msg) {
	trig.sendTipsy("");
};
)";

	MidiKitModule* m = createModule();
	m->loadScript(JS_SCRIPT);
	REQUIRE(m->activeEngine != nullptr);

	midi::Message in = noteOn(1, 60, 100);
	m->activeEngine->processInMessage(0, in);
	m->activeEngine->process();

	// Empty payload still encodes the header and end sentinel.
	REQUIRE(m->tipsyOutQueue.size() > 0);
	std::vector<float> voltages = drainTipsy(m);
	REQUIRE(voltages.size() > 0);
	REQUIRE(voltages[0] == tipsy::kMessageBeginSentinel);
	Test::destroyModule(m);
}

TEST_CASE("sendTipsy output is reset when a script is reloaded", "[MidiKit][Tipsy]") {
	const char* JS_SCRIPT_1 = R"(/**
 * @engine QuickJs@v1
 */
rack.onMidiMessage = function(midiPort, msg) {
	trig.sendTipsy("First message");
};
)";

	const char* JS_SCRIPT_2 = R"(/**
 * @engine QuickJs@v1
 */
rack.onMidiMessage = function(midiPort, msg) {
	trig.sendTipsy('{"key":"value"}', "application/json");
};
)";

	MidiKitModule* m = createModule();
	m->loadScript(JS_SCRIPT_1);
	REQUIRE(m->activeEngine != nullptr);

	midi::Message in = noteOn(1, 60, 100);
	m->activeEngine->processInMessage(0, in);
	m->activeEngine->process();
	REQUIRE(m->tipsyOutQueue.size() > 0);

	// Loading a script requests a discard, which queues a sentinel rather than
	// clearing (clear() writes the consumer's index and the worker must not
	// touch it). The stale message is still queued until the audio thread runs.
	m->loadScript(JS_SCRIPT_2);
	REQUIRE(m->tipsyOutEncoder.isDormant());

	// The audio thread drops the stale message instead of emitting it.
	REQUIRE_FALSE(m->processTipsyOutput(0));
	REQUIRE(m->tipsyOutQueue.empty());

	// The new script's sendTipsy works normally afterwards.
	m->activeEngine->processInMessage(0, in);
	m->activeEngine->process();
	REQUIRE(m->tipsyOutQueue.size() > 0);
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
		REQUIRE(m->activeEngine != nullptr);

		// rack.onTrigger(1) must enqueue a Tipsy message on the trigger output.
		m->activeEngine->processInTick(0);
		m->activeEngine->process(); // SyncTaskWorker: runs rack.onTrigger inline
		REQUIRE(m->tipsyOutQueue.size() > 0);

		std::vector<float> voltages = drainTipsy(m);
		REQUIRE(voltages.size() > 0);
		REQUIRE(voltages[0] == tipsy::kMessageBeginSentinel);
		Test::destroyModule(m);
	};

	runExample("presets/MidiKit/JavaScript/Tipsy.js");
	runExample("presets/MidiKit/Lua/Tipsy.lua");
}
