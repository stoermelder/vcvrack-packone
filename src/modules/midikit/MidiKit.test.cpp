#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "MidiKit.cpp"

using namespace StoermelderPackOne::MidiKit;
using namespace StoermelderPackOne::MidiScript;

SYNC_MODEL(modelMidiKit, "MidiKit");
Test::TestContext<> testContext;

// Minimal Elk script header (body can be empty — the Elk engine still loads it)
static constexpr const char* ELK_SCRIPT =
	"/**\n"
	" * @engine Elk\n"
	" */\n";

// Minimal Lua script (synchronously loaded, no body needed)
static constexpr const char* LUA_SCRIPT =
	"--[[\n"
	"@engine Lua\n"
	"--]]\n";


TEST_CASE("Construction and initialization", "[MidiKit]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");

	REQUIRE(m != nullptr);
	REQUIRE(m->NUM_PARAMS == 4);
	REQUIRE(m->NUM_INPUTS == 5);   // 4 voltage + 1 trigger
	REQUIRE(m->NUM_OUTPUTS == 1);  // trigger out
	REQUIRE(m->NUM_LIGHTS == 0);
	REQUIRE(m->script == "");
	REQUIRE(m->sample == 0);
	REQUIRE(m->inputTriggerTick == 0);

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

	for (int i = 0; i < 20; i++) {
		REQUIRE_NOTHROW(m->process(Test::makeProcessArgs(i + 1)));
	}

	REQUIRE(m->sample == 20);

	Test::destroyModule(m);
}

TEST_CASE("Default engine is Elk", "[MidiKit]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");

	REQUIRE(m->activeEngine == static_cast<MidiScriptEngine*>(&m->se));

	Test::destroyModule(m);
}

TEST_CASE("@engine Lua header selects Lua engine", "[MidiKit]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");

	m->loadScript(LUA_SCRIPT);

	REQUIRE(m->activeEngine == static_cast<MidiScriptEngine*>(&m->seLua));

	Test::destroyModule(m);
}

TEST_CASE("Elk header keeps Elk engine active", "[MidiKit]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");

	// First switch to Lua, then switch back via an Elk-tagged script
	m->loadScript(LUA_SCRIPT);
	REQUIRE(m->activeEngine == static_cast<MidiScriptEngine*>(&m->seLua));

	m->loadScript(ELK_SCRIPT);
	REQUIRE(m->activeEngine == static_cast<MidiScriptEngine*>(&m->se));

	Test::destroyModule(m);
}

TEST_CASE("clearScript resets to empty and restores Elk engine", "[MidiKit]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");

	m->loadScript(LUA_SCRIPT);
	REQUIRE(m->activeEngine == static_cast<MidiScriptEngine*>(&m->seLua));

	m->clearScript();

	REQUIRE(m->script == "");
	REQUIRE(m->activeEngine == static_cast<MidiScriptEngine*>(&m->se));

	Test::destroyModule(m);
}

TEST_CASE("Trigger input increments inputTriggerTick", "[MidiKit]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");

	m->inputs[MidiKitModule::INPUT_TRIG].channels = 1;

	// Prime the SchmittTrigger to LOW state before the first rising edge
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f);
	m->process(Test::makeProcessArgs(0));

	// Rising edge → tick increments
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f);
	m->process(Test::makeProcessArgs(1));
	REQUIRE(m->inputTriggerTick == 1);

	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f);
	m->process(Test::makeProcessArgs(2));
	REQUIRE(m->inputTriggerTick == 1);  // no change on falling edge

	// Second pulse
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f);
	m->process(Test::makeProcessArgs(3));
	REQUIRE(m->inputTriggerTick == 2);

	Test::destroyModule(m);
}

TEST_CASE("JSON round-trip preserves panelTheme and script", "[MidiKit]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");

	m->panelTheme = 2;
	m->loadScript(LUA_SCRIPT);

	json_t* j = m->dataToJson();

	m->panelTheme = 0;
	m->clearScript();
	REQUIRE(m->script == "");

	m->dataFromJson(j);
	json_decref(j);

	REQUIRE(m->panelTheme == 2);
	REQUIRE(m->script == LUA_SCRIPT);
	REQUIRE(m->activeEngine == static_cast<MidiScriptEngine*>(&m->seLua));

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

	out.send(msg, 5);
	REQUIRE(out.tickQueue.size() == 1);

	out.processTick(4);
	REQUIRE(out.tickQueue.size() == 1);  // not due yet

	out.processTick(5);
	REQUIRE(out.tickQueue.size() == 0);
}

