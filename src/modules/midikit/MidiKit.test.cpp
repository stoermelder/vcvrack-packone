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

	REQUIRE(m->activeEngine == nullptr);

	Test::destroyModule(m);
}

TEST_CASE("@engine minilua@v1 header selects Lua engine", "[MidiKit]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");

	m->loadScript(LUA_SCRIPT);

	REQUIRE(m->activeEngine == static_cast<MidiScriptEngine*>(&m->seLua));

	Test::destroyModule(m);
}

TEST_CASE("QuickJs header keeps QuickJs engine active", "[MidiKit]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");

	// First switch to Lua, then switch back via a QuickJs-tagged script
	m->loadScript(LUA_SCRIPT);
	REQUIRE(m->activeEngine == static_cast<MidiScriptEngine*>(&m->seLua));

	m->loadScript(QUICKJS_SCRIPT);
	REQUIRE(m->activeEngine == static_cast<MidiScriptEngine*>(&m->seQuickJs));

	Test::destroyModule(m);
}

TEST_CASE("clearScript resets to empty and restores no engine", "[MidiKit]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");

	m->loadScript(LUA_SCRIPT);
	REQUIRE(m->activeEngine == static_cast<MidiScriptEngine*>(&m->seLua));

	m->clearScript();

	REQUIRE(m->script == "");
	REQUIRE(m->activeEngine == nullptr);

	Test::destroyModule(m);
}

TEST_CASE("Trigger input increments inputTriggerTick", "[MidiKit]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");

	// With no default engine, load a script so process() runs past the
	// `if (!activeEngine) return;` guard.
	m->loadScript(QUICKJS_SCRIPT);

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
	// Messages to emit via handler->sendMidi() on the next process() call, as
	// (ticks) — one per pending entry, all drained in one call.
	std::vector<int> pending;
	// inputTriggerTick observed at the moment the engine emitted each message.
	std::vector<uint64_t> tickAtEmit;
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
		for (int ticks : pending) {
			midi::Message msg = makeCc();
			handler->sendMidi(0, &msg, 1, ticks);
			tickAtEmit.push_back(module->inputTriggerTick);
		}
		pending.clear();
	}

	// Unused by these tests — stubbed only to satisfy the interface.
	void loadScriptOnWorker(const char* script, const std::string& persistedConfigJson) override { }
	bool testScript(const std::string& script) override { return false; }
	void closeStateOnWorker() override { }
	bool captureConfig(std::string& out) override { return false; }
	void processInMessage(int midiPort, midi::Message& msg) override { }
	void processInTick(int trigPort) override { }
	void dispatchMidiMessage(int midiPort, midi::Message& msg) override { }
	void dispatchTrigger(int trigPort) override { }
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
	RecordingEngine eng(m);
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
	RecordingEngine eng(m);
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
	RecordingEngine eng(m);
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

	// With no default engine, load a script so process() runs past the
	// `if (!activeEngine) return;` guard.
	m->loadScript(QUICKJS_SCRIPT);

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

	// With no default engine, load a script so process() runs past the
	// `if (!activeEngine) return;` guard.
	m->loadScript(QUICKJS_SCRIPT);

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

// --- Logging (midiLogMessages) ------------------------------------------------

TEST_CASE("Log queue preserves FIFO order", "[MidiKit][Log]") {
	MidiKitModule* m = Test::createModule<MidiKitModule>("MidiKit");
	drainLogEntries(m);  // discard construction-time entries

	for (int i = 0; i < 10; i++) {
		m->midiLogMessages.try_push(std::make_tuple(LOG_FORMAT::TEXT, 0.f, std::string("line") + std::to_string(i)));
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
	m->midiLogMessages.try_push(std::make_tuple(LOG_FORMAT::TEXT, 0.f, std::string("from-direct")));
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
		if (m->midiLogMessages.try_push(std::make_tuple(LOG_FORMAT::TEXT, 0.f, std::string("x")))) {
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
	REQUIRE(m->script == CONTENT);
	REQUIRE(m->activeEngine == static_cast<MidiScriptEngine*>(&m->seQuickJs));

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