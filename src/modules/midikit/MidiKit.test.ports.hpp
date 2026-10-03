#include "MidiKit.test.hpp"

// Module variants: MidiKitModuleBase/MidiKitWidgetBase instantiated with other
// port counts than the original MidiKit. The rest of the suite only exercises
// the 1-in/1-out config, so multi-port routing and the MidiKitMicro variant are
// covered here.

// 3 CV inputs, 1 param, 2 trigger in/out, 2 MIDI in/out.
struct MultiConfig {
	static constexpr int cvInputs = 3;
	static constexpr int trigInputs = 2;
	static constexpr int trigOutputs = 2;
	static constexpr int params = 1;
	static constexpr int midiInputs = 2;
	static constexpr int midiOutputs = 2;
};
using MultiModule = MidiKitModuleBase<MultiConfig>;

// A widget for MultiConfig without a registered model: two of each MIDI port,
// no controls. Enough to build the base's context menu.
struct MultiWidget : MidiKitWidgetBase<MultiConfig> {
	MultiWidget(MultiModule* module) : MidiKitWidgetBase<MultiConfig>(module, "MidiKit") {
		addMidiInputDisplay(0, Rect(Vec(0.f, 36.4f), Vec(180.f, 44.6f)));
		addMidiInputDisplay(1, Rect(Vec(0.f, 81.f), Vec(180.f, 44.6f)));
		addLogDisplay(Rect(Vec(0.f, 126.f), Vec(180.f, 60.f)));
		addMidiOutputDisplay(0, Rect(Vec(0.f, 190.f), Vec(180.f, 44.6f)));
		addMidiOutputDisplay(1, Rect(Vec(0.f, 235.f), Vec(180.f, 44.6f)));
	}
};

static MultiModule* createMultiModule() {
	MultiModule* m = new MultiModule(std::make_shared<StoermelderPackOne::SyncTaskWorker>());
	m->id = rand();
	Module::SampleRateChangeEvent e{44100.f, 1.f / 44100.f};
	m->onSampleRateChange(e);
	return m;
}

struct MultiScaffold : Test::ModuleScaffold<MultiModule> {
	MultiScaffold() : Test::ModuleScaffold<MultiModule>([]() { return createMultiModule(); }) {}
};

static midi::Message ccMsg(uint8_t ch, uint8_t num, uint8_t value) {
	return Test::makeMidiMessage(0xb, ch, num, value);
}

// Runs enough process() calls to pass the module's process divider once.
static void pump(MultiModule* m, int64_t& frame) {
	for (int i = 0; i < 9; i++) m->process(Test::makeProcessArgs(frame++));
}

static const char* QUICKJS_EMPTY =
	"/**\n"
	" * @engine QuickJs@v1\n"
	" */\n";

static const char* LUA_EMPTY =
	"--[[\n"
	"@engine minilua@v1\n"
	"--]]\n";

static std::string probes(MultiModule* m) {
	std::string all, out;
	ScriptLog::Entry t;
	while (m->log.tryPop(t)) all += std::get<2>(t) + "\n";
	return all;
}

TEST_CASE("Variant: sizes follow the config", "[MidiKit][Variant]") {
	MultiScaffold mods;
	MultiModule* m = mods.create();

	REQUIRE(m->NUM_PARAMS == 1);
	REQUIRE(m->NUM_INPUTS == 3 + 2);
	REQUIRE(m->NUM_OUTPUTS == 2);
	REQUIRE(MultiModule::INPUT_TRIG == 3);
	REQUIRE(MultiModule::OUTPUT_TRIG == 0);

	StoermelderPackOne::MidiScript::MidiScriptEngine* se = &m->host.seLua;
	REQUIRE(se->inputCount == 3);
	REQUIRE(se->inputTrigCount == 2);
	REQUIRE(se->outputTrigCount == 2);
	REQUIRE(se->paramCount == 1);
	REQUIRE(se->midiInputCount == 2);
	REQUIRE(se->midiOutputCount == 2);
}

TEST_CASE("Variant: micro config sizes", "[MidiKit][Variant]") {
	Test::ModuleScaffold<MidiKitMicroModule> mods([]() {
		MidiKitMicroModule* m = new MidiKitMicroModule(std::make_shared<StoermelderPackOne::SyncTaskWorker>());
		m->id = rand();
		return m;
	});
	MidiKitMicroModule* m = mods.create();

	REQUIRE(m->NUM_PARAMS == 2);
	REQUIRE(m->NUM_INPUTS == 4);    // 2 CV + 2 trigger
	REQUIRE(m->NUM_OUTPUTS == 2);
	REQUIRE(m->host.seLua.inputCount == 2);
	REQUIRE(m->host.seLua.paramCount == 2);
}

// ── MIDI output routing ─────────────────────────────────────────────────────

// Tick-scheduled messages park in the target output's tickQueue, which makes
// the routing observable without a MIDI driver.
TEST_CASE("Variant: sendMidi routes to the addressed MIDI output", "[MidiKit][Variant]") {
	MultiScaffold mods;
	MultiModule* m = mods.create();
	m->loadScript(QUICKJS_EMPTY);
	m->enableMidiOut(2);
	int64_t frame = 1;

	midi::Message msg = ccMsg(0, 7, 100);
	REQUIRE(m->sendMidi(1, &msg, 1, 0, 5));
	pump(m, frame);
	REQUIRE(m->midiOuts.ports[0].tickQueue[0].size() == 0);
	REQUIRE(m->midiOuts.ports[1].tickQueue[0].size() == 1);

	REQUIRE(m->sendMidi(0, &msg, 1, 0, 5));
	pump(m, frame);
	REQUIRE(m->midiOuts.ports[0].tickQueue[0].size() == 1);
	REQUIRE(m->midiOuts.ports[1].tickQueue[0].size() == 1);
}

TEST_CASE("Variant: sendMidi drops an out-of-range MIDI port", "[MidiKit][Variant]") {
	MultiScaffold mods;
	MultiModule* m = mods.create();
	m->loadScript(QUICKJS_EMPTY);

	midi::Message msg = ccMsg(0, 7, 100);
	REQUIRE_FALSE(m->sendMidi(2, &msg, 1, 0, 5));
	REQUIRE_FALSE(m->sendMidi(-1, &msg, 1, 0, 5));
	REQUIRE(m->midiOuts.queue.empty());
}

TEST_CASE("Variant: out.flush sends to the queued port without crashing", "[MidiKit][Variant]") {
	MultiScaffold mods;
	MultiModule* m = mods.create();
	m->loadScript(QUICKJS_EMPTY);

	m->enableMidiOut(2);
	midi::Message msg = ccMsg(0, 7, 100);
	REQUIRE(m->sendMidi(1, &msg, 1, 0, 0));
	m->flushMidiOut();
	REQUIRE(m->midiOuts.queue.empty());
}

// ── MIDI input routing ──────────────────────────────────────────────────────

static const char* JS_PORT_PROBE = R"(/**
 * @engine QuickJs@v1
 */
midi.enablePorts(2);
midi.onMessage = function(midiPort, msg) {
    rack.log("P:" + midiPort);
};
)";

static const char* LUA_PORT_PROBE = R"(--[[
@engine minilua@v1
--]]
midi.enablePorts(2)
midi.onMessage = function(midiPort, msg)
    rack.log("P:" .. midiPort)
end
)";

TEST_CASE("Variant: incoming MIDI reaches the script with its 1-based port", "[MidiKit][Variant]") {
	for (const char* script : {JS_PORT_PROBE, LUA_PORT_PROBE}) {
		MultiScaffold mods;
		MultiModule* m = mods.create();
		m->loadScript(script);
		probes(m);   // drop load-time entries
		int64_t frame = 1;

		m->midiIns.ports[0].processor.getInput().onMessage(ccMsg(0, 1, 10));
		m->midiIns.ports[1].processor.getInput().onMessage(ccMsg(0, 2, 20));
		m->midiIns.ports[1].processor.getInput().onMessage(ccMsg(0, 3, 30));
		pump(m, frame);

		std::string log = probes(m);
		CATCH_INFO(script);
		CATCH_INFO(log);
		size_t p1 = 0, p2 = 0;
		for (size_t pos = log.find("P:1"); pos != std::string::npos; pos = log.find("P:1", pos + 1)) p1++;
		for (size_t pos = log.find("P:2"); pos != std::string::npos; pos = log.find("P:2", pos + 1)) p2++;
		REQUIRE(p1 == 1);
		REQUIRE(p2 == 2);
	}
}

TEST_CASE("Variant: loadScript() drops half-received NRPN state on every MIDI input", "[MidiKit][Variant]") {
	MultiScaffold mods;
	MultiModule* m = mods.create();
	m->loadScript(QUICKJS_EMPTY);
	int64_t frame = 1;

	// Arm an NRPN select (4/5) on each input. The enable is checked per input, so
	// switch it on for all of them (the module has 2 inputs here)..
	m->enableMidiIn(MultiModule::MIDI_INPUTS);
	for (int i = 0; i < MultiModule::MIDI_INPUTS; i++) m->enableNrpnIn(i, 0, 3);
	for (int i = 0; i < MultiModule::MIDI_INPUTS; i++) {
		m->midiIns.ports[i].processor.getInput().onMessage(ccMsg(0, 99, 4));
		m->midiIns.ports[i].processor.getInput().onMessage(ccMsg(0, 98, 5));
	}
	pump(m, frame);
	for (int i = 0; i < MultiModule::MIDI_INPUTS; i++) {
		CATCH_INFO("input " << i);
		REQUIRE(m->midiIns.ports[i].processor.ccNrpnParam[0] == 4 * 128 + 5);
	}

	// The reset is a request the audio thread carries out on its next sample.
	m->loadScript(QUICKJS_EMPTY);
	pump(m, frame);
	for (int i = 0; i < MultiModule::MIDI_INPUTS; i++) {
		CATCH_INFO("input " << i);
		REQUIRE(m->midiIns.ports[i].processor.ccNrpnParam[0] == -1);
	}
}

TEST_CASE("Variant: engine input queues drop on overflow instead of corrupting", "[MidiKit][Variant]") {
	for (const char* script : {QUICKJS_EMPTY, LUA_EMPTY}) {
		CATCH_INFO(script);
		MultiScaffold mods;
		MultiModule* m = mods.create();
		m->loadScript(script);
		probes(m);   // drop load-time entries
		StoermelderPackOne::MidiScript::MidiScriptEngine* engine = m->host.getActiveEngine();
		REQUIRE(engine != nullptr);

		// More ticks than the queue holds, none drained: a polyphonic clock with
		// every channel firing on several samples before the next drain.
		const size_t cap = engine->tickInQueue.capacity();
		for (size_t i = 0; i < cap + 40; i++) m->host.queueTick(0, uint8_t(i % PORT_MAX_CHANNELS), int64_t(i));
		REQUIRE(engine->tickInQueue.size() == cap);   // not size() > capacity

		// Same for MIDI messages.
		const size_t mcap = engine->midiInQueue.capacity();
		for (size_t i = 0; i < mcap + 10; i++) {
			StoermelderPackOne::MidiScript::QueuedMessage q;
			q.msg = ccMsg(0, 1, 10);
			m->host.queueMessage(0, q);
		}
		REQUIRE(engine->midiInQueue.size() == mcap);

		int64_t frame = 1;
		pump(m, frame);
		REQUIRE(engine->tickInQueue.empty());
		REQUIRE(engine->midiInQueue.empty());
	}
}

