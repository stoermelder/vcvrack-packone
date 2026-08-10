#include "MidiKit.test.hpp"
#include <fstream>

using namespace StoermelderPackOne::MidiScript;

// Minimal QuickJs script header (body can be empty — the engine still loads it)
static constexpr const char* QUICKJS_SCRIPT =
	"/**\n"
	" * @engine QuickJs@v1\n"
	" */\n";

// Minimal Lua script (synchronously loaded, no body needed)
static constexpr const char* LUA_SCRIPT =
	"--[[\n"
	"@engine minilua@v1\n"
	"--]]\n";


TEST_CASE("Construction and initialization", "[MidiKit]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");

	REQUIRE(m != nullptr);
	REQUIRE(m->NUM_PARAMS == 4);
	REQUIRE(m->NUM_INPUTS == 5);   // 4 voltage + 1 trigger
	REQUIRE(m->NUM_OUTPUTS == 1);  // trigger out
	REQUIRE(m->NUM_LIGHTS == 0);
	REQUIRE(m->host.script == "");
	REQUIRE(m->sample == 0);
	REQUIRE(m->inputTriggerTick[0] == 0);

	Test::destroyModule(m);
}


TEST_CASE("Preset JSON null-guards", "[MidiKit][JSON]") {
	auto module = Test::createModule<MidiKitModule>("MidiKit");

	SECTION("All top-level properties are null-guarded in dataFromJson()") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetNullGuards(module, rootJ);
		json_decref(rootJ);
	}

	Test::destroyModule(module);
}


TEST_CASE("process() does not crash with no script", "[MidiKit]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");

	// With no engine loaded, the dispatch path (processInMessage/processInTick/
	// activeEngine->process()) is skipped, but the module's own out-queue drain
	// and sample counting are unconditional — it must simply not crash.
	for (int i = 0; i < 20; i++) {
		REQUIRE_NOTHROW(m->process(Test::makeProcessArgs(i + 1)));
	}

	REQUIRE(m->sample == 20);

	Test::destroyModule(m);
}

TEST_CASE("Default engine it not set", "[MidiKit]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");

	REQUIRE(m->host.getActiveEngine() == nullptr);

	Test::destroyModule(m);
}

TEST_CASE("@engine minilua@v1 header selects Lua engine", "[MidiKit]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");

	m->loadScript(LUA_SCRIPT);

	REQUIRE(m->host.isLuaEngine());

	Test::destroyModule(m);
}

TEST_CASE("QuickJs header keeps QuickJs engine active", "[MidiKit]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");

	// First switch to Lua, then switch back via a QuickJs-tagged script
	m->loadScript(LUA_SCRIPT);
	REQUIRE(m->host.isLuaEngine());

	m->loadScript(QUICKJS_SCRIPT);
	REQUIRE(m->host.isQuickJsEngine());

	Test::destroyModule(m);
}

TEST_CASE("clearScript resets to empty and restores no engine", "[MidiKit]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");

	m->loadScript(LUA_SCRIPT);
	REQUIRE(m->host.isLuaEngine());

	m->clearScript();

	REQUIRE(m->host.script == "");
	REQUIRE(m->host.getActiveEngine() == nullptr);

	Test::destroyModule(m);
}

TEST_CASE("Trigger input increments inputTriggerTick", "[MidiKit]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");

	// With no default engine, load a script so process() runs past the
	// `if (!activeEngine) return;` guard. The trigger is enabled directly here
	// (as the script's trig.enableIn(1) would do) so the module processes ticks.
	m->loadScript(QUICKJS_SCRIPT);
	m->enableTrigger(0, 0);

	m->inputs[MidiKitModule::INPUT_TRIG].channels = 1;

	// Prime the SchmittTrigger to LOW state before the first rising edge
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f);
	m->process(Test::makeProcessArgs(0));

	// Rising edge → tick increments
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f);
	m->process(Test::makeProcessArgs(1));
	REQUIRE(m->inputTriggerTick[0] == 1);

	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f);
	m->process(Test::makeProcessArgs(2));
	REQUIRE(m->inputTriggerTick[0] == 1);  // no change on falling edge

	// Second pulse
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f);
	m->process(Test::makeProcessArgs(3));
	REQUIRE(m->inputTriggerTick[0] == 2);

	Test::destroyModule(m);
}

TEST_CASE("Trigger input is not processed until the trigger is enabled", "[MidiKit]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");

	// With no default engine, load a script so process() runs past the
	// `if (!activeEngine) return;` guard.
	m->loadScript(QUICKJS_SCRIPT);

	m->inputs[MidiKitModule::INPUT_TRIG].channels = 1;

	// Prime the SchmittTrigger LOW, then pulse — without trig.enableIn() the
	// module must not process triggers at all: no tick counting, no
	// tick-scheduled drains, and no trig.onTrigger dispatch.
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f);
	m->process(Test::makeProcessArgs(0));
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f);
	m->process(Test::makeProcessArgs(1));
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f);
	m->process(Test::makeProcessArgs(2));
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f);
	m->process(Test::makeProcessArgs(3));
	REQUIRE(m->inputTriggerTick[0] == 0);

	// Enabling the channel (as the script's trig.enableIn(1) would do) turns
	// trigger processing on — the next rising edge counts a tick.
	m->enableTrigger(0, 0);
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f);
	m->process(Test::makeProcessArgs(4));
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f);
	m->process(Test::makeProcessArgs(5));
	REQUIRE(m->inputTriggerTick[0] == 1);

	Test::destroyModule(m);
}

TEST_CASE("Polyphonic trigger input counts ticks per channel", "[MidiKit]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");

	m->loadScript(QUICKJS_SCRIPT);

	// Enable both trigger channels (as the script's trig.enableIn(1, 1) and
	// trig.enableIn(1, 2) would do) so the module counts ticks on each.
	m->enableTrigger(0, 0);
	m->enableTrigger(0, 1);

	m->inputs[MidiKitModule::INPUT_TRIG].channels = 2;

	// Prime both SchmittTriggers LOW before the first rising edges.
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f, 0);
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f, 1);
	m->process(Test::makeProcessArgs(0));

	// Channel 1 fires twice, channel 2 fires once.
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f, 0);
	m->process(Test::makeProcessArgs(1));
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f, 0);
	m->process(Test::makeProcessArgs(2));
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f, 0);
	m->process(Test::makeProcessArgs(3));
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f, 1);
	m->process(Test::makeProcessArgs(4));

	REQUIRE(m->inputTriggerTick[0] == 2);
	REQUIRE(m->inputTriggerTick[1] == 1);

	Test::destroyModule(m);
}

