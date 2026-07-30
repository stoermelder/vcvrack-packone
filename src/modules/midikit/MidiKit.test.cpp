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