TEST_CASE("Variant: QuickJS memory usage is a snapshot published by the worker", "[MidiKit][Variant]") {
	MultiScaffold mods;
	MultiModule* m = mods.create();
	size_t used = 0, total = 0;
	REQUIRE_FALSE(m->host.seQuickJs.getMemoryUsage(used, total));

	m->loadScript(QUICKJS_EMPTY);
	REQUIRE(m->host.seQuickJs.getMemoryUsage(used, total));
	REQUIRE(used > 0);
	REQUIRE(total > used);

	// The load publishes a snapshot of the runtime's own accounting
	// (single-threaded here, so walking the runtime is safe).
	JSMemoryUsage s;
	JS_ComputeMemoryUsage(m->host.seQuickJs.rt, &s);
	REQUIRE(used == size_t(s.malloc_size));

	// Nothing is reported once the runtime is gone.
	m->clearScript();
	REQUIRE_FALSE(m->host.seQuickJs.getMemoryUsage(used, total));
}

// rack.onUnload() runs after the script was replaced or reset; the params and
// inputs it enabled must still read live values there, and be disabled after.
static const char* JS_UNLOAD_READS_PARAM = R"(/**
 * @engine QuickJs@v1
 */
param.enable(1);
rack.onUnload = function() {
    rack.log("U:" + number.toString(param.getValue(1)));
};
)";

static const char* LUA_UNLOAD_READS_PARAM = R"(--[[
@engine minilua@v1
--]]
param.enable(1)
rack.onUnload = function()
    rack.log("U:" .. number.toString(param.getValue(1)))
end
)";

TEST_CASE("Variant: onUnload() still reads the params its script enabled", "[MidiKit][Variant]") {
	using StoermelderPackOne::MidiScript::ScriptParamQuantity;
	for (const char* script : {JS_UNLOAD_READS_PARAM, LUA_UNLOAD_READS_PARAM}) {
		for (int viaReset = 0; viaReset < 2; viaReset++) {
			CATCH_INFO(script);
			std::string how = viaReset ? "onReset()" : "loadScript()";
			CATCH_INFO(how);
			MultiScaffold mods;
			MultiModule* m = mods.create();
			m->loadScript(script);
			probes(m);   // drop load-time entries
			auto* pq = reinterpret_cast<ScriptParamQuantity*>(m->paramQuantities[MultiModule::PARAM]);
			REQUIRE(pq->enabled);
			m->params[MultiModule::PARAM].setValue(0.5f);

			if (viaReset) m->onReset();
			else m->loadScript(QUICKJS_EMPTY);

			std::string log = probes(m);
			CATCH_INFO(log);
			REQUIRE(log.find("U:0.5") != std::string::npos);
			// Disabled again once the outgoing script is gone.
			REQUIRE_FALSE(pq->enabled);
		}
	}
}

TEST_CASE("Variant: framed messages are not stranded on a port that is no longer enabled", "[MidiKit][Variant]") {
	MultiScaffold mods;
	MultiModule* m = mods.create();
	m->enableMidiOut(2);
	int64_t frame = 1;

	midi::Message msg = ccMsg(0, 7, 100);
	msg.frame = 100;
	REQUIRE(m->sendMidi(1, &msg, 1, 0, 0));
	pump(m, frame);   // moves it from the module's queue into port 2's frame queue
	REQUIRE(m->midiOuts.ports[1].frameQueue.size() == 1);

	// The script that used port 2 is replaced by one that does not.
	m->midiOuts.resetEnables();
	REQUIRE_FALSE(m->midiOuts.isEnabled(1));

	// Once the frame is due it is sent anyway, not left for a later script.
	frame = 200;
	pump(m, frame);
	REQUIRE(m->midiOuts.ports[1].frameQueue.empty());
}

TEST_CASE("Variant: extended-CC enables are per MIDI input", "[MidiKit][Variant]") {
	MultiScaffold mods;
	MultiModule* m = mods.create();
	m->loadScript(QUICKJS_EMPTY);

	m->enableNrpnIn(1, 0, 3);
	REQUIRE(m->midiIns.isNrpnEnabled(3, false, 1));
	REQUIRE_FALSE(m->midiIns.isNrpnEnabled(3, false, 0));

	m->enableCc14bitIn(0, 7, 2);
	REQUIRE(m->midiIns.isCc14bitEnabled(2, 7, 0));
	REQUIRE_FALSE(m->midiIns.isCc14bitEnabled(2, 7, 1));

	// A port the module doesn't have is ignored.
	m->enableNrpnIn(2, 0, 5);
	m->enableNrpnIn(-1, 0, 5);
	REQUIRE_FALSE(m->midiIns.isNrpnEnabled(5, false, 0));
	REQUIRE_FALSE(m->midiIns.isNrpnEnabled(5, false, 1));

	// A reset drops the enables of every port.
	m->onReset();
	REQUIRE_FALSE(m->midiIns.isNrpnEnabled(3, false, 1));
	REQUIRE_FALSE(m->midiIns.isCc14bitEnabled(2, 7, 0));
}

// ── Port enabling (midi.enablePorts / midiOut.enablePorts) ──────────────────

static const char* JS_PORTS_DEFAULT = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(midiPort, msg) {
    rack.log("P:" + midiPort);
};
)";

static const char* LUA_PORTS_DEFAULT = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    rack.log("P:" .. midiPort)
end
)";

TEST_CASE("Variant: only MIDI input 1 is enabled until a script enables more", "[MidiKit][Variant]") {
	for (const char* script : {JS_PORTS_DEFAULT, LUA_PORTS_DEFAULT}) {
		MultiScaffold mods;
		MultiModule* m = mods.create();
		m->loadScript(script);
		probes(m);
		int64_t frame = 1;

		m->midiIns.ports[0].processor.getInput().onMessage(ccMsg(0, 1, 10));
		m->midiIns.ports[1].processor.getInput().onMessage(ccMsg(0, 2, 20));
		pump(m, frame);

		std::string log = probes(m);
		CATCH_INFO(script);
		CATCH_INFO(log);
		REQUIRE(log.find("P:1") != std::string::npos);
		REQUIRE(log.find("P:2") == std::string::npos);
	}
}

TEST_CASE("Variant: sendMidi drops a MIDI output that is not enabled, logging once", "[MidiKit][Variant]") {
	MultiScaffold mods;
	MultiModule* m = mods.create();
	m->loadScript(QUICKJS_EMPTY);
	probes(m);

	midi::Message msg = ccMsg(0, 7, 100);
	REQUIRE_FALSE(m->sendMidi(1, &msg, 1, 0, 5));
	REQUIRE_FALSE(m->sendMidi(1, &msg, 1, 0, 5));
	REQUIRE(m->midiOuts.queue.empty());
	std::string log = probes(m);
	CATCH_INFO(log);
	REQUIRE(log.find("MIDI output 2 is not enabled") != std::string::npos);
	REQUIRE(log.find("MIDI output 2 is not enabled") == log.rfind("MIDI output 2 is not enabled"));

	// Port 1 is always on.
	REQUIRE(m->sendMidi(0, &msg, 1, 0, 5));

	m->enableMidiOut(2);
	REQUIRE(m->sendMidi(1, &msg, 1, 0, 5));
}

static const char* JS_PORTS_OUT = R"(/**
 * @engine QuickJs@v1
 */
midiOut.enablePorts(2);
midi.onMessage = function(midiPort, msg) {
    midiOut.selectPort(2);
    midiOut.sendAfterTrigger(msg, 5);
};
)";

static const char* LUA_PORTS_OUT = R"(--[[
@engine minilua@v1
--]]
midiOut.enablePorts(2)
midi.onMessage = function(midiPort, msg)
    midiOut.selectPort(2)
    midiOut.sendAfterTrigger(msg, 5)
end
)";

TEST_CASE("Variant: midiOut.enablePorts lets a script send on that output", "[MidiKit][Variant]") {
	for (const char* script : {JS_PORTS_OUT, LUA_PORTS_OUT}) {
		MultiScaffold mods;
		MultiModule* m = mods.create();
		m->loadScript(script);
		int64_t frame = 1;

		CATCH_INFO(script);
		m->midiIns.ports[0].processor.getInput().onMessage(ccMsg(0, 1, 10));
		pump(m, frame);
		REQUIRE(m->midiOuts.ports[1].tickQueue[0].size() == 1);
		REQUIRE(m->midiOuts.ports[0].tickQueue[0].size() == 0);
	}
}

TEST_CASE("Variant: the enabled port count is a consecutive run that only grows", "[MidiKit][Variant]") {
	MultiScaffold mods;
	MultiModule* m = mods.create();
	REQUIRE(m->midiIns.enabledCount() == 1);
	REQUIRE(m->midiOuts.enabledCount() == 1);

	m->enableMidiIn(2);
	m->enableMidiIn(1);   // enabling never shrinks
	REQUIRE(m->midiIns.enabledCount() == 2);
	m->enableMidiOut(9);  // clamped to the ports the module has
	REQUIRE(m->midiOuts.enabledCount() == 2);

	m->onReset();
	REQUIRE(m->midiIns.enabledCount() == 1);
	REQUIRE(m->midiOuts.enabledCount() == 1);
}

TEST_CASE("Variant: an input beyond the count is drained, not queued for later", "[MidiKit][Variant]") {
	MultiScaffold mods;
	MultiModule* m = mods.create();
	m->loadScript(JS_PORTS_DEFAULT);
	probes(m);
	int64_t frame = 1;

	m->midiIns.ports[1].processor.getInput().onMessage(ccMsg(0, 2, 20));
	pump(m, frame);
	m->enableMidiIn(2);
	pump(m, frame);
	REQUIRE(probes(m).find("P:2") == std::string::npos);
}

TEST_CASE("Variant: a script's port enables are forgotten on reload and reset", "[MidiKit][Variant]") {
	MultiScaffold mods;
	MultiModule* m = mods.create();
	m->loadScript(JS_PORTS_OUT);
	midi::Message msg = ccMsg(0, 7, 100);
	REQUIRE(m->sendMidi(1, &msg, 1, 0, 5));

	m->loadScript(QUICKJS_EMPTY);
	REQUIRE_FALSE(m->sendMidi(1, &msg, 1, 0, 5));

	m->loadScript(JS_PORTS_OUT);
	REQUIRE(m->sendMidi(1, &msg, 1, 0, 5));
	m->onReset();
	REQUIRE_FALSE(m->sendMidi(1, &msg, 1, 0, 5));
}