TEST_CASE("JSON round-trip preserves panelTheme and script", "[MidiKit]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");

	m->panelTheme = 2;
	m->loadScript(LUA_SCRIPT);

	json_t* j = m->dataToJson();

	m->panelTheme = 0;
	m->clearScript();
	REQUIRE(m->host.script == "");

	m->dataFromJson(j);
	json_decref(j);

	REQUIRE(m->panelTheme == 2);
	REQUIRE(m->host.script == LUA_SCRIPT);
	REQUIRE(m->host.isLuaEngine());

	Test::destroyModule(m);
}

TEST_CASE("process() does not crash with Lua script loaded", "[MidiKit]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");

	m->loadScript(LUA_SCRIPT);

	for (int i = 0; i < 20; i++) {
		REQUIRE_NOTHROW(m->process(Test::makeProcessArgs(i + 1)));
	}

	Test::destroyModule(m);
}


// MidiOutput::processTick — tick-scheduled sends
//
// midi::Output::sendMessage is non-virtual and no-ops without a subscribed
// device, so a send is not directly observable here. These tests assert on
// queue drainage instead: an entry leaves tickQueue exactly when it is sent,
// which is the property the ">= vs ==" bug turned on.

static midi::Message makeCc() {
	midi::Message msg;
	msg.setSize(3);
	msg.setStatus(0xb);
	msg.setChannel(0);
	msg.setNote(20);
	msg.setValue(100);
	return msg;
}

TEST_CASE("processTick sends a message on its exact tick", "[MidiKit]") {
	MidiOutput out;
	midi::Message msg = makeCc();

	out.send(msg, 0, 5);
	REQUIRE(out.tickQueue[0].size() == 1);

	out.processTick(0, 4);
	REQUIRE(out.tickQueue[0].size() == 1);  // not due yet

	out.processTick(0, 5);
	REQUIRE(out.tickQueue[0].size() == 0);
}

TEST_CASE("processTick sends a message whose tick has already passed", "[MidiKit]") {
	MidiOutput out;
	midi::Message msg = makeCc();

	// process() calls processTick() before draining the engine out-queue, so a
	// script can schedule for a tick the counter has already consumed.
	out.send(msg, 0, 5);
	REQUIRE(out.tickQueue[0].size() == 1);

	out.processTick(0, 6);
	REQUIRE(out.tickQueue[0].size() == 0);  // with "==" this stayed queued forever
}

TEST_CASE("processTick drains every due message in one call", "[MidiKit]") {
	MidiOutput out;
	midi::Message msg = makeCc();

	out.send(msg, 0, 3);
	out.send(msg, 0, 5);
	out.send(msg, 0, 7);
	REQUIRE(out.tickQueue[0].size() == 3);

	out.processTick(0, 5);
	REQUIRE(out.tickQueue[0].size() == 1);        // 3 and 5 sent, 7 still pending
	REQUIRE(out.tickQueue[0].top().tick == 7);

	out.processTick(0, 7);
	REQUIRE(out.tickQueue[0].size() == 0);
}

TEST_CASE("processTick: a stale entry does not block later messages", "[MidiKit]") {
	MidiOutput out;
	midi::Message msg = makeCc();

	// tickQueue is ordered smallest-tick-first, so the stale entry sits at the
	// head. With "==" it was never popped and blocked everything behind it.
	out.send(msg, 0, 2);   // stale — this tick is already in the past
	out.send(msg, 0, 7);   // legitimately scheduled for later
	REQUIRE(out.tickQueue[0].size() == 2);

	out.processTick(0, 7);
	REQUIRE(out.tickQueue[0].size() == 0);  // both drained, not stuck at the head
}

TEST_CASE("processTick leaves not-yet-due messages queued", "[MidiKit]") {
	MidiOutput out;
	midi::Message msg = makeCc();

	out.send(msg, 0, 10);

	for (uint64_t t = 0; t < 10; t++) {
		out.processTick(0, t);
		REQUIRE(out.tickQueue[0].size() == 1);
	}

	out.processTick(0, 10);
	REQUIRE(out.tickQueue[0].size() == 0);
}

TEST_CASE("processFrame sends a message on its exact frame", "[MidiKit]") {
	MidiOutput out;
	midi::Message msg = makeCc();
	msg.frame = 5;

	out.send(msg, 0, 0);
	REQUIRE(out.frameQueue.size() == 1);

	out.processFrame(4);
	REQUIRE(out.frameQueue.size() == 1);  // not due yet

	out.processFrame(5);
	REQUIRE(out.frameQueue.size() == 0);  // with ">" this stayed queued one call longer
}

TEST_CASE("processFrame sends a message whose frame has already passed", "[MidiKit]") {
	MidiOutput out;
	midi::Message msg = makeCc();
	msg.frame = 5;

	out.send(msg, 0, 0);
	REQUIRE(out.frameQueue.size() == 1);

	out.processFrame(6);
	REQUIRE(out.frameQueue.size() == 0);
}

TEST_CASE("processFrame drains every due message in one call", "[MidiKit]") {
	MidiOutput out;
	midi::Message msg = makeCc();

	msg.frame = 3;
	out.send(msg, 0, 0);
	msg.frame = 5;
	out.send(msg, 0, 0);
	msg.frame = 7;
	out.send(msg, 0, 0);
	REQUIRE(out.frameQueue.size() == 3);

	out.processFrame(5);
	REQUIRE(out.frameQueue.size() == 1);         // 3 and 5 sent, 7 still pending
	REQUIRE(out.frameQueue.top().msg.frame == 7);

	out.processFrame(7);
	REQUIRE(out.frameQueue.size() == 0);
}

TEST_CASE("processFrame leaves not-yet-due messages queued", "[MidiKit]") {
	MidiOutput out;
	midi::Message msg = makeCc();
	msg.frame = 10;

	out.send(msg, 0, 0);
	REQUIRE(out.frameQueue.size() == 1);

	for (int64_t f = 0; f < 10; f++) {
		out.processFrame(f);
		REQUIRE(out.frameQueue.size() == 1);
	}

	out.processFrame(10);
	REQUIRE(out.frameQueue.size() == 0);
}

// process() ordering and the divider boundary
//
// The engine interface is virtual, so a recording stub can observe exactly
// which process() calls reach the engine and what frame each one saw. That
// makes the trigger/divider interleaving assertable rather than inferred from
// queue side effects.