TEST_CASE("processTick sends a message whose tick has already passed", "[MidiKit]") {
	MidiOutput out;
	midi::Message msg = makeCc();

	// process() calls processTick() before draining the engine out-queue, so a
	// script can schedule for a tick the counter has already consumed.
	out.send(msg, 5);
	REQUIRE(out.tickQueue.size() == 1);

	out.processTick(6);
	REQUIRE(out.tickQueue.size() == 0);  // with "==" this stayed queued forever
}

TEST_CASE("processTick drains every due message in one call", "[MidiKit]") {
	MidiOutput out;
	midi::Message msg = makeCc();

	out.send(msg, 3);
	out.send(msg, 5);
	out.send(msg, 7);
	REQUIRE(out.tickQueue.size() == 3);

	out.processTick(5);
	REQUIRE(out.tickQueue.size() == 1);        // 3 and 5 sent, 7 still pending
	REQUIRE(out.tickQueue.top().tick == 7);

	out.processTick(7);
	REQUIRE(out.tickQueue.size() == 0);
}

TEST_CASE("processTick: a stale entry does not block later messages", "[MidiKit]") {
	MidiOutput out;
	midi::Message msg = makeCc();

	// tickQueue is ordered smallest-tick-first, so the stale entry sits at the
	// head. With "==" it was never popped and blocked everything behind it.
	out.send(msg, 2);   // stale — this tick is already in the past
	out.send(msg, 7);   // legitimately scheduled for later
	REQUIRE(out.tickQueue.size() == 2);

	out.processTick(7);
	REQUIRE(out.tickQueue.size() == 0);  // both drained, not stuck at the head
}

TEST_CASE("processTick leaves not-yet-due messages queued", "[MidiKit]") {
	MidiOutput out;
	midi::Message msg = makeCc();

	out.send(msg, 10);

	for (uint64_t t = 0; t < 10; t++) {
		out.processTick(t);
		REQUIRE(out.tickQueue.size() == 1);
	}

	out.processTick(10);
	REQUIRE(out.tickQueue.size() == 0);
}

TEST_CASE("processFrame sends a message on its exact frame", "[MidiKit]") {
	MidiOutput out;
	midi::Message msg = makeCc();
	msg.frame = 5;

	out.send(msg, 0);
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

	out.send(msg, 0);
	REQUIRE(out.frameQueue.size() == 1);

	out.processFrame(6);
	REQUIRE(out.frameQueue.size() == 0);
}

TEST_CASE("processFrame drains every due message in one call", "[MidiKit]") {
	MidiOutput out;
	midi::Message msg = makeCc();

	msg.frame = 3;
	out.send(msg, 0);
	msg.frame = 5;
	out.send(msg, 0);
	msg.frame = 7;
	out.send(msg, 0);
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

	out.send(msg, 0);
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
	// Messages to hand back from processOutMessage(), as (ticks) — one per
	// processOutMessage() call until exhausted.
	std::vector<int> pending;
	// inputTriggerTick observed at the moment the engine emitted each message.
	std::vector<uint64_t> tickAtEmit;
	MidiKitModule* module = nullptr;

	void process() override {
		processCalls++;
	}

	bool processOutMessage(int& midiPort, midi::Message& msg, int& ticks) override {
		if (pending.empty()) return false;
		midiPort = 0;
		msg = makeCc();
		ticks = pending.front();
		pending.erase(pending.begin());
		if (module) tickAtEmit.push_back(module->inputTriggerTick);
		return true;
	}

	// Unused by these tests — stubbed only to satisfy the interface.
	void runAsync(std::function<void()> task) override { }
	void loadScript(const char* script) override { }
	void processInMessage(int midiPort, midi::Message& msg) override { }
	void writeLog(std::string, bool useTimestamp = true) override { }
	void writeOverlay(std::string s1, std::string s2, std::string s3) override { }
	void enableInput(int i) override { }
	float getInputVoltage(int i, uint8_t ch) override { return 0.f; }
	float getTrigVoltage(int i, uint8_t ch) override { return 0.f; }
	uint64_t getTrigTicks(int i) override { return 0; }
	void enableParam(int i) override { }
	float getParamValue(int i) override { return 0.f; }
	void setTrig(int i, uint8_t ch, float duration = 1e-3f) override { }
	void setTrigVoltage(int i, uint8_t ch, float voltage) override { }
	std::string getInputName(int i) override { return ""; }
	std::string getParamName(int i) override { return ""; }
	std::string getParamFormatValue(int i) override { return ""; }
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
	RecordingEngine eng;
	m->activeEngine = &eng;

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
	RecordingEngine eng;
	eng.module = m;
	m->activeEngine = &eng;

	// Three messages scheduled for tick 1; all must be pulled in a single
	// divider tick, not one per call.
	eng.pending = {1, 1, 1};

	// Nothing is drained until the divider actually fires on the 8th call.
	for (int64_t f = 0; f < 7; f++) {
		step(m, 0.f, f);
	}
	REQUIRE(eng.pending.size() == 3);
	REQUIRE(m->midiOutput.tickQueue.size() == 0);

	step(m, 0.f, 7);

	REQUIRE(eng.pending.empty());
	REQUIRE(m->midiOutput.tickQueue.size() == 3);

	Test::destroyModule(m);
}