TEST_CASE("Variant: enablePorts rejects an out-of-range port", "[MidiKit][Variant]") {
	for (const char* fn : {"midi.enablePorts", "midiOut.enablePorts"}) {
		for (bool js : {true, false}) {
			for (int bad : {0, 3}) {
				MultiScaffold mods;
				MultiModule* m = mods.create();
				std::string script = js
					? std::string("/**\n * @engine QuickJs@v1\n */\n") + fn + "(" + std::to_string(bad) + ");\n"
					: std::string("--[[\n@engine minilua@v1\n--]]\n") + fn + "(" + std::to_string(bad) + ")\n";
				m->loadScript(script);
				std::string log = probes(m);
				CATCH_INFO(script);
				CATCH_INFO(log);
				REQUIRE(log.find("rror") != std::string::npos);
			}
		}
	}
}

// ── UI queries run behind MIDI (MidiScriptEngine::runLowPriority) ───────────

static const char* JS_UI_QUERY = R"(/**
 * @engine QuickJs@v1
 */
input.enable(1);
input.getName = function(i) { rack.log("Q"); return "Name"; };
midi.onMessage = function(midiPort, msg) { rack.log("M"); };
)";

// Collects tasks instead of running them, so a test decides when the worker gets to them.
struct DeferredWorker : StoermelderPackOne::ITaskWorker {
	std::vector<std::function<void()>> tasks;
	std::atomic<bool> cancel{false};
	bool work(std::function<void()> t) override { tasks.push_back(std::move(t)); return true; }
	bool work(std::function<void()> t, Context*) override { tasks.push_back(std::move(t)); return true; }
	bool work(std::function<void(std::atomic<bool>&)> t) override { tasks.push_back([this, t]() { t(cancel); }); return true; }
	bool work(std::function<void(std::atomic<bool>&)> t, Context*) override { tasks.push_back([this, t]() { t(cancel); }); return true; }
	bool isWorkerThread() const override { return true; }
};

TEST_CASE("Variant: a UI query is answered by the next process() when nothing else is pending", "[MidiKit][Variant][UiQuery]") {
	MultiScaffold mods;
	MultiModule* m = mods.create();
	m->loadScript(JS_UI_QUERY);
	probes(m);
	int64_t frame = 1;

	auto* info = m->inputInfos[MultiModule::INPUT];
	REQUIRE(info->getName().empty());          // queued, not run yet
	REQUIRE(probes(m).find("Q") == std::string::npos);

	pump(m, frame);
	REQUIRE(probes(m).find("Q") != std::string::npos);
	REQUIRE(info->getName() == "Name");
}

TEST_CASE("Variant: a pending UI query runs after the MIDI message, never before", "[MidiKit][Variant][UiQuery]") {
	MultiScaffold mods;
	MultiModule* m = mods.create();
	m->loadScript(JS_UI_QUERY);
	probes(m);
	int64_t frame = 1;

	m->inputInfos[MultiModule::INPUT]->getName();   // queues the query first
	m->midiIns.ports[0].processor.getInput().onMessage(ccMsg(0, 1, 10));
	pump(m, frame);

	std::string log = probes(m);
	CATCH_INFO(log);
	REQUIRE(log.find("M") != std::string::npos);
	REQUIRE(log.find("Q") != std::string::npos);
	REQUIRE(log.find("M") < log.find("Q"));
}

TEST_CASE("Variant: UI queries are drained one per task and never flood the worker", "[MidiKit][Variant][UiQuery]") {
	MultiScaffold mods;
	MultiModule* m = mods.create();
	auto worker = std::make_shared<DeferredWorker>();
	StoermelderPackOne::MidiScript::MidiScriptEngine& e = m->host.seQuickJs;
	m->host.setDomain(std::make_shared<StoermelderPackOne::MidiScript::WorkerDomain>(worker));

	int ran = 0;
	for (int i = 0; i < 3; i++) REQUIRE(e.runLowPriority([&]() { ran++; }));

	// process() runs every few samples; however often it runs, one drain task waits.
	for (int i = 0; i < 5; i++) e.process();
	REQUIRE(worker->tasks.size() == 1);
	REQUIRE(ran == 0);

	// Each drain task runs a single query and lets the next one be scheduled.
	worker->tasks[0]();
	REQUIRE(ran == 1);
	e.process();
	REQUIRE(worker->tasks.size() == 2);
	worker->tasks[1]();
	e.process();
	worker->tasks[2]();
	REQUIRE(ran == 3);
	e.process();
	REQUIRE(worker->tasks.size() == 3);   // lane empty: nothing more scheduled

	// Back to a synchronous worker, so the teardown's unload runs inline instead
	// of waiting out its timeout on a worker that never runs it.
	m->host.setDomain(std::make_shared<StoermelderPackOne::MidiScript::WorkerDomain>(std::make_shared<StoermelderPackOne::SyncTaskWorker>()));
}

TEST_CASE("Variant: a full UI query lane drops instead of blocking", "[MidiKit][Variant][UiQuery]") {
	MultiScaffold mods;
	MultiModule* m = mods.create();
	StoermelderPackOne::MidiScript::MidiScriptEngine& e = m->host.seQuickJs;

	int accepted = 0;
	for (int i = 0; i < 100; i++) if (e.runLowPriority([]() {})) accepted++;
	REQUIRE(accepted > 0);
	REQUIRE(accepted < 100);
}

static const char* JS_MENU_QUERY = R"(/**
 * @engine QuickJs@v1
 */
rack.registerContextMenu({
	type: "boolean",
	label: "Item",
	onGetValue: function() { rack.log("Q"); return false; },
	onChange: function(v) {}
});
midi.onMessage = function(midiPort, msg) { rack.log("M"); };
)";

TEST_CASE("Variant: the context menu query is low priority", "[MidiKit][Variant][UiQuery]") {
	MultiScaffold mods;
	MultiModule* m = mods.create();
	m->loadScript(JS_MENU_QUERY);
	probes(m);
	int64_t frame = 1;
	auto* e = m->host.getActiveEngine();

	// Deferred until the engine pumps, and behind a pending MIDI message.
	int calls = 0;
	e->getContextMenus([&](const std::vector<StoermelderPackOne::MidiScript::ScriptMenuItem>& specs) {
		calls++;
		REQUIRE(specs.size() == 1);
	});
	REQUIRE(calls == 0);
	m->midiIns.ports[0].processor.getInput().onMessage(ccMsg(0, 1, 10));
	pump(m, frame);
	REQUIRE(calls == 1);
	std::string log = probes(m);
	CATCH_INFO(log);
	REQUIRE(log.find("M") < log.find("Q"));
}

TEST_CASE("Variant: a bypassed module still answers UI queries", "[MidiKit][Variant][UiQuery]") {
	MultiScaffold mods;
	MultiModule* m = mods.create();
	m->loadScript(JS_MENU_QUERY);

	int calls = 0;
	m->host.getActiveEngine()->getContextMenus([&](const std::vector<StoermelderPackOne::MidiScript::ScriptMenuItem>&) { calls++; });
	REQUIRE(calls == 0);
	m->processBypass(Test::makeProcessArgs(1));
	REQUIRE(calls == 1);
}

// Trigger ports

TEST_CASE("Variant: each trigger input has its own tick clock", "[MidiKit][Variant]") {
	MultiScaffold mods;
	MultiModule* m = mods.create();
	m->loadScript(QUICKJS_EMPTY);
	m->enableTrigger(0, 0);
	m->enableTrigger(1, 0);
	m->inputs[MultiModule::INPUT_TRIG + 0].channels = 1;
	m->inputs[MultiModule::INPUT_TRIG + 1].channels = 1;
	int64_t frame = 0;

	m->process(Test::makeProcessArgs(frame++));   // prime both LOW
	m->inputs[MultiModule::INPUT_TRIG + 1].setVoltage(10.f);
	m->process(Test::makeProcessArgs(frame++));
	REQUIRE(m->triggerIns.triggerTick[0][0] == 0);
	REQUIRE(m->triggerIns.triggerTick[1][0] == 1);
	REQUIRE(m->getTrigTicks(1, 0) == 1);
	REQUIRE(m->getTrigTicks(0, 0) == 0);
}

TEST_CASE("Variant: scheduled MIDI flushes on the clock of its own trigger input", "[MidiKit][Variant]") {
	MultiScaffold mods;
	MultiModule* m = mods.create();
	m->loadScript(QUICKJS_EMPTY);
	m->enableMidiOut(2);
	m->enableTrigger(0, 0);
	m->enableTrigger(1, 0);
	m->inputs[MultiModule::INPUT_TRIG + 0].channels = 1;
	m->inputs[MultiModule::INPUT_TRIG + 1].channels = 1;

	// The first process() carries out the script load's request to drop older
	// scheduled messages and primes the triggers LOW; schedule after it.
	int64_t frame = 0;
	m->process(Test::makeProcessArgs(frame++));

	// Due at tick 1: on output 1 against trigger input 2, on output 2 against
	// trigger input 1. tickQueue is flattened trigPort * 16 + channel.
	midi::Message msg = ccMsg(0, 7, 100);
	m->midiOuts.ports[0].send(msg, 0, 1, 1);
	m->midiOuts.ports[1].send(msg, 0, 1, 0);
	const int P2 = PORT_MAX_CHANNELS;

	// A rising edge on trigger input 2 flushes only what was scheduled on it.
	m->inputs[MultiModule::INPUT_TRIG + 1].setVoltage(10.f);
	m->process(Test::makeProcessArgs(frame++));
	REQUIRE(m->triggerIns.triggerTick[1][0] == 1);
	REQUIRE(m->midiOuts.ports[0].tickQueue[P2].size() == 0);
	REQUIRE(m->midiOuts.ports[1].tickQueue[0].size() == 1);

	// A rising edge on trigger input 1 flushes the rest.
	m->inputs[MultiModule::INPUT_TRIG + 0].setVoltage(10.f);
	m->process(Test::makeProcessArgs(frame++));
	REQUIRE(m->triggerIns.triggerTick[0][0] == 1);
	REQUIRE(m->midiOuts.ports[1].tickQueue[0].size() == 0);
}

TEST_CASE("Variant: trigger outputs are addressed by index", "[MidiKit][Variant]") {
	MultiScaffold mods;
	MultiModule* m = mods.create();

	// Applied by the audio thread.
	m->setTrigVoltage(1, 0, 4.f);
	m->process(Test::makeProcessArgs(1));
	REQUIRE(m->outputs[MultiModule::OUTPUT_TRIG + 1].getVoltage(0) == 4.f);
	REQUIRE(m->outputs[MultiModule::OUTPUT_TRIG + 0].getVoltage(0) == 0.f);
}