struct RecordingEngine : MidiScriptEngine {
	int processCalls = 0;
	// Messages to emit via handler->sendMidi() on the next process() call, as
	// (ticks) — one per pending entry, all drained in one call.
	std::vector<int> pending;
	// inputTriggerTick observed at the moment the engine emitted each message.
	std::vector<uint64_t> tickAtEmit;
	// Ordered record of which engine callbacks the module made, for asserting
	// the relative order of trigger/inbound/outbound effects in one process()
	// call. Appended by processInTick/processInMessage/process.
	std::vector<int> events;
	enum Event { TICK = 1, MESSAGE, PROCESS };
	MidiKitModule* module;

	// Uses the real MidiKitModule as its handler rather than a test double, so
	// sendMidi() reaches the same module out-queue process() drains — the queue
	// is module-owned now, so a double would have to reimplement the thing
	// under test. A worker is not optional: every engine needs one before any
	// dispatch path (including closeState() from onRemove()) can run.
	explicit RecordingEngine(MidiKitModule* module) : MidiScriptEngine(module, 4, 1, 1, 4, 1, 1), module(module) {
		setWorker(std::make_shared<StoermelderPackOne::SyncTaskWorker>());
	}

	void process() override {
		processCalls++;
		events.push_back(PROCESS);
		for (int ticks : pending) {
			midi::Message msg = makeCc();
			handler->sendMidi(0, &msg, 1, 0, ticks);
			tickAtEmit.push_back(module->inputTriggerTick[0]);
		}
		pending.clear();
	}

	// Unused by these tests — stubbed only to satisfy the interface.
	void loadScriptOnWorker(const char* script, const std::string& persistedConfigJson) override { }
	bool testScript(const std::string& script) override { return false; }
	void closeStateOnWorker() override { }
	bool captureConfig(std::string& out) override { return false; }
	// Everything the module handed over, in order, so tests can assert on the
	// decode result the audio thread produced.
	std::vector<StoermelderPackOne::MidiScript::QueuedMessage> received;
	void processInMessage(int midiPort, const StoermelderPackOne::MidiScript::QueuedMessage& msg) override {
		received.push_back(msg);
		events.push_back(MESSAGE);
	}
	void processInTick(int trigPort, uint8_t channel) override {
		events.push_back(TICK);
	}
	void dispatchMidiMessage(int midiPort, midi::Message& msg) override { }
	void dispatchNrpn(int midiPort, const StoermelderPackOne::MidiScript::QueuedMessage& q, bool isRpn) override { }
	void dispatchCc14bit(int midiPort, const StoermelderPackOne::MidiScript::QueuedMessage& q) override { }
	void dispatchTrigger(int trigPort, uint8_t channel) override { }
	void dispatchTipsyMessage(const StoermelderPackOne::MidiScript::TipsyMessage& msg) override { }
	std::string getInputName(int i) override { return ""; }
	std::string getParamName(int i) override { return ""; }
	std::string getParamFormatValue(int i) override { return ""; }
	void getContextMenus(const std::function<void(const std::vector<StoermelderPackOne::MidiScript::ScriptMenuItem>&)>& callback) override {
		std::vector<StoermelderPackOne::MidiScript::ScriptMenuItem> empty;
		callback(empty);
	}
	void invokeContextMenuCallback(int callbackId, int value) override { }
};

// Drives one full sample through process() with the trigger input held at the
// given voltage.
static void step(MidiKitModule* m, float trigVoltage, int64_t frame) {
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(trigVoltage);
	m->process(Test::makeProcessArgs(frame));
}

static void patchTrigger(MidiKitModule* m) {
	m->inputs[MidiKitModule::INPUT_TRIG].channels = 1;
}

TEST_CASE("process() runs the engine only on divider ticks", "[MidiKit]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");
	RecordingEngine eng(m);
	m->host.getActiveEngine() = &eng;

	// processDivider is set to a division of 8. dsp::ClockDivider increments
	// before comparing, so it fires on every 8th call — call indices 7, 15, 23
	// — not on the first one.
	for (int64_t f = 0; f < 7; f++) {
		step(m, 0.f, f);
	}
	REQUIRE(eng.processCalls == 0);

	step(m, 0.f, 7);
	REQUIRE(eng.processCalls == 1);

	for (int64_t f = 8; f < 24; f++) {
		step(m, 0.f, f);
	}
	REQUIRE(eng.processCalls == 3);

	Test::destroyModule(m);
}

TEST_CASE("process() drains the engine out-queue on a divider tick", "[MidiKit]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");
	RecordingEngine eng(m);
	m->host.getActiveEngine() = &eng;

	// Three messages scheduled for tick 1; all must be pulled in a single
	// divider tick, not one per call.
	eng.pending = {1, 1, 1};

	// Nothing is drained until the divider actually fires on the 8th call.
	for (int64_t f = 0; f < 7; f++) {
		step(m, 0.f, f);
	}
	REQUIRE(eng.pending.size() == 3);
	REQUIRE(m->midiOutput.tickQueue[0].size() == 0);

	step(m, 0.f, 7);

	REQUIRE(eng.pending.empty());
	REQUIRE(m->midiOutput.tickQueue[0].size() == 3);

	Test::destroyModule(m);
}

TEST_CASE("process() consumes the tick before the engine schedules on it", "[MidiKit]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");
	RecordingEngine eng(m);
	m->host.getActiveEngine() = &eng;
	patchTrigger(m);
	// The module only processes triggers on enabled channels — as the script's
	// trig.enableIn(1) would do.
	m->enableTrigger(0, 0);

	// Prime the SchmittTrigger LOW and advance to one call short of a divider
	// tick, without letting the engine emit anything.
	for (int64_t f = 0; f < 7; f++) {
		step(m, 0.f, f);
	}
	REQUIRE(eng.processCalls == 0);

	// A trigger and a divider tick coincide on this sample: processTick() runs
	// first and consumes tick 1, then the engine emits a message scheduled for
	// tick 1 — a tick already gone. This is the ordering that makes a stale
	// entry reachable at all, and it is why processTick() must use ">=".
	eng.pending = {1};
	step(m, 10.f, 7);
	REQUIRE(eng.processCalls == 1);

	REQUIRE(m->inputTriggerTick[0] == 1);
	REQUIRE(eng.tickAtEmit.size() == 1);
	REQUIRE(eng.tickAtEmit[0] == 1);       // emitted after the tick was consumed
	REQUIRE(m->midiOutput.tickQueue[0].size() == 1);
	REQUIRE(m->midiOutput.tickQueue[0].top().tick == 1);

	// The next trigger drains it rather than stranding it behind the counter.
	// The entry sits at tick 1 while the counter moves to 2, so only ">=" can
	// pop it — "==" strands it here permanently.
	step(m, 0.f, 8);
	REQUIRE(m->midiOutput.tickQueue[0].size() == 1);   // falling edge: no tick
	step(m, 10.f, 9);

	REQUIRE(m->inputTriggerTick[0] == 2);
	REQUIRE(eng.tickAtEmit.size() == 1);           // engine emitted only once
	REQUIRE(m->midiOutput.tickQueue[0].size() == 0);

	Test::destroyModule(m);
}