TEST_CASE("process() consumes the tick before the engine schedules on it", "[MidiKit]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");
	RecordingEngine eng;
	eng.module = m;
	m->activeEngine = &eng;
	patchTrigger(m);

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

	REQUIRE(m->inputTriggerTick == 1);
	REQUIRE(eng.tickAtEmit.size() == 1);
	REQUIRE(eng.tickAtEmit[0] == 1);       // emitted after the tick was consumed
	REQUIRE(m->midiOutput.tickQueue.size() == 1);
	REQUIRE(m->midiOutput.tickQueue.top().tick == 1);

	// The next trigger drains it rather than stranding it behind the counter.
	// The entry sits at tick 1 while the counter moves to 2, so only ">=" can
	// pop it — "==" strands it here permanently.
	step(m, 0.f, 8);
	REQUIRE(m->midiOutput.tickQueue.size() == 1);   // falling edge: no tick
	step(m, 10.f, 9);

	REQUIRE(m->inputTriggerTick == 2);
	REQUIRE(eng.tickAtEmit.size() == 1);           // engine emitted only once
	REQUIRE(m->midiOutput.tickQueue.size() == 0);

	Test::destroyModule(m);
}

TEST_CASE("process() handles triggers arriving between divider ticks", "[MidiKit]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");
	RecordingEngine eng;
	eng.module = m;
	m->activeEngine = &eng;
	patchTrigger(m);

	// Schedule for two ticks ahead on the first divider tick (call index 7).
	eng.pending = {2};
	for (int64_t f = 0; f < 8; f++) {
		step(m, 0.f, f);
	}
	REQUIRE(m->midiOutput.tickQueue.size() == 1);

	// Triggers are handled every sample, independent of the divider. These land
	// between divider boundaries and must not send the tick-2 message early.
	step(m, 10.f, 8);
	REQUIRE(m->inputTriggerTick == 1);
	REQUIRE(m->midiOutput.tickQueue.size() == 1);

	step(m, 0.f, 9);
	step(m, 10.f, 10);
	REQUIRE(m->inputTriggerTick == 2);
	REQUIRE(m->midiOutput.tickQueue.size() == 0);

	Test::destroyModule(m);
}

TEST_CASE("process() sends frame-scheduled messages on divider ticks only", "[MidiKit]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");

	// ticks == 0 with a set frame routes to frameQueue rather than tickQueue.
	midi::Message msg = makeCc();
	msg.frame = 9;
	m->midiOutput.send(msg, 0);
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

TEST_CASE("Trigger input drains tick-scheduled messages via process()", "[MidiKit]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");

	m->inputs[MidiKitModule::INPUT_TRIG].channels = 1;
	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f);
	m->process(Test::makeProcessArgs(0));

	// Schedule for tick 2 but also queue a stale tick-1 entry ahead of it, as
	// happens when a script schedules for a tick the counter already consumed.
	// The stale entry sorts to the head, so with "==" it blocks both forever.
	midi::Message msg = makeCc();
	m->midiOutput.send(msg, 2);
	m->midiOutput.tickQueue.push(MidiOutput::TickSchedule{msg, 0});
	REQUIRE(m->midiOutput.tickQueue.size() == 2);

	int64_t frame = 1;
	for (int pulse = 0; pulse < 3; pulse++) {
		m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f);
		m->process(Test::makeProcessArgs(frame++));
		m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f);
		m->process(Test::makeProcessArgs(frame++));
	}

	REQUIRE(m->inputTriggerTick == 3);
	REQUIRE(m->midiOutput.tickQueue.size() == 0);

	Test::destroyModule(m);
}