TEST_CASE("Variant: trigger outputs widen to the highest channel a script wrote", "[MidiKit][Variant]") {
	MultiScaffold mods;
	MultiModule* m = mods.create();
	int64_t frame = 1;
	auto step = [&]() { m->process(Test::makeProcessArgs(frame++)); };
	// Rack gives a freshly connected output 1 channel.
	for (int p = 0; p < 2; p++) m->outputs[MultiModule::OUTPUT_TRIG + p].channels = 1;
	step();
	REQUIRE(m->outputs[MultiModule::OUTPUT_TRIG + 0].getChannels() == 1);

	// Channel 4 of port 1 (0-based 3) via a held voltage, channel 2 of port 2 via a gate.
	m->setTrigVoltage(0, 3, 4.f);
	m->setTrig(1, 1, 1.f);
	step();
	REQUIRE(m->outputs[MultiModule::OUTPUT_TRIG + 0].getChannels() == 4);
	REQUIRE(m->outputs[MultiModule::OUTPUT_TRIG + 0].getVoltage(3) == 4.f);
	REQUIRE(m->outputs[MultiModule::OUTPUT_TRIG + 1].getChannels() == 2);
	REQUIRE(m->outputs[MultiModule::OUTPUT_TRIG + 1].getVoltage(1) == 10.f);

	// A script load starts mono again.
	m->loadScript("");
	step();
	REQUIRE(m->outputs[MultiModule::OUTPUT_TRIG + 0].getChannels() == 1);
	REQUIRE(m->outputs[MultiModule::OUTPUT_TRIG + 1].getChannels() == 1);
}

TEST_CASE("Variant: a script reload drops held trigger voltages and running pulses", "[MidiKit][Variant]") {
	for (const char* next : {"", QUICKJS_EMPTY}) {
		MultiScaffold mods;
		MultiModule* m = mods.create();
		int64_t frame = 1;
		auto step = [&]() { m->process(Test::makeProcessArgs(frame++)); };
		for (int p = 0; p < 2; p++) m->outputs[MultiModule::OUTPUT_TRIG + p].channels = 1;
		step();

		// Held high on output 1, a long pulse on output 2.
		m->setTrigVoltage(0, 0, 10.f);
		m->setTrig(1, 0, 10.f);
		step();
		REQUIRE(m->outputs[MultiModule::OUTPUT_TRIG + 0].getVoltage(0) == 10.f);
		REQUIRE(m->outputs[MultiModule::OUTPUT_TRIG + 1].getVoltage(0) == 10.f);

		// Both the "no engine matches" and the "engine loads" paths start low.
		m->loadScript(next);
		step();
		CATCH_INFO(std::string("next script: '") + next + "'");
		REQUIRE(m->outputs[MultiModule::OUTPUT_TRIG + 0].getVoltage(0) == 0.f);
		REQUIRE(m->outputs[MultiModule::OUTPUT_TRIG + 1].getVoltage(0) == 0.f);
	}
}

// Trigger ports through the script API
//
// The cases above drive the module directly. These load real scripts (both
// engines) and check that the 1-based `trigPort` argument of every trig.*
// call lands on the matching 0-based port.

// Tokens of the form `tag...` (up to the next whitespace) in the log, in order.
static std::vector<std::string> logTokens(MultiModule* m, const std::string& tag) {
	std::string all = probes(m);
	std::vector<std::string> out;
	for (size_t pos = all.find(tag); pos != std::string::npos; pos = all.find(tag, pos + 1)) {
		size_t end = all.find_first_of(" \r\n", pos);
		out.push_back(all.substr(pos, end == std::string::npos ? std::string::npos : end - pos));
	}
	return out;
}

// Both trigger inputs polyphonic (2 channels), both trigger outputs connected.
static void wireTrigPorts(MultiModule* m) {
	for (int p = 0; p < 2; p++) {
		m->inputs[MultiModule::INPUT_TRIG + p].channels = 2;
		m->outputs[MultiModule::OUTPUT_TRIG + p].channels = 4;
	}
}

// Sets one channel of one trigger input and lets the module process it.
static void driveTrig(MultiModule* m, int64_t& frame, int port, int ch, float v) {
	m->inputs[MultiModule::INPUT_TRIG + port].setVoltage(v, ch);
	pump(m, frame);
}

static const char* JS_TRIG_PORT_ROUTING = R"(/**
 * @engine QuickJs@v1
 */
trig.enableIn(1, 1);
trig.enableIn(2, 1);
trig.enableIn(2, 2);
trig.onTrigger = function(port, ch) {
    rack.log("T:" + port + ":" + ch + ":" + trig.getTicks(port, ch));
};
)";

static const char* LUA_TRIG_PORT_ROUTING = R"(--[[
@engine minilua@v1
--]]
trig.enableIn(1, 1)
trig.enableIn(2, 1)
trig.enableIn(2, 2)
trig.onTrigger = function(port, ch)
    rack.log("T:" .. port .. ":" .. ch .. ":" .. trig.getTicks(port, ch))
end
)";

TEST_CASE("Variant: trig.onTrigger reports the trigger port and channel that fired", "[MidiKit][Variant][TrigPorts]") {
	for (const char* script : {JS_TRIG_PORT_ROUTING, LUA_TRIG_PORT_ROUTING}) {
		CATCH_INFO(script);
		MultiScaffold mods;
		MultiModule* m = mods.create();
		wireTrigPorts(m);
		m->loadScript(script);
		probes(m);
		int64_t frame = 1;

		pump(m, frame);   // prime every enabled channel LOW
		driveTrig(m, frame, 1, 0, 10.f);   // port 2, channel 1
		driveTrig(m, frame, 0, 0, 10.f);   // port 1, channel 1
		driveTrig(m, frame, 1, 1, 10.f);   // port 2, channel 2
		driveTrig(m, frame, 1, 0, 0.f);
		driveTrig(m, frame, 1, 0, 10.f);   // port 2, channel 1 again

		// Each (port, channel) has its own tick counter.
		REQUIRE(logTokens(m, "T:") == std::vector<std::string>{"T:2:1:1", "T:1:1:1", "T:2:2:1", "T:2:1:2"});
	}
}

TEST_CASE("Variant: trig.enableIn arms only the addressed port", "[MidiKit][Variant][TrigPorts]") {
	const char* js = R"(/**
 * @engine QuickJs@v1
 */
trig.enableIn(2, 1);
trig.onTrigger = function(port, ch) { rack.log("T:" + port + ":" + ch); };
)";
	const char* lua = R"(--[[
@engine minilua@v1
--]]
trig.enableIn(2, 1)
trig.onTrigger = function(port, ch) rack.log("T:" .. port .. ":" .. ch) end
)";
	for (const char* script : {js, lua}) {
		CATCH_INFO(script);
		MultiScaffold mods;
		MultiModule* m = mods.create();
		wireTrigPorts(m);
		m->loadScript(script);
		probes(m);
		int64_t frame = 1;

		REQUIRE_FALSE(m->triggerIns.isEnabled(0, 0));
		REQUIRE(m->triggerIns.isEnabled(1, 0));
		REQUIRE_FALSE(m->triggerIns.isEnabled(1, 1));

		pump(m, frame);
		driveTrig(m, frame, 0, 0, 10.f);   // port 1: never enabled
		driveTrig(m, frame, 1, 1, 10.f);   // port 2, channel 2: never enabled
		REQUIRE(logTokens(m, "T:").empty());
		REQUIRE(m->getTrigTicks(0, 0) == 0);
		REQUIRE(m->getTrigTicks(1, 1) == 0);

		driveTrig(m, frame, 1, 0, 10.f);
		REQUIRE(logTokens(m, "T:") == std::vector<std::string>{"T:2:1"});
	}
}

TEST_CASE("Variant: trig.isHigh/isLow read the addressed trigger input", "[MidiKit][Variant][TrigPorts]") {
	const char* js = R"(/**
 * @engine QuickJs@v1
 */
trig.enableIn(2, 1);
function b(v) { return v ? 1 : 0; }
trig.onTrigger = function(port, ch) {
    rack.log("H:" + b(trig.isHigh(1)) + b(trig.isHigh(2)) + b(trig.isLow(1)) + b(trig.isLow(2)) + b(trig.isHigh(2, 2)));
};
)";
	const char* lua = R"(--[[
@engine minilua@v1
--]]
trig.enableIn(2, 1)
local function b(v) if v then return 1 end return 0 end
trig.onTrigger = function(port, ch)
    rack.log("H:" .. b(trig.isHigh(1)) .. b(trig.isHigh(2)) .. b(trig.isLow(1)) .. b(trig.isLow(2)) .. b(trig.isHigh(2, 2)))
end
)";
	for (const char* script : {js, lua}) {
		CATCH_INFO(script);
		MultiScaffold mods;
		MultiModule* m = mods.create();
		wireTrigPorts(m);
		m->loadScript(script);
		probes(m);
		int64_t frame = 1;

		pump(m, frame);
		driveTrig(m, frame, 1, 0, 10.f);
		// Port 1 low, port 2 (channel 1) high, port 2 channel 2 low.
		REQUIRE(logTokens(m, "H:") == std::vector<std::string>{"H:01100"});
	}
}

// One call per script, each on trigger output 2 (and channel 3 for the last).
struct TrigOutCase {
	const char* js;
	const char* lua;
	// Voltage expected at output 2 / output 1 on the tested channel.
	int channel;
	float out2;
	float out1;
};

TEST_CASE("Variant: trig output calls address the matching trigger output", "[MidiKit][Variant][TrigPorts]") {
	static const TrigOutCase cases[] = {
		{"trig.setHigh(2);",       "trig.setHigh(2)",       0, 10.f, 0.f},
		{"trig.setHigh(2, 3);",    "trig.setHigh(2, 3)",    2, 10.f, 0.f},
		{"trig.setTrigger(2);",    "trig.setTrigger(2)",    0, 10.f, 0.f},
		{"trig.setGate(2, 50);",   "trig.setGate(2, 50)",   0, 10.f, 0.f},
		{"trig.setGate(2, 4, 50);", "trig.setGate(2, 4, 50)", 3, 10.f, 0.f},
		{"trig.setHigh(1);",       "trig.setHigh(1)",       0, 0.f, 10.f},
	};
	for (const TrigOutCase& c : cases) {
		for (int engine = 0; engine < 2; engine++) {
			std::string script = engine == 0
				? std::string("/**\n * @engine QuickJs@v1\n */\n") + c.js + "\n"
				: std::string("--[[\n@engine minilua@v1\n--]]\n") + c.lua + "\n";
			CATCH_INFO(script);
			MultiScaffold mods;
			MultiModule* m = mods.create();
			wireTrigPorts(m);
			m->loadScript(script);
			int64_t frame = 1;
			pump(m, frame);

			REQUIRE(m->outputs[MultiModule::OUTPUT_TRIG + 1].getVoltage(c.channel) == c.out2);
			REQUIRE(m->outputs[MultiModule::OUTPUT_TRIG + 0].getVoltage(c.channel) == c.out1);
		}
	}
}