TEST_CASE("process() handles triggers arriving between divider ticks", "[MidiKit]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");
	RecordingEngine eng(m);
	m->host.getActiveEngine() = &eng;
	patchTrigger(m);
	// The module only processes triggers on enabled channels — as the script's
	// trig.enableIn(1) would do.
	m->enableTrigger(0, 0);

	// Schedule for two ticks ahead on the first divider tick (call index 7).
	eng.pending = {2};
	for (int64_t f = 0; f < 8; f++) {
		step(m, 0.f, f);
	}
	REQUIRE(m->midiOutput.tickQueue[0].size() == 1);

	// Triggers are handled every sample, independent of the divider. These land
	// between divider boundaries and must not send the tick-2 message early.
	step(m, 10.f, 8);
	REQUIRE(m->inputTriggerTick[0] == 1);
	REQUIRE(m->midiOutput.tickQueue[0].size() == 1);

	step(m, 0.f, 9);
	step(m, 10.f, 10);
	REQUIRE(m->inputTriggerTick[0] == 2);
	REQUIRE(m->midiOutput.tickQueue[0].size() == 0);

	Test::destroyModule(m);
}

TEST_CASE("process() sends frame-scheduled messages on divider ticks only", "[MidiKit]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");

	// With no default engine, load a script so process() runs past the
	// `if (!activeEngine) return;` guard.
	m->loadScript(QUICKJS_SCRIPT);

	// ticks == 0 with a set frame routes to frameQueue rather than tickQueue.
	midi::Message msg = makeCc();
	msg.frame = 9;
	m->midiOutput.send(msg, 0, 0);
	REQUIRE(m->midiOutput.frameQueue.size() == 1);

	// Divider ticks land on call indices 7 and 15, and processFrame() is only
	// reached inside that branch. The frame-9 message is therefore still queued
	// at frame 14, four samples after it came due — this is the one-divider-
	// period latency the review notes under #3.
	for (int64_t f = 0; f <= 14; f++) {
		step(m, 0.f, f);
	}
	REQUIRE(m->midiOutput.frameQueue.size() == 1);

	// The next divider tick drains it.
	step(m, 0.f, 15);
	REQUIRE(m->midiOutput.frameQueue.size() == 0);

	Test::destroyModule(m);
}

TEST_CASE("process() orders trigger, inbound, and outbound effects in one call", "[MidiKit]") {
	// Refactor plan §7.1: one divider-tick process() call that has a trigger
	// edge, a pending inbound message, and a queued outbound message, asserting
	// the observable order of effects. This is the ordering the extractions
	// most risk breaking, and nothing else pins it down:
	//   1. the trigger edge is consumed first (tick queued to the engine);
	//   2. the inbound message is decoded and queued to the engine;
	//   3. the engine runs, so it can see the just-queued inbound and emit.
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");
	RecordingEngine eng(m);
	m->host.getActiveEngine() = &eng;
	patchTrigger(m);
	m->enableTrigger(0, 0);

	// Prime the SchmittTrigger LOW and advance to one call short of a divider
	// tick (the divider fires on call index 7).
	for (int64_t f = 0; f < 7; f++) {
		step(m, 0.f, f);
	}
	REQUIRE(eng.events.empty());

	// One call with all three at once: a rising edge on channel 0, an inbound
	// CC queued for this sample (frame -1 processes immediately), and an
	// outbound message the engine emits during its pump, scheduled for the tick
	// just consumed.
	midi::Message in = makeCc();
	m->midiInput.onMessage(in);
	eng.pending = {1};
	step(m, 10.f, 7);

	// The engine was pumped exactly once, on this divider tick.
	REQUIRE(eng.processCalls == 1);

	// The observable order of effects within that single call:
	// trigger edge → inbound decode → engine pump.
	REQUIRE(eng.events.size() == 3);
	REQUIRE(eng.events[0] == RecordingEngine::TICK);
	REQUIRE(eng.events[1] == RecordingEngine::MESSAGE);
	REQUIRE(eng.events[2] == RecordingEngine::PROCESS);

	// The side effects that order produces: the edge was consumed, the inbound
	// reached the engine, and the engine's outbound landed after the tick was
	// consumed (so it stays queued until the next trigger).
	REQUIRE(m->inputTriggerTick[0] == 1);
	REQUIRE(eng.received.size() == 1);
	REQUIRE(eng.received[0].type == StoermelderPackOne::MessageEx::Type::CC);
	REQUIRE(eng.tickAtEmit.size() == 1);
	REQUIRE(eng.tickAtEmit[0] == 1);
	REQUIRE(m->midiOutput.tickQueue[0].size() == 1);
	REQUIRE(m->midiOutput.tickQueue[0].top().tick == 1);

	Test::destroyModule(m);
}

TEST_CASE("Trigger input drains tick-scheduled messages via process()", "[MidiKit]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");

	// With no default engine, load a script so process() runs past the
	// `if (!activeEngine) return;` guard. The trigger is enabled directly here
	// (as the script's trig.enableIn(1) would do) so the module drains ticks.
	m->loadScript(QUICKJS_SCRIPT);
	m->enableTrigger(0, 0);

	m->inputs[MidiKitModule::INPUT_TRIG].channels = 1;
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f);
	m->process(Test::makeProcessArgs(0));

	// Schedule for tick 2 but also queue a stale tick-1 entry ahead of it, as
	// happens when a script schedules for a tick the counter already consumed.
	// The stale entry sorts to the head, so with "==" it blocks both forever.
	midi::Message msg = makeCc();
	m->midiOutput.send(msg, 0, 2);
	m->midiOutput.tickQueue[0].push(MidiOutput::TickSchedule{msg, 0});
	REQUIRE(m->midiOutput.tickQueue[0].size() == 2);

	int64_t frame = 1;
	for (int pulse = 0; pulse < 3; pulse++) {
		m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f);
		m->process(Test::makeProcessArgs(frame++));
		m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f);
		m->process(Test::makeProcessArgs(frame++));
	}

	REQUIRE(m->inputTriggerTick[0] == 3);
	REQUIRE(m->midiOutput.tickQueue[0].size() == 0);

	Test::destroyModule(m);
}

