#include "MidiKit.test.hpp"

// Tests for trig.sendTipsy() API.
//
// sendTipsy(data, [mimeType]) queues a Tipsy protocol message for output on
// the module's trigger CV output. The payload is enqueued on the script
// (worker) thread; the actual encoding runs on the audio thread in
// MidiScriptEngine::processTipsyOutput(), which initiates the next pending
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
		any = m->activeEngine->processTipsyOutput(0);
		if (any) {
			out.push_back(m->outputs[MidiKitModule::OUTPUT_TRIG].getVoltage(0));
		}
	}
    while (any || !m->activeEngine->tipsyPendingQueue.empty());
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
	REQUIRE(m->activeEngine->tipsyPendingQueue.size() > 0);

	std::vector<float> voltages = drainTipsy(m);
	REQUIRE(voltages.size() > 0);
	// The protocol always begins with the message-begin sentinel.
	REQUIRE(voltages[0] == tipsy::kMessageBeginSentinel);
	// The trigger output reflects the last drained voltage.
	REQUIRE(m->outputs[MidiKitModule::OUTPUT_TRIG].getVoltage(0) == voltages.back());
	// Everything was drained: no pending messages, encoder idle.
	REQUIRE(m->activeEngine->tipsyPendingQueue.empty());
	REQUIRE(m->activeEngine->tipsyEncoder.isDormant());
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

	REQUIRE(m->activeEngine->tipsyPendingQueue.size() > 0);
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
	MidiScriptEngine* e = m->activeEngine;

	const unsigned char* data = reinterpret_cast<const unsigned char*>("data");
	// A missing mime type or payload is rejected.
	REQUIRE_FALSE(e->sendTipsy(nullptr, data, 4));
	REQUIRE_FALSE(e->sendTipsy("text/plain", nullptr, 4));
	REQUIRE(e->tipsyPendingQueue.empty());

	// A valid call succeeds and queues the payload.
	REQUIRE(e->sendTipsy("text/plain", data, 4));
	REQUIRE(e->tipsyPendingQueue.size() == 1);
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
	MidiScriptEngine* e = m->activeEngine;

	const unsigned char* data = reinterpret_cast<const unsigned char*>("data");

	// The pending queue holds 8 messages; the first 8 succeed and fill it.
	for (int i = 0; i < 8; i++) {
		REQUIRE(e->sendTipsy("text/plain", data, 4));
	}
	REQUIRE(e->tipsyPendingQueue.full());
	REQUIRE(e->tipsyPendingQueue.size() == 8);

	// A 9th message overflows: it is rejected and nothing is queued.
	REQUIRE_FALSE(e->sendTipsy("text/plain", data, 4));
	REQUIRE(e->tipsyPendingQueue.size() == 8);

	// Draining frees slots so new messages can be queued again.
	drainTipsy(m);
	REQUIRE(e->tipsyPendingQueue.empty());
	REQUIRE(e->sendTipsy("text/plain", data, 4));
	REQUIRE(e->tipsyPendingQueue.size() == 1);
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
	REQUIRE(m->activeEngine->tipsyPendingQueue.size() > 0);
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
	REQUIRE(m->activeEngine->tipsyPendingQueue.size() > 0);

	// Engine loadScript() must reset the Tipsy state.
	m->loadScript(JS_SCRIPT_2);
	REQUIRE(m->activeEngine->tipsyPendingQueue.empty());
	REQUIRE(m->activeEngine->tipsyEncoder.isDormant());

	// The new script's sendTipsy works normally afterwards.
	m->activeEngine->processInMessage(0, in);
	m->activeEngine->process();
	REQUIRE(m->activeEngine->tipsyPendingQueue.size() > 0);
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
		REQUIRE(m->activeEngine->tipsyPendingQueue.size() > 0);

		std::vector<float> voltages = drainTipsy(m);
		REQUIRE(voltages.size() > 0);
		REQUIRE(voltages[0] == tipsy::kMessageBeginSentinel);
		Test::destroyModule(m);
	};

	runExample("presets/MidiKit/JavaScript/Tipsy.js");
	runExample("presets/MidiKit/Lua/Tipsy.lua");
}