TEST_CASE("Variant: trig.setLow drops only the addressed trigger output", "[MidiKit][Variant][TrigPorts]") {
	for (int engine = 0; engine < 2; engine++) {
		std::string script = engine == 0
			? "/**\n * @engine QuickJs@v1\n */\ntrig.setHigh(1);\ntrig.setHigh(2);\ntrig.setLow(2);\n"
			: "--[[\n@engine minilua@v1\n--]]\ntrig.setHigh(1)\ntrig.setHigh(2)\ntrig.setLow(2)\n";
		CATCH_INFO(script);
		MultiScaffold mods;
		MultiModule* m = mods.create();
		wireTrigPorts(m);
		m->loadScript(script);
		int64_t frame = 1;
		pump(m, frame);

		REQUIRE(m->outputs[MultiModule::OUTPUT_TRIG + 0].getVoltage(0) == 10.f);
		REQUIRE(m->outputs[MultiModule::OUTPUT_TRIG + 1].getVoltage(0) == 0.f);
	}
}

TEST_CASE("Variant: a trigger pulse on output 2 leaves output 1 quiet and ends on time", "[MidiKit][Variant][TrigPorts]") {
	MultiScaffold mods;
	MultiModule* m = mods.create();
	wireTrigPorts(m);
	m->loadScript("/**\n * @engine QuickJs@v1\n */\ntrig.setGate(2, 10);\n");

	// 10 ms @ 44.1 kHz = 441 samples.
	int high1 = 0, high2 = 0;
	for (int i = 0; i < 1000; i++) {
		m->process(Test::makeProcessArgs(i));
		if (m->outputs[MultiModule::OUTPUT_TRIG + 0].getVoltage(0) > 5.f) high1++;
		if (m->outputs[MultiModule::OUTPUT_TRIG + 1].getVoltage(0) > 5.f) high2++;
	}
	REQUIRE(high1 == 0);
	REQUIRE(high2 > 400);
	REQUIRE(high2 < 480);
}

TEST_CASE("Variant: a trigger port beyond the module's ports is rejected by both engines", "[MidiKit][Variant][TrigPorts]") {
	// Port 3 does not exist on a 2-port module. Nothing must be armed, driven
	// or written out of bounds.
	const char* js[] = {
		"trig.enableIn(3);", "trig.getTicks(3);", "trig.isHigh(3);", "trig.isLow(3);",
		"trig.setGate(3, 10);", "trig.setHigh(3);", "trig.setLow(3);", "trig.setTrigger(3);",
		"trig.enableIn(0);", "trig.setHigh(0);",
	};
	const char* lua[] = {
		"trig.enableIn(3)", "trig.getTicks(3)", "trig.isHigh(3)", "trig.isLow(3)",
		"trig.setGate(3, 10)", "trig.setHigh(3)", "trig.setLow(3)", "trig.setTrigger(3)",
		"trig.enableIn(0)", "trig.setHigh(0)",
	};
	for (int engine = 0; engine < 2; engine++) {
		for (size_t i = 0; i < sizeof(js) / sizeof(js[0]); i++) {
			std::string script = engine == 0
				? std::string("/**\n * @engine QuickJs@v1\n */\n") + js[i] + "\n"
				: std::string("--[[\n@engine minilua@v1\n--]]\n") + lua[i] + "\n";
			CATCH_INFO(script);
			MultiScaffold mods;
			MultiModule* m = mods.create();
			wireTrigPorts(m);
			m->loadScript(script);
			int64_t frame = 1;
			pump(m, frame);

			for (int p = 0; p < 2; p++) {
				REQUIRE(m->triggerIns.enabledMask[p].load() == 0);
				REQUIRE(m->outputs[MultiModule::OUTPUT_TRIG + p].getVoltage(0) == 0.f);
			}
		}
	}
}

TEST_CASE("Variant: sendTipsy always uses trigger output 1", "[MidiKit][Variant][TrigPorts][Tipsy]") {
	for (int engine = 0; engine < 2; engine++) {
		std::string script = engine == 0
			? "/**\n * @engine QuickJs@v1\n */\ntrig.sendTipsy(\"Hello\");\n"
			: "--[[\n@engine minilua@v1\n--]]\ntrig.sendTipsy(\"Hello\")\n";
		CATCH_INFO(script);
		MultiScaffold mods;
		MultiModule* m = mods.create();
		wireTrigPorts(m);
		m->loadScript(script);

		int64_t frame = 1;
		int nonZero1 = 0, nonZero2 = 0;
		for (int i = 0; i < 400; i++) {
			m->process(Test::makeProcessArgs(frame++));
			if (m->outputs[MultiModule::OUTPUT_TRIG + 0].getVoltage(0) != 0.f) nonZero1++;
			if (m->outputs[MultiModule::OUTPUT_TRIG + 1].getVoltage(0) != 0.f) nonZero2++;
		}
		REQUIRE(nonZero1 > 0);
		REQUIRE(nonZero2 == 0);
	}
}

TEST_CASE("Variant: trig.enableTipsyIn claims trigger input 1 only", "[MidiKit][Variant][TrigPorts][Tipsy]") {
	const char* js = R"(/**
 * @engine QuickJs@v1
 */
trig.enableTipsyIn();
trig.enableIn(1, 1);
trig.enableIn(2, 1);
trig.onTrigger = function(port, ch) { rack.log("T:" + port + ":" + ch); };
trig.onTipsyMessage = function(data, mimeType) {};
)";
	const char* lua = R"(--[[
@engine minilua@v1
--]]
trig.enableTipsyIn()
trig.enableIn(1, 1)
trig.enableIn(2, 1)
trig.onTrigger = function(port, ch) rack.log("T:" .. port .. ":" .. ch) end
trig.onTipsyMessage = function(data, mimeType) end
)";
	for (const char* script : {js, lua}) {
		CATCH_INFO(script);
		MultiScaffold mods;
		MultiModule* m = mods.create();
		wireTrigPorts(m);
		m->loadScript(script);
		probes(m);
		REQUIRE(m->tipsyIn.claimed() == 0);
		int64_t frame = 1;

		pump(m, frame);
		driveTrig(m, frame, 0, 0, 10.f);   // claimed: not a trigger
		REQUIRE(logTokens(m, "T:").empty());
		REQUIRE(m->getTrigVoltage(0, 0) == 0.f);   // reads 0 while claimed

		driveTrig(m, frame, 1, 0, 10.f);   // port 2 is an ordinary trigger input
		REQUIRE(logTokens(m, "T:") == std::vector<std::string>{"T:2:1"});
		REQUIRE(m->getTrigVoltage(1, 0) == 10.f);
		REQUIRE(m->getTrigTicks(1, 0) == 1);
		REQUIRE(m->getTrigTicks(0, 0) == 0);
	}
}

TEST_CASE("Variant: a script reload forgets trigger enables and ticks on every port", "[MidiKit][Variant][TrigPorts]") {
	MultiScaffold mods;
	MultiModule* m = mods.create();
	wireTrigPorts(m);
	m->loadScript(JS_TRIG_PORT_ROUTING);
	int64_t frame = 1;
	pump(m, frame);
	driveTrig(m, frame, 0, 0, 10.f);
	driveTrig(m, frame, 1, 0, 10.f);
	REQUIRE(m->getTrigTicks(0, 0) == 1);
	REQUIRE(m->getTrigTicks(1, 0) == 1);

	m->loadScript(QUICKJS_EMPTY);
	for (int p = 0; p < 2; p++) {
		REQUIRE(m->triggerIns.enabledMask[p].load() == 0);
		REQUIRE(m->getTrigTicks(p, 0) == 0);
	}
}

TEST_CASE("Variant: the default MidiKit has two trigger inputs and two trigger outputs", "[MidiKit][Variant][TrigPorts]") {
	ModuleScaffold mods;
	MidiKitModule* m = mods.create();
	m->model = modelMidiKit;

	REQUIRE(m->NUM_INPUTS == 4 + 2);
	REQUIRE(m->NUM_OUTPUTS == 2);
	REQUIRE(MidiKitModule::INPUT_TRIG == 4);
	REQUIRE(MidiKitModule::OUTPUT_TRIG == 0);
	REQUIRE(m->host.seLua.inputTrigCount == 2);
	REQUIRE(m->host.seLua.outputTrigCount == 2);
	REQUIRE(m->host.seQuickJs.inputTrigCount == 2);
	REQUIRE(m->host.seQuickJs.outputTrigCount == 2);

	// Both trigger ports are named and reachable on the panel.
	REQUIRE(m->inputInfos[MidiKitModule::INPUT_TRIG + 0]->name == "Trigger 1");
	REQUIRE(m->inputInfos[MidiKitModule::INPUT_TRIG + 1]->name == "Trigger 2");
	REQUIRE(m->outputInfos[MidiKitModule::OUTPUT_TRIG + 0]->name == "Trigger 1");
	REQUIRE(m->outputInfos[MidiKitModule::OUTPUT_TRIG + 1]->name == "Trigger 2");

	MidiKitWidget* mw = Test::createWidget<MidiKitWidget>(m);
	REQUIRE(mw->getInputs().size() == 4 + 2);
	REQUIRE(mw->getOutputs().size() == 2);
	Test::destroyWidget(mw);
}

// sendAfterTrigger(msg, ticks, trigPort): the delay counts the ticks of that
// trigger input, so only that input's clock releases the message.
TEST_CASE("Variant: sendAfterTrigger counts the ticks of the trigger input it names", "[MidiKit][Variant][TrigPorts]") {
	const char* js = R"(/**
 * @engine QuickJs@v1
 */
trig.enableIn(1, 1);
trig.enableIn(2, 1);
midi.onMessage = function(port, msg) { midiOut.sendAfterTrigger(msg, 2, 2); };
)";
	const char* lua = R"(--[[
@engine minilua@v1
--]]
trig.enableIn(1, 1)
trig.enableIn(2, 1)
midi.onMessage = function(port, msg) midiOut.sendAfterTrigger(msg, 2, 2) end
)";
	const int P2 = PORT_MAX_CHANNELS;
	for (const char* script : {js, lua}) {
		CATCH_INFO(script);
		MultiScaffold mods;
		MultiModule* m = mods.create();
		wireTrigPorts(m);
		m->loadScript(script);
		int64_t frame = 1;
		pump(m, frame);   // prime both LOW

		m->midiIns.ports[0].processor.getInput().onMessage(ccMsg(0, 7, 100));
		pump(m, frame);
		// Parked against trigger input 2 (port index 1), not input 1.
		REQUIRE(m->midiOuts.ports[0].tickQueue[P2].size() == 1);
		REQUIRE(m->midiOuts.ports[0].tickQueue[0].size() == 0);

		// Any number of ticks on trigger input 1 releases nothing.
		for (int i = 0; i < 4; i++) {
			driveTrig(m, frame, 0, 0, 10.f);
			driveTrig(m, frame, 0, 0, 0.f);
		}
		REQUIRE(m->getTrigTicks(0, 0) == 4);
		REQUIRE(m->midiOuts.ports[0].tickQueue[P2].size() == 1);

		// Two ticks on trigger input 2 do.
		driveTrig(m, frame, 1, 0, 10.f);
		REQUIRE(m->midiOuts.ports[0].tickQueue[P2].size() == 1);
		driveTrig(m, frame, 1, 0, 0.f);
		driveTrig(m, frame, 1, 0, 10.f);
		REQUIRE(m->midiOuts.ports[0].tickQueue[P2].size() == 0);
	}
}