TEST_CASE("sendAfterTrigger on one channel is only drained by that channel's clock", "[MidiKit]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");

	// With no default engine, load a script so process() runs past the
	// `if (!activeEngine) return;` guard. Enable both trigger channels (as the
	// script's trig.enableIn(1, 1)/trig.enableIn(1, 2) would do) so the module
	// processes ticks on each.
	m->loadScript(QUICKJS_SCRIPT);
	m->enableTrigger(0, 0);
	m->enableTrigger(0, 1);

	m->inputs[MidiKitModule::INPUT_TRIG].channels = 2;

	// Schedule a message against channel 2's clock at tick 2.
	midi::Message msg = makeCc();
	m->midiOutput.send(msg, 1, 2);   // channel index 1 = script channel 2

	// Prime both SchmittTriggers LOW.
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f, 0);
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f, 1);
	m->process(Test::makeProcessArgs(0));

	// Two pulses on channel 1 must NOT drain channel 2's queue.
	for (int pulse = 0; pulse < 2; pulse++) {
		m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f, 0);
		m->process(Test::makeProcessArgs(pulse * 2 + 1));
		m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f, 0);
		m->process(Test::makeProcessArgs(pulse * 2 + 2));
	}
	REQUIRE(m->inputTriggerTick[0] == 2);
	REQUIRE(m->inputTriggerTick[1] == 0);
	REQUIRE(m->midiOutput.tickQueue[1].size() == 1);   // still queued

	// Two pulses on channel 2 drain it (tick 2 reached).
	for (int pulse = 0; pulse < 2; pulse++) {
		m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f, 1);
		m->process(Test::makeProcessArgs(pulse * 2 + 10));
		m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f, 1);
		m->process(Test::makeProcessArgs(pulse * 2 + 11));
	}
	REQUIRE(m->inputTriggerTick[1] == 2);
	REQUIRE(m->midiOutput.tickQueue[1].size() == 0);   // drained

	Test::destroyModule(m);
}

// --- Logging (midiLogMessages) ------------------------------------------------

TEST_CASE("Log queue preserves FIFO order", "[MidiKit][Log]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");
	drainLogEntries(m);  // discard construction-time entries

	for (int i = 0; i < 10; i++) {
		m->log.midiLogMessages.try_push(std::make_tuple(LOG_FORMAT::TEXT, 0.f, std::string("line") + std::to_string(i)));
	}

	auto entries = drainLogEntries(m);
	REQUIRE(entries.size() == 10);
	for (int i = 0; i < 10; i++) {
		REQUIRE(std::get<1>(entries[i]) == "line" + std::to_string(i));
	}

	Test::destroyModule(m);
}


TEST_CASE("Log accepts entries from multiple producers", "[MidiKit][Log]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");
	drainLogEntries(m);  // discard construction-time entries

	// Producer A: the module's handler writeLog (the worker-thread path).
	m->writeLog("from-engine", true);
	// Producer B: a direct push (the loadScript/onReset path).
	m->log.midiLogMessages.try_push(std::make_tuple(LOG_FORMAT::TEXT, 0.f, std::string("from-direct")));
	// Producer A again.
	m->writeLog("from-engine-2", false);

	auto entries = drainLogEntries(m);
	REQUIRE(entries.size() == 3);
	REQUIRE(std::get<1>(entries[0]) == "from-engine");
	REQUIRE(std::get<1>(entries[1]) == "from-direct");
	REQUIRE(std::get<1>(entries[2]) == "from-engine-2");
	// writeLog(useTimestamp=true) -> TIMESTAMP, writeLog(useTimestamp=false) -> TEXT.
	REQUIRE(std::get<0>(entries[0]) == LOG_FORMAT::TIMESTAMP);
	REQUIRE(std::get<0>(entries[1]) == LOG_FORMAT::TEXT);
	REQUIRE(std::get<0>(entries[2]) == LOG_FORMAT::TEXT);

	Test::destroyModule(m);
}


TEST_CASE("LoadScript emits a RESET log entry", "[MidiKit][Log]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");
	drainLogEntries(m);  // discard construction-time entries

	m->loadScript(QUICKJS_SCRIPT);

	auto entries = drainLogEntries(m);
	REQUIRE(!entries.empty());
	// loadScript() pushes the RESET marker before any script output.
	REQUIRE(std::get<0>(entries[0]) == LOG_FORMAT::RESET);

	Test::destroyModule(m);
}


TEST_CASE("Log queue drops entries when full", "[MidiKit][Log]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");
	drainLogEntries(m);  // discard construction-time entries

	// The queue holds exactly 512 entries; pushing more must drop (try_push
	// returns false) rather than block.
	int pushed = 0;
	for (int i = 0; i < 1000; i++) {
		if (m->log.midiLogMessages.try_push(std::make_tuple(LOG_FORMAT::TEXT, 0.f, std::string("x")))) {
			pushed++;
		}
	}
	REQUIRE(pushed == 512);

	// Every accepted entry is still drained out (no loss of accepted entries).
	auto entries = drainLogEntries(m);
	REQUIRE(entries.size() == 512);

	Test::destroyModule(m);
}


// ─── rack.registerContextMenu(): module lifecycle ───────────────────────────
// The module-level consequences of script-registered context menus: clearing
// the script or switching engines drops the previous engine's registered
// items. These behaviours are engine-independent: each case drives both
// engines through the public module API (loadScript/clearScript/
// getContextMenus) and never touches engine internals.

static const char* QJS_BOOL = R"(/**
 * @engine QuickJs@v1
 */
let v = false;
rack.registerContextMenu({
	type: "boolean",
	label: "Velocity to CC",
	onGetValue: function() {
		return v;
	},
	onChange: function(checked) {
		v = checked;
		rack.log("onChange: " + (checked ? "true" : "false"));
	}
});
)";

static const char* QJS_OPTIONS = R"(/**
 * @engine QuickJs@v1
 */
let v = 1;
rack.registerContextMenu({
	type: "options",
	label: "Out mode",
	options: ["Internal", "External", "Both"],
	onGetValue: function() {
		return v;
	},
	onChange: function(selectedIndex, selectedLabel) {
		v = selectedIndex;
		rack.log("onChange: " + selectedIndex + " " + selectedLabel);
	}
});
)";

static const char* LUA_BOOL = R"(--[[
@engine minilua@v1
--]]
v = false
rack.registerContextMenu({
	type = "boolean",
	label = "Velocity to CC",
	onGetValue = function()
		return v
	end,
	onChange = function(checked)
		v = checked
		rack.log("onChange: " .. tostring(checked))
	end
})
)";