TEST_CASE("Variant: sendAfterTrigger on trigger input 1 is unaffected by input 2", "[MidiKit][Variant][TrigPorts]") {
	const char* js = R"(/**
 * @engine QuickJs@v1
 */
trig.enableIn(1, 1);
trig.enableIn(2, 1);
midi.onMessage = function(port, msg) { midiOut.sendAfterTrigger(msg, 1); };
)";
	const char* lua = R"(--[[
@engine minilua@v1
--]]
trig.enableIn(1, 1)
trig.enableIn(2, 1)
midi.onMessage = function(port, msg) midiOut.sendAfterTrigger(msg, 1) end
)";
	for (const char* script : {js, lua}) {
		CATCH_INFO(script);
		MultiScaffold mods;
		MultiModule* m = mods.create();
		wireTrigPorts(m);
		m->loadScript(script);
		int64_t frame = 1;
		pump(m, frame);

		m->midiIns.ports[0].processor.getInput().onMessage(ccMsg(0, 7, 100));
		pump(m, frame);
		REQUIRE(m->midiOuts.ports[0].tickQueue[0].size() == 1);

		driveTrig(m, frame, 1, 0, 10.f);   // input 2: not its clock
		REQUIRE(m->midiOuts.ports[0].tickQueue[0].size() == 1);
		driveTrig(m, frame, 0, 0, 10.f);
		REQUIRE(m->midiOuts.ports[0].tickQueue[0].size() == 0);
	}
}

// End-to-end: interleaved schedules on both trigger inputs

// Records what MidiOutput really sends. midi::Output::sendMessage() forwards to
// `outputDevice`, so attaching one is enough to observe the sends without a
// driver.
struct RecordingOutputDevice : midi::OutputDevice {
	std::vector<std::pair<int, int>> sent;   // (controller, value)
	void sendMessage(const midi::Message& msg) override {
		sent.push_back(std::make_pair(int(msg.getNote()), int(msg.getValue())));
	}
};

// One rising edge on (port, channel), then back low so the next one is an edge.
static void pulseTrig(MultiModule* m, int64_t& frame, int port, int ch) {
	driveTrig(m, frame, port, ch, 10.f);
	driveTrig(m, frame, port, ch, 0.f);
}

// The controller number selects the schedule, the value is just a payload:
//   cc 1: 3 ticks on input 1          cc 4: 2 ticks on input 1
//   cc 2: 1 tick  on input 2          cc 5: 2 ticks on input 2, channel 2
//   cc 3: 3 ticks on input 2          cc 6: 2 ticks on input 1, channel 2
static const char* JS_TRIG_SCHEDULES = R"(/**
 * @engine QuickJs@v1
 */
trig.enableIn(1, 1);
trig.enableIn(1, 2);
trig.enableIn(2, 1);
trig.enableIn(2, 2);
midi.onMessage = function(port, msg) {
    let cc = midi.getControl(msg);
    if (cc === 1) midiOut.sendAfterTrigger(msg, 3, 1);
    else if (cc === 2) midiOut.sendAfterTrigger(msg, 1, 2);
    else if (cc === 3) midiOut.sendAfterTrigger(msg, 3, 2);
    else if (cc === 4) midiOut.sendAfterTrigger(msg, 2, 1);
    else if (cc === 5) midiOut.sendAfterTrigger(msg, 2, 2, 2);
    else if (cc === 6) midiOut.sendAfterTrigger(msg, 2, 1, 2);
};
)";

static const char* LUA_TRIG_SCHEDULES = R"(--[[
@engine minilua@v1
--]]
trig.enableIn(1, 1)
trig.enableIn(1, 2)
trig.enableIn(2, 1)
trig.enableIn(2, 2)
midi.onMessage = function(port, msg)
    local cc = midi.getControl(msg)
    if cc == 1 then midiOut.sendAfterTrigger(msg, 3, 1)
    elseif cc == 2 then midiOut.sendAfterTrigger(msg, 1, 2)
    elseif cc == 3 then midiOut.sendAfterTrigger(msg, 3, 2)
    elseif cc == 4 then midiOut.sendAfterTrigger(msg, 2, 1)
    elseif cc == 5 then midiOut.sendAfterTrigger(msg, 2, 2, 2)
    elseif cc == 6 then midiOut.sendAfterTrigger(msg, 2, 1, 2)
    end
end
)";

typedef std::vector<std::pair<int, int>> Sent;

TEST_CASE("Variant: interleaved sendAfterTrigger schedules on both trigger inputs release in clock order", "[MidiKit][Variant][TrigPorts]") {
	for (const char* script : {JS_TRIG_SCHEDULES, LUA_TRIG_SCHEDULES}) {
		CATCH_INFO(script);
		MultiScaffold mods;
		MultiModule* m = mods.create();
		wireTrigPorts(m);
		RecordingOutputDevice dev1, dev2;
		m->midiOuts.ports[0].outputDevice = &dev1;
		m->midiOuts.ports[1].outputDevice = &dev2;
		m->midiOuts.ports[0].channel = -1;
		m->midiOuts.ports[1].channel = -1;
		m->loadScript(script);
		int64_t frame = 1;
		pump(m, frame);   // prime every enabled channel LOW

		// Schedule all six; each value is 10x its controller.
		for (int cc = 1; cc <= 6; cc++) m->midiIns.ports[0].processor.getInput().onMessage(ccMsg(0, cc, cc * 10));
		pump(m, frame);
		REQUIRE(dev1.sent.empty());

		// Trigger inputs and channels tick in an interleaved order. Only the
		// clock a message was scheduled on can release it, and only once that
		// clock has counted the message's own number of ticks.
		pulseTrig(m, frame, 1, 0);   // in 2 ch1, tick 1: cc 2 (1 tick)
		REQUIRE(dev1.sent == Sent{{2, 20}});
		pulseTrig(m, frame, 0, 0);   // in 1 ch1, tick 1: nothing due
		pulseTrig(m, frame, 1, 1);   // in 2 ch2, tick 1: nothing due
		REQUIRE(dev1.sent == Sent{{2, 20}});
		pulseTrig(m, frame, 0, 0);   // in 1 ch1, tick 2: cc 4
		REQUIRE(dev1.sent == Sent({{2, 20}, {4, 40}}));
		pulseTrig(m, frame, 1, 0);   // in 2 ch1, tick 2: nothing due
		pulseTrig(m, frame, 0, 1);   // in 1 ch2, tick 1: nothing due
		REQUIRE(dev1.sent == Sent({{2, 20}, {4, 40}}));
		pulseTrig(m, frame, 1, 1);   // in 2 ch2, tick 2: cc 5
		REQUIRE(dev1.sent == Sent({{2, 20}, {4, 40}, {5, 50}}));
		pulseTrig(m, frame, 0, 0);   // in 1 ch1, tick 3: cc 1 (cc 3 also has 3 ticks, but on input 2)
		REQUIRE(dev1.sent == Sent({{2, 20}, {4, 40}, {5, 50}, {1, 10}}));
		pulseTrig(m, frame, 0, 1);   // in 1 ch2, tick 2: cc 6
		REQUIRE(dev1.sent == Sent({{2, 20}, {4, 40}, {5, 50}, {1, 10}, {6, 60}}));
		pulseTrig(m, frame, 1, 0);   // in 2 ch1, tick 3: cc 3
		REQUIRE(dev1.sent == Sent({{2, 20}, {4, 40}, {5, 50}, {1, 10}, {6, 60}, {3, 30}}));

		// Delays count from the tick a message is scheduled at, not from zero.
		// Input 1 ch1 is now at 3 ticks, input 2 ch1 at 3.
		m->midiIns.ports[0].processor.getInput().onMessage(ccMsg(0, 1, 11));   // input 1: due at 6
		m->midiIns.ports[0].processor.getInput().onMessage(ccMsg(0, 2, 22));   // input 2: due at 4
		pump(m, frame);
		pulseTrig(m, frame, 0, 0);   // in 1 ch1, tick 4: not due
		pulseTrig(m, frame, 0, 0);   // in 1 ch1, tick 5: not due
		REQUIRE(dev1.sent.size() == 6);
		pulseTrig(m, frame, 1, 0);   // in 2 ch1, tick 4: cc 2
		REQUIRE(dev1.sent.size() == 7);
		REQUIRE(dev1.sent.back() == std::make_pair(2, 22));
		pulseTrig(m, frame, 0, 0);   // in 1 ch1, tick 6: cc 1
		REQUIRE(dev1.sent.size() == 8);
		REQUIRE(dev1.sent.back() == std::make_pair(1, 11));

		// Everything went to the first MIDI output and nothing is left queued.
		REQUIRE(dev2.sent.empty());
		for (int i = 0; i < 2 * PORT_MAX_CHANNELS; i++) {
			REQUIRE(m->midiOuts.ports[0].tickQueue[i].empty());
		}
	}
}

TEST_CASE("Variant: a script reload drops sendAfterTrigger messages pending on either trigger input", "[MidiKit][Variant][TrigPorts]") {
	MultiScaffold mods;
	MultiModule* m = mods.create();
	wireTrigPorts(m);
	RecordingOutputDevice dev;
	m->midiOuts.ports[0].outputDevice = &dev;
	m->midiOuts.ports[0].channel = -1;
	m->loadScript(JS_TRIG_SCHEDULES);
	int64_t frame = 1;
	pump(m, frame);

	m->midiIns.ports[0].processor.getInput().onMessage(ccMsg(0, 1, 10));   // input 1, 3 ticks
	m->midiIns.ports[0].processor.getInput().onMessage(ccMsg(0, 3, 30));   // input 2, 3 ticks
	pump(m, frame);
	pulseTrig(m, frame, 1, 0);
	REQUIRE(m->midiOuts.ports[0].tickQueue[0].size() == 1);
	REQUIRE(m->midiOuts.ports[0].tickQueue[PORT_MAX_CHANNELS].size() == 1);

	// The old script's pending messages must not fire on the new script's clocks.
	m->loadScript(JS_TRIG_SCHEDULES);
	pump(m, frame);
	for (int i = 0; i < 2 * PORT_MAX_CHANNELS; i++) {
		REQUIRE(m->midiOuts.ports[0].tickQueue[i].empty());
	}
	for (int i = 0; i < 4; i++) {
		pulseTrig(m, frame, 0, 0);
		pulseTrig(m, frame, 1, 0);
	}
	REQUIRE(dev.sent.empty());
}

TEST_CASE("Variant: a script reload keeps what the new script schedules in rack.onLoad", "[MidiKit][Variant][TrigPorts]") {
	const char* js = R"(/**
 * @engine QuickJs@v1
 */
trig.enableIn(2, 1);
rack.onLoad = function() {
    let msg = midi.create();
    midi.setCc(msg, 1, 7, 1);
    midiOut.sendAfterTrigger(msg, 1, 2);
};
)";
	const char* lua = R"(--[[
@engine minilua@v1
--]]
trig.enableIn(2, 1)
rack.onLoad = function()
    local msg = midi.create()
    midi.setCc(msg, 1, 7, 1)
    midiOut.sendAfterTrigger(msg, 1, 2)
end
)";
	for (const char* script : {js, lua}) {
		CATCH_INFO(script);
		MultiScaffold mods;
		MultiModule* m = mods.create();
		wireTrigPorts(m);
		RecordingOutputDevice dev;
		m->midiOuts.ports[0].outputDevice = &dev;
		m->midiOuts.ports[0].channel = -1;
		m->loadScript(JS_TRIG_SCHEDULES);
		int64_t frame = 1;
		pump(m, frame);
		m->midiIns.ports[0].processor.getInput().onMessage(ccMsg(0, 1, 10));   // stale: input 1, 3 ticks
		pump(m, frame);
		REQUIRE(m->midiOuts.ports[0].tickQueue[0].size() == 1);

		m->loadScript(script);
		pump(m, frame);
		REQUIRE(m->midiOuts.ports[0].tickQueue[0].empty());
		REQUIRE(m->midiOuts.ports[0].tickQueue[PORT_MAX_CHANNELS].size() == 1);

		pump(m, frame);   // prime input 2 LOW
		pulseTrig(m, frame, 1, 0);
		REQUIRE(dev.sent == Sent{{7, 1}});
	}
}

// A script without onUnload must not re-send its last callback's messages when
// it is closed: the teardown used to find them still in the message store and send them again.
TEST_CASE("Variant: closing a script without onUnload does not send its last messages again", "[MidiKit][Variant]") {
	const char* js = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) { midiOut.send(msg); };
)";
	const char* lua = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(port, msg) midiOut.send(msg) end
)";
	for (const char* script : {js, lua}) {
		CATCH_INFO(script);
		MultiScaffold mods;
		MultiModule* m = mods.create();
		RecordingOutputDevice dev;
		m->midiOuts.ports[0].outputDevice = &dev;
		m->midiOuts.ports[0].channel = -1;
		m->loadScript(script);
		int64_t frame = 1;
		pump(m, frame);

		m->midiIns.ports[0].processor.getInput().onMessage(ccMsg(0, 7, 100));
		pump(m, frame);
		REQUIRE(dev.sent == Sent{{7, 100}});

		m->loadScript(QUICKJS_EMPTY);
		pump(m, frame);
		REQUIRE(dev.sent == Sent{{7, 100}});
	}
}

TEST_CASE("Variant: CV inputs and params are addressed by index", "[MidiKit][Variant]") {
	MultiScaffold mods;
	MultiModule* m = mods.create();

	// Disabled until the script enables them.
	m->inputs[MultiModule::INPUT + 2].setVoltage(3.f);
	m->params[MultiModule::PARAM + 0].setValue(0.5f);
	REQUIRE(m->getInputVoltage(2, 0) == 0.f);
	REQUIRE(m->getParamValue(0) == 0.f);

	m->enableInput(2);
	m->enableParam(0);
	REQUIRE(m->getInputVoltage(2, 0) == 3.f);
	REQUIRE(m->getInputVoltage(1, 0) == 0.f);
	REQUIRE(m->getParamValue(0) == 0.5f);
}

// Persistence

TEST_CASE("Variant: MIDI port JSON keys keep the first port's legacy key", "[MidiKit][Variant][JSON]") {
	MultiScaffold mods;
	MultiModule* m = mods.create();
	m->enableMidiIn(2);
	m->enableMidiOut(2);

	json_t* rootJ = m->dataToJson();
	REQUIRE(json_object_get(rootJ, "midiInput") != nullptr);
	REQUIRE(json_object_get(rootJ, "midiInput2") != nullptr);
	REQUIRE(json_object_get(rootJ, "midiOutput") != nullptr);
	REQUIRE(json_object_get(rootJ, "midiOutput2") != nullptr);
	REQUIRE(json_object_get(rootJ, "midiInput3") == nullptr);

	// Round trip: the second input's channel survives.
	m->midiIns.ports[1].processor.getInput().channel = 5;
	m->midiOuts.ports[1].channel = 7;
	json_decref(rootJ);
	rootJ = m->dataToJson();

	MultiScaffold mods2;
	MultiModule* m2 = mods2.create();
	m2->dataFromJson(rootJ);
	json_decref(rootJ);
	REQUIRE(m2->midiIns.ports[1].processor.getInput().channel == 5);
	REQUIRE(m2->midiOuts.ports[1].channel == 7);
	REQUIRE(m2->midiIns.ports[0].processor.getInput().channel == m->midiIns.ports[0].processor.getInput().channel);
}

TEST_CASE("Variant: only enabled MIDI ports are serialized", "[MidiKit][Variant][JSON]") {
	MultiScaffold mods;
	MultiModule* m = mods.create();

	json_t* rootJ = m->dataToJson();
	REQUIRE(json_object_get(rootJ, "midiInput") != nullptr);
	REQUIRE(json_object_get(rootJ, "midiOutput") != nullptr);
	REQUIRE(json_object_get(rootJ, "midiInput2") == nullptr);
	REQUIRE(json_object_get(rootJ, "midiOutput2") == nullptr);
	json_decref(rootJ);

	m->loadScript(JS_PORTS_OUT);   // midiOut.enablePorts(2)
	rootJ = m->dataToJson();
	REQUIRE(json_object_get(rootJ, "midiInput2") == nullptr);
	REQUIRE(json_object_get(rootJ, "midiOutput2") != nullptr);
	json_decref(rootJ);
}

TEST_CASE("Variant: reloading a script keeps the settings of MIDI ports it stops using", "[MidiKit][Variant][JSON]") {
	MultiScaffold mods;
	MultiModule* m = mods.create();
	m->loadScript(JS_PORT_PROBE);   // midi.enablePorts(2)
	m->loadScript(JS_PORTS_OUT);
	m->midiIns.ports[1].processor.getInput().channel = 5;
	m->midiOuts.ports[1].channel = 7;
	m->midiIns.ports[1].processor.getInput().setDriverId(0);
	m->midiOuts.ports[1].setDriverId(0);
	int inDriver = m->midiIns.ports[1].processor.getInput().getDriverId();
	int outDriver = m->midiOuts.ports[1].getDriverId();

	// A script that enables nothing: the ports are off, their settings stay.
	m->loadScript(QUICKJS_EMPTY);
	REQUIRE_FALSE(m->midiIns.isEnabled(1));
	REQUIRE_FALSE(m->midiOuts.isEnabled(1));
	REQUIRE(m->midiIns.ports[1].processor.getInput().channel == 5);
	REQUIRE(m->midiOuts.ports[1].channel == 7);
	REQUIRE(m->midiIns.ports[1].processor.getInput().getDriverId() == inDriver);
	REQUIRE(m->midiOuts.ports[1].getDriverId() == outDriver);

	m->clearScript();
	REQUIRE(m->midiIns.ports[1].processor.getInput().channel == 5);
	REQUIRE(m->midiOuts.ports[1].channel == 7);

	// Enabling them again brings the user's selection back, and it is saved.
	m->loadScript(JS_PORT_PROBE);
	m->loadScript(JS_PORTS_OUT);
	REQUIRE(m->midiIns.ports[1].processor.getInput().channel == 5);
	REQUIRE(m->midiOuts.ports[1].channel == 7);
	json_t* rootJ = m->dataToJson();
	REQUIRE(json_object_get(rootJ, "midiOutput2") != nullptr);
	json_decref(rootJ);
}

TEST_CASE("Variant: a patch restores a MIDI port's settings before the script enables it", "[MidiKit][Variant][JSON]") {
	MultiScaffold mods;
	MultiModule* m = mods.create();
	m->loadScript(JS_PORTS_OUT);
	m->midiOuts.ports[1].channel = 7;
	json_t* rootJ = m->dataToJson();

	MultiScaffold mods2;
	MultiModule* m2 = mods2.create();
	m2->dataFromJson(rootJ);
	json_decref(rootJ);
	REQUIRE(m2->midiOuts.isEnabled(1));
	REQUIRE(m2->midiOuts.ports[1].channel == 7);
}

TEST_CASE("Variant: a single-port patch loads into the first port", "[MidiKit][Variant][JSON]") {
	MultiScaffold mods;
	MultiModule* m = mods.create();

	// The single-port MidiKit saves only "midiInput"/"midiOutput".
	Test::ModuleScaffold<MidiKitModule> single([]() { return createModule(); });
	MidiKitModule* s = single.create();
	s->midiIns.ports[0].processor.getInput().channel = 9;
	json_t* rootJ = s->dataToJson();
	m->dataFromJson(rootJ);
	json_decref(rootJ);

	REQUIRE(m->midiIns.ports[0].processor.getInput().channel == 9);
}

// Widgets

static int countMenuEntries(rack::ui::Menu* menu, const std::string& text) {
	int n = 0;
	for (rack::Widget* child : menu->children) {
		if (auto* mi = dynamic_cast<rack::ui::MenuItem*>(child)) {
			if (mi->text == text) n++;
		}
		else if (auto* ml = dynamic_cast<rack::ui::MenuLabel*>(child)) {
			if (ml->text == text) n++;
		}
	}
	return n;
}

// The "Log" submenu of `menu` (owned by the caller), or null without one.
static rack::ui::Menu* createLogSubmenu(rack::ui::Menu* menu) {
	for (rack::Widget* child : menu->children) {
		auto* mi = dynamic_cast<rack::ui::MenuItem*>(child);
		if (mi && mi->text == "Log") return mi->createChildMenu();
	}
	return nullptr;
}

TEST_CASE("Variant: context menu lists every MIDI port once all are enabled", "[MidiKit][Variant][ContextMenu]") {
	MultiScaffold mods;
	MultiModule* m = mods.create();
	m->enableMidiIn(2);
	m->enableMidiOut(2);
	MultiWidget* mw = new MultiWidget(m);

	rack::ui::Menu* menu = new rack::ui::Menu;
	mw->appendContextMenu(menu);
	REQUIRE(countMenuEntries(menu, "MIDI input 1") == 1);
	REQUIRE(countMenuEntries(menu, "MIDI input 2") == 1);
	REQUIRE(countMenuEntries(menu, "MIDI output 1") == 1);
	REQUIRE(countMenuEntries(menu, "MIDI output 2") == 1);
	REQUIRE(countMenuEntries(menu, "MIDI input") == 0);

	delete menu;
	Test::destroyWidget(mw);
}

// Log display context menu

struct ClipboardSpy : StoermelderPackOne::vcv::UiAccess {
	std::string text;
	int sets = 0;
	void setClipboard(const std::string& t) override { text = t; sets++; }
};

static rack::ui::MenuItem* findMenuItem(rack::ui::Menu* menu, const std::string& text) {
	for (rack::Widget* child : menu->children) {
		auto* mi = dynamic_cast<rack::ui::MenuItem*>(child);
		if (mi && mi->text == text) return mi;
	}
	return nullptr;
}