static const char* LUA_OPTIONS = R"(--[[
@engine minilua@v1
--]]
v = 1
rack.registerContextMenu({
	type = "options",
	label = "Out mode",
	options = {"Internal", "External", "Both"},
	onGetValue = function()
		return v
	end,
	onChange = function(selectedIndex, selectedLabel)
		v = selectedIndex
		rack.log("onChange: " .. selectedIndex .. " " .. selectedLabel)
	end
})
)";

// ─── rack.registerContextMenu(): widget integration ─────────────────────────
// appendContextMenu() inserts an async placeholder that builds the real menu
// items once the worker has evaluated onGetValue — driven by the
// placeholder's step(), which the tests call via buildScriptMenuItems() (the
// worker is inline under SyncTaskWorker). Clicking an item through
// MenuItem::doAction() fires the script callback. The widget behaviour is
// engine-independent: each case builds the menu for a fresh module+widget per
// engine script.

// Drive the async placeholder (ScriptContextMenuItems) that builds the
// script-registered items, then remove and delete the placeholder exactly as
// Rack's Menu::step() would once it has requested deletion. Only the
// placeholder is stepped: stepping the whole menu would run Menu::step()
// (which reads its parent's box) and MenuItem::step() (which needs APP->window
// for font metrics), neither of which the test harness provides. The
// placeholder is the only MenuEntry that is not a MenuItem, MenuLabel, or
// MenuSeparator. Freeing it matters: the built items are its siblings and must
// keep working after it is destroyed — their callbacks capture the module
// pointer, not the placeholder itself.
static void buildScriptMenuItems(rack::ui::Menu* menu) {
	rack::Widget* placeholder = nullptr;
	for (rack::Widget* child : menu->children) {
		if (!dynamic_cast<rack::ui::MenuEntry*>(child))
			continue;
		if (dynamic_cast<rack::ui::MenuItem*>(child))
			continue;
		if (dynamic_cast<rack::ui::MenuLabel*>(child))
			continue;
		if (dynamic_cast<rack::ui::MenuSeparator*>(child))
			continue;
		placeholder = child;
		break;
	}
	if (!placeholder) return;
	placeholder->step();
	// Delete exactly as Rack's Widget::step() would: removeChild() detaches the
	// placeholder from the menu (RemoveEvent + parent = NULL), then delete is
	// legal — Widget::~Widget() asserts the widget is orphaned.
	menu->removeChild(placeholder);
	delete placeholder;
}

TEST_CASE("Context menu: boolean item is built and click fires the callback", "[MidiKit][ContextMenu]") {
	for (const char* script : {QJS_BOOL, LUA_BOOL}) {
		MidiKitModule* m = createModule();
		m->model = modelMidiKit;
		m->loadScript(script);
		MidiKitWidget* mw = Test::createWidget<MidiKitWidget>(m);

		rack::ui::Menu* menu = new rack::ui::Menu;
		mw->appendContextMenu(menu);
		// The script items are built asynchronously: the worker evaluates
		// onGetValue (inline here via SyncTaskWorker) and the placeholder's
		// step() builds the real items. The helper also frees the placeholder
		// as Rack's Menu::step() would, so clicking below runs against the
		// freed placeholder (its callbacks must capture the module, not
		// `this`). The whole menu can't be stepped without a window.
		buildScriptMenuItems(menu);

		rack::ui::MenuItem* item = nullptr;
		for (rack::Widget* child : menu->children) {
			if (auto* mi = dynamic_cast<rack::ui::MenuItem*>(child)) {
				if (mi->text == "Velocity to CC") { item = mi; break; }
			}
		}
		REQUIRE(item != nullptr);
		// Unchecked → no checkmark.
		REQUIRE(item->rightText == "");

		item->doAction(true);
		std::string log = drainLog(m);
		REQUIRE(log.find("onChange: true") != std::string::npos);

		delete menu;
		Test::destroyWidget(mw);
		Test::destroyModule(m);
	}
}

TEST_CASE("Context menu: options submenu is built and click fires the callback", "[MidiKit][ContextMenu]") {
	for (const char* script : {QJS_OPTIONS, LUA_OPTIONS}) {
		MidiKitModule* m = createModule();
		m->model = modelMidiKit;
		m->loadScript(script);
		MidiKitWidget* mw = Test::createWidget<MidiKitWidget>(m);

		rack::ui::Menu* menu = new rack::ui::Menu;
		mw->appendContextMenu(menu);
		// See the boolean test case: step the async placeholder so it builds
		// the real items, then free the placeholder (Rack's Menu::step()
		// behaviour) so the submenu callbacks run without `this`.
		buildScriptMenuItems(menu);

		rack::ui::MenuItem* sub = nullptr;
		for (rack::Widget* child : menu->children) {
			if (auto* mi = dynamic_cast<rack::ui::MenuItem*>(child)) {
				if (mi->text == "Out mode") { sub = mi; break; }
			}
		}
		REQUIRE(sub != nullptr);

		// Building the child menu is what a hover/click on the submenu does.
		rack::ui::Menu* submenu = sub->createChildMenu();
		REQUIRE(submenu != nullptr);

		rack::ui::MenuItem* external = nullptr;
		rack::ui::MenuItem* both = nullptr;
		for (rack::Widget* child : submenu->children) {
			if (auto* mi = dynamic_cast<rack::ui::MenuItem*>(child)) {
				if (mi->text == "External") external = mi;
				if (mi->text == "Both") both = mi;
			}
		}
		REQUIRE(external != nullptr);
		REQUIRE(both != nullptr);
		// selected == 1 → "External" is the checked one, "Both" is not.
		REQUIRE(external->rightText == "✔");
		REQUIRE(both->rightText == "");

		both->doAction(true);
		std::string log = drainLog(m);
		REQUIRE(log.find("onChange: 2 Both") != std::string::npos);

		delete submenu;
		delete menu;
		Test::destroyWidget(mw);
		Test::destroyModule(m);
	}
}


// ─── Example-script submenus (appendExampleItems / hasExampleScripts) ───────
// appendExampleItems() and hasExampleScripts() scan a real directory on disk,
// so these tests build a throwaway tree under the system temp dir (see
// TempExampleDir) and point the menu builder at it. They assert on the menu
// structure — leaf items for matching scripts, nested submenus for subfolders,
// empty subfolders skipped, "None found" when nothing matches — and on the
// click-through: a leaf's action loads the script into the module via loadJs().