TEST_CASE("Log display context menu copies the whole log to the clipboard and clears it", "[MidiKit][LogMenu]") {
	ModuleScaffold mods;
	MidiKitModule* m = mods.create();
	m->model = modelMidiKit;
	MidiKitWidget* mw = Test::createWidget<MidiKitWidget>(m);
	REQUIRE(mw->logDisplay != nullptr);

	ClipboardSpy spy;
	StoermelderPackOne::vcv::uiAccess = &spy;

	// Empty log: both entries are there but disabled.
	rack::ui::Menu* menu = new rack::ui::Menu;
	mw->logDisplay->appendContextMenu(menu);
	REQUIRE(findMenuItem(menu, "Copy to clipboard") != nullptr);
	REQUIRE(findMenuItem(menu, "Clear") != nullptr);
	REQUIRE(findMenuItem(menu, "Copy to clipboard")->disabled);
	REQUIRE(findMenuItem(menu, "Clear")->disabled);
	delete menu;

	// More lines than the display can show: the copy still has all of them, oldest first.
	m->loadScript(QUICKJS_EMPTY);   // pushes a RESET, clearing older entries
	for (int i = 0; i < 40; i++) m->writeLog("line" + std::to_string(i), false);
	mw->step();
	menu = new rack::ui::Menu;
	mw->logDisplay->appendContextMenu(menu);
	REQUIRE_FALSE(findMenuItem(menu, "Copy to clipboard")->disabled);
	findMenuItem(menu, "Copy to clipboard")->doAction(false);
	REQUIRE(spy.sets == 1);
	REQUIRE(spy.text.find("line0\n") != std::string::npos);
	REQUIRE(spy.text.find("line39\n") != std::string::npos);
	REQUIRE(spy.text.find("line0\n") < spy.text.find("line39\n"));
	REQUIRE(mw->buffer.size() >= 40);   // Copy leaves the log alone

	findMenuItem(menu, "Clear")->doAction(false);
	REQUIRE(mw->buffer.empty());
	mw->logDisplay->step();
	REQUIRE(mw->logDisplay->text.empty());
	delete menu;

	// Nothing left: the entries are disabled again.
	menu = new rack::ui::Menu;
	mw->logDisplay->appendContextMenu(menu);
	REQUIRE(findMenuItem(menu, "Copy to clipboard")->disabled);
	REQUIRE(findMenuItem(menu, "Clear")->disabled);
	delete menu;

	StoermelderPackOne::vcv::uiAccess = nullptr;
	Test::destroyWidget(mw);
}

TEST_CASE("Variant: context menu lists MIDI ports 2+ only while the script enables them", "[MidiKit][Variant][ContextMenu]") {
	MultiScaffold mods;
	MultiModule* m = mods.create();
	MultiWidget* mw = new MultiWidget(m);   // no registered model, so built directly

	auto count = [&](const std::string& text) {
		rack::ui::Menu* menu = new rack::ui::Menu;
		mw->appendContextMenu(menu);
		int n = countMenuEntries(menu, text);
		delete menu;
		return n;
	};
	REQUIRE(count("MIDI input 1") == 1);
	REQUIRE(count("MIDI output 1") == 1);
	REQUIRE(count("MIDI input 2") == 0);
	REQUIRE(count("MIDI output 2") == 0);

	m->loadScript(JS_PORTS_OUT);   // outputs only
	REQUIRE(count("MIDI input 2") == 0);
	REQUIRE(count("MIDI output 2") == 1);

	m->loadScript(JS_PORT_PROBE);  // inputs only
	REQUIRE(count("MIDI input 2") == 1);
	REQUIRE(count("MIDI output 2") == 0);

	Test::destroyWidget(mw);
}

TEST_CASE("Variant: MidiKit context menu lists only MIDI port 1 by default", "[MidiKit][Variant][ContextMenu]") {
	ModuleScaffold mods;
	MidiKitModule* m = mods.create();
	m->model = modelMidiKit;
	MidiKitWidget* mw = Test::createWidget<MidiKitWidget>(m);

	rack::ui::Menu* menu = new rack::ui::Menu;
	mw->appendContextMenu(menu);
	REQUIRE(countMenuEntries(menu, "MIDI input 1") == 1);
	REQUIRE(countMenuEntries(menu, "MIDI output 1") == 1);
	REQUIRE(countMenuEntries(menu, "MIDI input 2") == 0);   // not enabled by the script
	REQUIRE(countMenuEntries(menu, "MIDI output 2") == 0);
	REQUIRE(countMenuEntries(menu, "Log") == 0);   // only MidiKitMicro has it

	delete menu;
	Test::destroyWidget(mw);
}

TEST_CASE("Variant: MidiKitMicro widget works without a log display", "[MidiKit][Variant][Micro]") {
	Test::ModuleScaffold<MidiKitMicroModule> mods([]() {
		MidiKitMicroModule* m = new MidiKitMicroModule(std::make_shared<StoermelderPackOne::SyncTaskWorker>());
		m->id = rand();
		return m;
	});
	MidiKitMicroModule* m = mods.create();
	m->model = modelMidiKitMicro;
	MidiKitMicroWidget* mw = Test::createWidget<MidiKitMicroWidget>(m);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->logDisplay == nullptr);

	// The log is drained into the widget, which keeps only the newest five
	// lines for the context menu; stepping must not touch the missing display.
	m->loadScript(QUICKJS_EMPTY);   // pushes a RESET, clearing older entries
	for (int i = 0; i < 8; i++) m->writeLog("line" + std::to_string(i), false);
	mw->step();
	ScriptLog::Entry t;
	REQUIRE_FALSE(m->log.tryPop(t));
	REQUIRE(mw->buffer.size() == 5);

	// The last lines are in the "Log" submenu, newest first.
	rack::ui::Menu* logMenu = new rack::ui::Menu;
	mw->appendContextMenu(logMenu);
	rack::ui::Menu* sub = createLogSubmenu(logMenu);
	REQUIRE(sub != nullptr);
	REQUIRE(sub->children.size() == 5);
	delete sub;
	delete logMenu;

	// Long lines wrap inside a fixed width: same width, but taller than a
	// short line. The default UiAccess measures text without a window.
	StoermelderPackOne::vcv::UiAccess measureFallback;
	StoermelderPackOne::vcv::uiAccess = &measureFallback;
	std::string longText;
	for (int i = 0; i < 60; i++) longText += "word ";
	m->writeLog(longText, false);
	mw->step();
	rack::ui::Menu* longMenu = new rack::ui::Menu;
	mw->appendContextMenu(longMenu);
	sub = createLogSubmenu(longMenu);
	REQUIRE(sub != nullptr);
	REQUIRE(sub->children.size() == 5);
	auto it = sub->children.begin();
	rack::Widget* longLine = *it++;
	rack::Widget* shortLine = *it;
	longLine->step();
	shortLine->step();
	REQUIRE(longLine->box.size.x == shortLine->box.size.x);
	REQUIRE(longLine->box.size.y > shortLine->box.size.y);   // newest is the 400-char line
	StoermelderPackOne::vcv::uiAccess = nullptr;
	delete sub;
	delete longMenu;

	// A reset clears the menu log.
	m->loadScript(QUICKJS_EMPTY);
	mw->step();
	REQUIRE(mw->buffer.size() < 5);
	mw->resetLog();
	REQUIRE(mw->buffer.empty());
	rack::ui::Menu* emptyMenu = new rack::ui::Menu;
	mw->appendContextMenu(emptyMenu);
	sub = createLogSubmenu(emptyMenu);
	REQUIRE(sub != nullptr);
	REQUIRE(sub->children.size() == 1);   // the "(empty)" placeholder
	delete sub;
	delete emptyMenu;

	// It still has the MIDI menu and the base's script menu.
	rack::ui::Menu* menu = new rack::ui::Menu;
	mw->appendContextMenu(menu);
	REQUIRE(countMenuEntries(menu, "MIDI input 1") == 1);
	REQUIRE(countMenuEntries(menu, "MIDI output 1") == 1);
	REQUIRE(countMenuEntries(menu, "MIDI input 2") == 0);
	REQUIRE(countMenuEntries(menu, "MIDI output 2") == 0);
	REQUIRE(countMenuEntries(menu, "Log") == 1);

	delete menu;
	Test::destroyWidget(mw);
}
// ── The outgoing script's rack.onUnload output ──────────────────────────────

static const char* JS_UNLOAD_ON_OUTPUT_2 = R"(/**
 * @engine QuickJs@v1
 */
midiOut.enablePorts(2);
rack.onUnload = function() {
    midiOut.selectPort(2);
    let m = midi.create();
    midi.setCc(m, 1, 9, 77);
    midiOut.send(m);
};
)";

static const char* LUA_UNLOAD_ON_OUTPUT_2 = R"(--[[
@engine minilua@v1
--]]
midiOut.enablePorts(2)
rack.onUnload = function()
    midiOut.selectPort(2)
    local m = midi.create()
    midi.setCc(m, 1, 9, 77)
    midiOut.send(m)
end
)";

// The port enables belong to the script, but only until its onUnload has run: an
// all-notes-off to output 2 must still reach it when the script is replaced,
// cleared or the module reset.
TEST_CASE("Variant: rack.onUnload output on a second port survives reload, clear and reset", "[MidiKit][Variant]") {
	enum Action { RELOAD_SAME_ENGINE, RELOAD_OTHER_ENGINE, CLEAR, RESET };
	for (const char* script : { JS_UNLOAD_ON_OUTPUT_2, LUA_UNLOAD_ON_OUTPUT_2 }) {
		for (Action action : { RELOAD_SAME_ENGINE, RELOAD_OTHER_ENGINE, CLEAR, RESET }) {
			CATCH_INFO(script);
			CATCH_INFO(action);
			MultiScaffold mods;
			MultiModule* m = mods.create();
			RecordingOutputDevice dev2;
			m->midiOuts.ports[1].outputDevice = &dev2;
			m->midiOuts.ports[1].channel = -1;
			m->loadScript(script);
			int64_t frame = 1;
			pump(m, frame);
			REQUIRE(dev2.sent.empty());

			bool js = std::string(script).find("QuickJs") != std::string::npos;
			switch (action) {
				case RELOAD_SAME_ENGINE: m->loadScript(js ? QUICKJS_EMPTY : LUA_EMPTY); break;
				case RELOAD_OTHER_ENGINE: m->loadScript(js ? LUA_EMPTY : QUICKJS_EMPTY); break;
				case CLEAR: m->loadScript(""); break;
				case RESET:
					m->onReset();
					// A reset also detaches the MIDI outputs from their devices.
					m->midiOuts.ports[1].outputDevice = &dev2;
					break;
			}
			pump(m, frame);

			REQUIRE(dev2.sent == Sent{{9, 77}});
			// ...and the new script starts with only output 1 enabled again.
			REQUIRE_FALSE(m->midiOuts.isEnabled(1));
		}
	}
}