// Creates a unique, writable directory tree for one test case and removes it on
// destruction. Names come from a static counter, which keeps every concurrently
// live tree unique; a stale leftover from a crashed run is removed first, and
// createDirectories() is happy to recreate it.
struct TempExampleDir {
	std::string root;

	TempExampleDir() {
		root = rack::system::join(rack::system::getTempDirectory(), "MidiKit-example-test-" + std::to_string(++s_counter));
		rack::system::removeRecursively(root);  // clear stale leftovers
		REQUIRE(rack::system::createDirectories(root));
	}
	~TempExampleDir() {
		rack::system::removeRecursively(root);
	}

	// Full path of a root-relative path.
	std::string path(const std::string& rel) const {
		return rack::system::join(root, rel);
	}

	// Writes a file (creating parent dirs) and returns its full path.
	std::string write(const std::string& rel, const std::string& content = "") {
		std::string p = path(rel);
		rack::system::createDirectories(rack::system::getDirectory(p));
		std::ofstream f(p);
		REQUIRE(f.good());
		f << content;
		return p;
	}

	static int s_counter;
};
int TempExampleDir::s_counter = 0;

// Creates the module+widget pair used by the example-menu tests.
static void createExampleFixture(MidiKitModule** m, MidiKitWidget** mw) {
	*m = createModule();
	(*m)->model = modelMidiKit;
	*mw = Test::createWidget<MidiKitWidget>(*m);
}

// Finds a child MenuItem of `menu` by text; returns NULL when absent.
static rack::ui::MenuItem* findMenuItem(rack::ui::Menu* menu, const std::string& text) {
	for (rack::Widget* child : menu->children) {
		if (auto* mi = dynamic_cast<rack::ui::MenuItem*>(child)) {
			if (mi->text == text) return mi;
		}
	}
	return nullptr;
}

// Returns whether `menu` has a MenuLabel with the given text.
static bool hasMenuLabel(rack::ui::Menu* menu, const std::string& text) {
	for (rack::Widget* child : menu->children) {
		if (auto* label = dynamic_cast<rack::ui::MenuLabel*>(child)) {
			if (label->text == text) return true;
		}
	}
	return false;
}

TEST_CASE("hasExampleScripts detects scripts recursively", "[MidiKit][Examples]") {
	MidiKitModule* m;
	MidiKitWidget* mw;
	createExampleFixture(&m, &mw);

	TempExampleDir d;
	d.write("A.js");
	d.write("notes.md");
	d.write("sub/B.js");
	d.write("sub/deep/C.js");
	d.write("empty/deep/placeholder.txt");

	REQUIRE(mw->hasExampleScripts(d.root, ".js"));
	REQUIRE(!mw->hasExampleScripts(d.root, ".lua"));           // no .lua anywhere
	REQUIRE(mw->hasExampleScripts(d.path("sub"), ".js"));      // one level down
	REQUIRE(mw->hasExampleScripts(d.path("sub/deep"), ".js")); // nested
	REQUIRE(!mw->hasExampleScripts(d.path("empty"), ".js"));   // only .txt
	REQUIRE(!mw->hasExampleScripts(d.path("missing"), ".js")); // no such dir

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}

TEST_CASE("appendExampleItems builds nested submenus and skips empty folders", "[MidiKit][Examples]") {
	MidiKitModule* m;
	MidiKitWidget* mw;
	createExampleFixture(&m, &mw);

	TempExampleDir d;
	d.write("Alpha.js");
	d.write("Beta.md");               // wrong extension → ignored
	d.write("sub/SubOne.js");
	d.write("sub/SubTwo.js");
	d.write("sub/deep/DeepOne.js");
	d.write("empty/placeholder.txt"); // no .js in this subtree → skipped
	d.write("other/Other.lua");       // .lua only → skipped for a .js listing

	rack::ui::Menu* menu = new rack::ui::Menu;
	mw->appendExampleItems(menu, d.root, ".js");

	// Top level: subfolders come first (sorted), then files (sorted).
	// Folders without any matching script do not appear at all.
	rack::ui::MenuItem* sub = findMenuItem(menu, "sub");
	REQUIRE(sub != nullptr);
	REQUIRE(sub->rightText == "▸");                  // submenu arrow

	rack::ui::MenuItem* alpha = findMenuItem(menu, "Alpha");
	REQUIRE(alpha != nullptr);
	REQUIRE(alpha->rightText == "");                 // leaf: no submenu arrow
	REQUIRE(findMenuItem(menu, "Beta") == nullptr);  // wrong ext ignored
	REQUIRE(findMenuItem(menu, "empty") == nullptr); // no .js inside
	REQUIRE(findMenuItem(menu, "other") == nullptr); // no .js inside

	// Verify ordering: subfolder(s) appear before file(s).
	int subIdx = 0, alphaIdx = 0, idx = 0;
	for (auto* child : menu->children) {
		if (child == sub) subIdx = idx;
		if (child == alpha) alphaIdx = idx;
		idx++;
	}
	REQUIRE(subIdx < alphaIdx);

	// Open sub/: SubOne and SubTwo are leaves, deep/ is another submenu.
	rack::ui::Menu* subMenu = sub->createChildMenu();
	REQUIRE(subMenu != nullptr);
	REQUIRE(findMenuItem(subMenu, "SubOne") != nullptr);
	REQUIRE(findMenuItem(subMenu, "SubTwo") != nullptr);
	rack::ui::MenuItem* deep = findMenuItem(subMenu, "deep");
	REQUIRE(deep != nullptr);

	// Open deep/: only DeepOne, no "None found" (a non-empty match).
	rack::ui::Menu* deepMenu = deep->createChildMenu();
	REQUIRE(findMenuItem(deepMenu, "DeepOne") != nullptr);
	REQUIRE(deepMenu->children.size() == 1);
	REQUIRE(!hasMenuLabel(deepMenu, "None found"));

	delete deepMenu;
	delete subMenu;
	delete menu;
	Test::destroyWidget(mw);
	Test::destroyModule(m);
}

TEST_CASE("appendExampleItems leaf click loads the script", "[MidiKit][Examples]") {
	MidiKitModule* m;
	MidiKitWidget* mw;
	createExampleFixture(&m, &mw);

	static const std::string CONTENT =
		"/**\n"
		" * @engine QuickJs@v1\n"
		" */\n"
		"rack.log(\"loaded from submenu\");\n";

	TempExampleDir d;
	std::string path = d.write("Alpha.js", CONTENT);

	rack::ui::Menu* menu = new rack::ui::Menu;
	mw->appendExampleItems(menu, d.root, ".js");

	rack::ui::MenuItem* alpha = findMenuItem(menu, "Alpha");
	REQUIRE(alpha != nullptr);

	// Clicking a leaf is what Rack does on mouse release: it runs the item's
	// action, which records the file path and loads it into the module.
	alpha->doAction(true);

	REQUIRE(mw->filename == path);
	REQUIRE(m->host.script == CONTENT);
	REQUIRE(m->host.isQuickJsEngine());

	delete menu;
	Test::destroyWidget(mw);
	Test::destroyModule(m);
}

TEST_CASE("appendExampleItems shows 'None found' when nothing matches", "[MidiKit][Examples]") {
	MidiKitModule* m;
	MidiKitWidget* mw;
	createExampleFixture(&m, &mw);

	TempExampleDir d;
	d.write("readme.md"); // no scripts of the requested engine

	// A directory with only non-matching files → label only, no items.
	rack::ui::Menu* menu = new rack::ui::Menu;
	mw->appendExampleItems(menu, d.root, ".js");
	REQUIRE(hasMenuLabel(menu, "None found"));
	for (rack::Widget* child : menu->children) {
		REQUIRE(dynamic_cast<rack::ui::MenuItem*>(child) == nullptr);
	}
	delete menu;

	// A directory that does not exist at all → same behaviour.
	rack::ui::Menu* menu2 = new rack::ui::Menu;
	mw->appendExampleItems(menu2, d.path("missing"), ".js");
	REQUIRE(hasMenuLabel(menu2, "None found"));
	delete menu2;

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}

// ─── MidiProcessor integration ───────────────────────────────────────────────
// The module decodes the incoming stream through MidiProcessor before it
// reaches the engine, so NRPN/RPN/14-bit CC assembly happens once on the audio
// thread rather than in every script.

// Feeds raw MIDI into the module's real input queue and runs process() enough
// times to clear the divider (8), so the queue is actually pumped.
static void feedMidi(MidiKitModule* m, std::vector<midi::Message> msgs, int64_t& frame) {
	for (auto& msg : msgs) m->midiInput.onMessage(msg);
	for (int i = 0; i < 9; i++) m->process(Test::makeProcessArgs(frame++));
}

static midi::Message cc(uint8_t ch, uint8_t num, uint8_t value) {
	return Test::makeMidiMessage(0xb, ch, num, value);
}

TEST_CASE("Incoming MIDI is decoded before reaching the engine", "[MidiKit][MidiProcessor]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");
	RecordingEngine eng(m);
	m->host.getActiveEngine() = &eng;
	int64_t frame = 1;

	SECTION("A plain CC arrives as an undecoded message") {
		feedMidi(m, { cc(0, 7, 64) }, frame);

		REQUIRE(eng.received.size() == 1);
		REQUIRE(eng.received[0].type == StoermelderPackOne::MessageEx::Type::CC);
		REQUIRE(eng.received[0].msg.getNote() == 7);
		REQUIRE(eng.received[0].msg.getValue() == 64);
		// Not part of an extended message, so no decode result rides along.
		REQUIRE(eng.received[0].isComponent == false);
		REQUIRE(eng.received[0].paramNumber == -1);
		REQUIRE(eng.received[0].extraValue == -1);
	}

	SECTION("A note is passed through untouched") {
		feedMidi(m, { Test::makeMidiMessage(0x9, 0, 60, 100) }, frame);

		REQUIRE(eng.received.size() == 1);
		REQUIRE(eng.received[0].type == StoermelderPackOne::MessageEx::Type::NOTE_ON);
		REQUIRE(eng.received[0].msg.getNote() == 60);
	}

	SECTION("NRPN components are flagged, and the assembled event is not queued") {
		// Four CCs make one NRPN. Every one of them still reaches the engine as a
		// raw CC -- this integration must not change what onMessage sees -- but
		// each is marked as belonging to an extended message.
		feedMidi(m, { cc(0, 99, 4), cc(0, 98, 5), cc(0, 6, 20), cc(0, 38, 2) }, frame);

		// Exactly four: the assembled NRPN events are dropped rather than queued,
		// so onMessage still fires once per real MIDI message.
		REQUIRE(eng.received.size() == 4);
		for (auto& q : eng.received) {
			CATCH_INFO("cc=" << int(q.msg.getNote()));
			REQUIRE(q.type == StoermelderPackOne::MessageEx::Type::CC);
			REQUIRE(q.isComponent == true);
		}
	}

	SECTION("A 14-bit CC pair is flagged from the second message on") {
		// The decoder cannot know a pair is coming, so the first MSB escapes
		// unflagged; the LSB completes a tracked pair and is flagged.
		feedMidi(m, { cc(0, 5, 3), cc(0, 32 + 5, 10) }, frame);

		REQUIRE(eng.received.size() == 2);
		REQUIRE(eng.received[0].isComponent == false);
		REQUIRE(eng.received[1].isComponent == true);
	}

	Test::destroyModule(m);
}

TEST_CASE("Decoder state is cleared on reset and script load", "[MidiKit][MidiProcessor]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");
	RecordingEngine eng(m);
	m->host.getActiveEngine() = &eng;
	int64_t frame = 1;

	// Arm an NRPN parameter, leaving the decoder mid-sequence.
	feedMidi(m, { cc(0, 99, 4), cc(0, 98, 5) }, frame);
	REQUIRE(m->midiProcessor.ccNrpnParam[0] == (4 * 128 + 5));

	SECTION("onReset() drops it") {
		m->onReset();
		REQUIRE(m->midiProcessor.ccNrpnParam[0] == -1);
	}

	SECTION("loadScript() drops it, so a new script inherits no half-read state") {
		// Detach the recorder first: loadScript() closes the outgoing engine, and
		// the real Lua engine it installs must be the one teardown sees. Leaving a
		// stack-allocated RecordingEngine as activeEngine across the switch calls
		// virtuals on it during module destruction, after it has gone out of scope.
		m->host.getActiveEngine() = nullptr;
		m->loadScript(LUA_SCRIPT);
		REQUIRE(m->midiProcessor.ccNrpnParam[0] == -1);
	}

	Test::destroyModule(m);
}

TEST_CASE("The processor decodes the module's own queue, not a private one", "[MidiKit][MidiProcessor]") {
	// The queue is injected rather than owned, so midiInput keeps its widget
	// binding and JSON. Pins that wiring: no separate queue was allocated, and
	// getInput() resolves to the module's member.
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");

	REQUIRE(m->midiProcessor.ownedInput == nullptr);
	REQUIRE(&m->midiProcessor.getInput() == &m->midiInput);

	Test::destroyModule(m);
}
