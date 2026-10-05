// Handle-store warnings, onLoad/onUnload/onRemove/onReset ordering, out-queue draining and overflow.
//
// Part of the cross-engine suite: the shared helpers (run, requireEquivalent, EngineRun, ...)
// are in MidiKit.test.hpp.

// midi.create() / midi.createNRPN() outside a callback
// A message handle is only valid inside the callback that created it, so a
// creator called anywhere else (top level, param.onTooltip, ...) raises. At top
// level that fails the load with the script line. These tests pin the error
// and that the two engines use the same wording for it (unlike parse-error
// text, which differs deliberately).

static const char* OUTSIDE_CALLBACK_ERROR = "only allowed inside a callback";

// The whole log of loading `script`, errors included (run() insists on none).
static std::string loadLogOf(const std::string& script) {
	Kit<> kit;
	MidiKitModule* m = kit.m;
	m->loadScript(script);
	std::string log = drainLog(m);
	return log;
}

static void requireLoadError(const std::string& js, const std::string& lua, const char* needle) {
	CATCH_INFO("JS:\n" << js);
	CATCH_INFO("Lua:\n" << lua);
	std::string jsLog = loadLogOf(js);
	std::string luaLog = loadLogOf(lua);
	CATCH_INFO("JS log:\n" << jsLog);
	CATCH_INFO("Lua log:\n" << luaLog);
	REQUIRE(jsLog.find("Error loading script") != std::string::npos);
	REQUIRE(luaLog.find("Error loading script") != std::string::npos);
	REQUIRE(jsLog.find(needle) != std::string::npos);
	REQUIRE(luaLog.find(needle) != std::string::npos);
}

static const char* JS_TOPLEVEL_CREATE = R"(/**
 * @engine QuickJs@v1
 */
let g = midi.create();
)";

static const char* LUA_TOPLEVEL_CREATE = R"(--[[
@engine minilua@v1
--]]
g = midi.create()
)";

// A top-level message handle can't be built at all: the script fails at
// midi.create() instead of getting a handle that dies with the next callback.

static const char* JS_TOPLEVEL_SYSEX = R"(/**
 * @engine QuickJs@v1
 */
let msg = midi.create();
midi.setSysEx(msg, "43104c0000");
rack.log("PROBE:" + (midi.getType(msg) === midi.SYSEX ? "yes" : "no"));
)";

static const char* LUA_TOPLEVEL_SYSEX = R"(--[[
@engine minilua@v1
--]]
msg = midi.create()
midi.setSysEx(msg, "43104c0000")
rack.log("PROBE:" .. (midi.getType(msg) == midi.SYSEX and "yes" or "no"))
)";

TEST_CASE("midi.create at top level fails the load identically", "[MidiKit][CrossEngine]") {
	requireLoadError(JS_TOPLEVEL_CREATE, LUA_TOPLEVEL_CREATE, OUTSIDE_CALLBACK_ERROR);

	// A script that goes on to use the handle fails at the create, so none of its probes run.
	requireLoadError(JS_TOPLEVEL_SYSEX, LUA_TOPLEVEL_SYSEX, OUTSIDE_CALLBACK_ERROR);
	REQUIRE(loadLogOf(JS_TOPLEVEL_SYSEX).find("PROBE:") == std::string::npos);
	REQUIRE(loadLogOf(LUA_TOPLEVEL_SYSEX).find("PROBE:") == std::string::npos);
}


static const char* JS_TOPLEVEL_CREATE_NRPN = R"(/**
 * @engine QuickJs@v1
 */
let g = midi.createNRPN();
)";

static const char* LUA_TOPLEVEL_CREATE_NRPN = R"(--[[
@engine minilua@v1
--]]
g = midi.createNRPN()
)";

TEST_CASE("midi.createNRPN at top level fails the load identically", "[MidiKit][CrossEngine]") {
	requireLoadError(JS_TOPLEVEL_CREATE_NRPN, LUA_TOPLEVEL_CREATE_NRPN, OUTSIDE_CALLBACK_ERROR);
}


static const char* JS_CALLBACK_CREATE = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let m = midi.create();
    midi.setCc(m, 1, 20, 100);
    midiOut.send(m);
};
)";

static const char* LUA_CALLBACK_CREATE = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local m = midi.create()
    midi.setCc(m, 1, 20, 100)
    midiOut.send(m)
end
)";

TEST_CASE("midi.create inside midi.onMessage does not fail in either engine", "[MidiKit][CrossEngine]") {
	requireEquivalentLog(JS_CALLBACK_CREATE, LUA_CALLBACK_CREATE, OUTSIDE_CALLBACK_ERROR, false);
}


// A message can't be built at top level, so a send from there can't happen
// either: the script fails at midi.create() and nothing is emitted, whichever
// send call follows it.
TEST_CASE("A send from top-level code fails the load and emits nothing, for every send call", "[MidiKit][CrossEngine]") {
	const char* call = GENERATE("midiOut.send(m)", "midiOut.sendAfterMs(m, 10)", "midiOut.sendAtFrame(m, 200)", "midiOut.sendAfterTrigger(m, 1)");
	CATCH_INFO(call);
	std::string js = std::string("/**\n * @engine QuickJs@v1\n */\ntrig.enableIn(1, 1);\nlet m = midi.create();\nmidi.setNoteOn(m, 1, 60, 100);\n")
		+ call + ";\nmidi.onMessage = function(port, msg) {};\n";
	std::string lua = std::string("--[[\n@engine minilua@v1\n--]]\ntrig.enableIn(1, 1)\nlocal m = midi.create()\nmidi.setNoteOn(m, 1, 60, 100)\n")
		+ call + "\nmidi.onMessage = function(port, msg) end\n";
	requireLoadError(js, lua, OUTSIDE_CALLBACK_ERROR);
	FOR_EACH_LANG;
	Kit<> kit;
	MidiKitModule* m = kit.m;
	m->loadScript(lang == Lang::Js ? js : lua);
	drainLog(m);
	int port, ticks;
	midi::Message out;
	REQUIRE(!processOutMessage(m, port, out, ticks));
}


// onLoad

static const char* JS_ON_LOAD = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(midiPort, msg) {};
rack.onLoad = function() {
    rack.log("onLoad ran");
    let msg = midi.create();
    midi.setNoteOn(msg, 1, 60, 100);
    midiOut.send(msg);
};
)";

static const char* LUA_ON_LOAD = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg) end
rack.onLoad = function()
    rack.log("onLoad ran")
    local msg = midi.create()
    midi.setNoteOn(msg, 1, 60, 100)
    midiOut.send(msg)
end
)";

TEST_CASE("onLoad runs once and sends one Note-On, in both engines", "[MidiKit][CrossEngine]") {
	auto checkOnLoad = [](const std::string& script) {
		Kit<> kit;
		MidiKitModule* m = kit.m;
		m->loadScript(script);

		std::string loadLog = drainLog(m);
		REQUIRE(loadLog.find("onLoad ran") != std::string::npos);
		REQUIRE(loadLog.find("onLoad ran", loadLog.find("onLoad ran") + 1) == std::string::npos);

		int port, ticks;
		midi::Message out;
		REQUIRE(processOutMessage(m, port, out, ticks));
		auto sent = Out::of(out, port, ticks);
		REQUIRE_FALSE(processOutMessage(m, port, out, ticks));
		return sent;
	};

	auto js = checkOnLoad(JS_ON_LOAD);
	auto lua = checkOnLoad(LUA_ON_LOAD);
	REQUIRE(js.port == 0);
	REQUIRE(lua.port == 0);
	REQUIRE(js.bytes == std::vector<uint8_t>{0x90, 60, 100});
	REQUIRE(lua.bytes == std::vector<uint8_t>{0x90, 60, 100});
}

TEST_CASE("Script without onLoad loads without any onLoad log noise in either engine", "[MidiKit][CrossEngine]") {
	requireEquivalentLog(JS_NO_ON_LOAD, LUA_NO_ON_LOAD, "onLoad", false);
}

TEST_CASE("onUnload runs when replaced and sends its Note-Off in both engines", "[MidiKit][CrossEngine]") {
	auto checkOnUnload = [](const std::string& script) {
		Kit<> kit;
		MidiKitModule* m = kit.m;
		m->loadScript(script);
		drainLog(m);

		m->clearScript();

		std::string log = drainLog(m);
		REQUIRE(log.find("onUnload ran") != std::string::npos);

		// The out-queue is module-owned, so onUnload()'s message is still there
		// regardless of which engine produced it or that activeEngine is now null.
		int port, ticks;
		midi::Message out;
		REQUIRE(processOutMessage(m, port, out, ticks));
		auto sent = Out::of(out, port, ticks);
		return sent;
	};

	auto js = checkOnUnload(JS_ON_UNLOAD);
	auto lua = checkOnUnload(LUA_ON_UNLOAD);
	REQUIRE(js.port == 0);
	REQUIRE(lua.port == 0);
	REQUIRE(js.bytes == std::vector<uint8_t>{0x80, 60, 0});
	REQUIRE(lua.bytes == std::vector<uint8_t>{0x80, 60, 0});
}


TEST_CASE("Destroying the module runs onUnload and sends its message to the device", "[MidiKit][CrossEngine]") {
	// The script is closed in the destructor (UI thread), not in onRemove() (under
	// Rack's engine mutex). The device outlives the module, so what onUnload()
	// sent is observable after the delete: the message left the out-queue for the
	// device instead of being freed with the module.
	auto check = [](const std::string& script) {
		Device dev;
		MidiKitModule* m = createModule();
		m->midiOuts.ports[0].outputDevice = &dev;
		m->midiOuts.ports[0].channel = -1;
		m->loadScript(script);
		drainLog(m);
		REQUIRE(m->host.getActiveEngine() != nullptr);

		delete m;

		REQUIRE(dev.sent.size() == 1);
		REQUIRE(dev.sent[0].status == 0x8);   // note off
		REQUIRE(dev.sent[0].note == 60);
	};
	check(JS_ON_UNLOAD);
	check(LUA_ON_UNLOAD);
}


TEST_CASE("onRemove() leaves the script running; the destructor closes it once", "[MidiKit][CrossEngine]") {
	// onRemove() runs under Rack's engine mutex, so it must not wait for the
	// worker: it only leaves the broadcast bus. Even a repeat RemoveEvent
	// (undo/redo can plausibly produce one) does not run onUnload(); the
	// destructor does, once.
	auto check = [](const std::string& script) {
		Device dev;
		MidiKitModule* m = createModule();
		m->midiOuts.ports[0].outputDevice = &dev;
		m->midiOuts.ports[0].channel = -1;
		m->loadScript(script);
		drainLog(m);

		Module::RemoveEvent eRemove;
		m->onRemove(eRemove);
		m->onRemove(eRemove);
		REQUIRE(drainLog(m).find("onUnload ran") == std::string::npos);
		REQUIRE(m->host.getActiveEngine() != nullptr);
		REQUIRE(dev.sent.empty());

		delete m;
		REQUIRE(dev.sent.size() == 1);
	};
	check(JS_ON_UNLOAD);
	check(LUA_ON_UNLOAD);
}


TEST_CASE("switching engines keeps the outgoing engine's onUnload output", "[MidiKit][CrossEngine]") {
	// Covers the queue consolidation: the outgoing engine's onUnload() message
	// used to be queued on THAT engine's own out-queue, which nothing drained
	// once activeEngine moved on. With a module-owned queue nothing is keyed on
	// which engine is active, so the message is still observable here.
	//
	// This does NOT cover the switch being a blocking host.unload() rather than
	// an async loadScript("") — under SyncTaskWorker both run onUnload() inline,
	// so the message lands either way. See the async-worker test below.
	Kit<> kit;
	MidiKitModule* m = kit.m;
	m->loadScript(JS_ON_UNLOAD);
	drainLog(m);
	REQUIRE(m->host.isQuickJsEngine());

	m->loadScript(LUA_ON_UNLOAD);
	REQUIRE(m->host.isLuaEngine());

	std::string log = drainLog(m);
	REQUIRE(log.find("onUnload ran") != std::string::npos);   // the outgoing (JS) engine's onUnload

	int port, ticks;
	midi::Message out;
	REQUIRE(processOutMessage(m, port, out, ticks));
	REQUIRE(out.getStatus() == 0x8);   // note off
	REQUIRE(out.getNote() == 60);

}


// async-worker teardown
// These use a real background worker rather than SyncTaskWorker. Under
// SyncTaskWorker every dispatch runs inline, so a fire-and-forget loadScript()
// and a blocking host.unload() are indistinguishable — a test written against it
// passes whether or not teardown actually waits. The whole point of the switch
// and teardown paths using host.unload() is that they DO wait, which only a real
// worker can show.

TEST_CASE("Switching engines runs the outgoing onUnload() before the new script, in one worker task", "[MidiKit][CrossEngine][Async]") {
	// A switch, a reload into the same engine and a clear all queue ONE worker
	// task: onUnload(), endUnload(), then the new script. The outgoing
	// onUnload() therefore runs with its port enables intact (port 2 here) and
	// its log lines come before the reset; the new script's come after it.
	static const char* JS_UNLOAD_PORT2 = R"(/**
 * @engine QuickJs@v1
 */
midiOut.enablePorts(2);
rack.onUnload = function() {
    rack.log("onUnload ran");
    let msg = midi.create();
    midi.setNoteOff(msg, 1, 60);
    midiOut.selectPort(2);
    midiOut.send(msg);
};
)";
	static const char* LUA_LOGS_ON_LOAD = R"(--[[
@engine minilua@v1
--]]
rack.onLoad = function()
    rack.log("onLoad ran")
end
)";
	auto worker = asyncWorker();
	Kit<> kit(worker);
	MidiKitModule* m = kit.m;

	m->loadScript(JS_UNLOAD_PORT2);
	barrier(worker);
	drainLog(m);
	REQUIRE(m->host.isQuickJsEngine());

	m->loadScript(LUA_LOGS_ON_LOAD);
	barrier(worker);

	std::vector<std::tuple<LOG_FORMAT, std::string>> log = drainLogEntries(m);
	int unloadAt = -1, resetAt = -1, loadAt = -1;
	for (int i = 0; i < (int)log.size(); i++) {
		const std::string& text = std::get<1>(log[i]);
		if (text.find("onUnload ran") != std::string::npos) unloadAt = i;
		if (std::get<0>(log[i]) == LOG_FORMAT::RESET) resetAt = i;
		if (text.find("onLoad ran") != std::string::npos) loadAt = i;
	}
	REQUIRE(unloadAt >= 0);
	REQUIRE(unloadAt < resetAt);
	REQUIRE(resetAt < loadAt);

	// Sent on port 2: the enable was still in place when onUnload() ran.
	int port, ticks;
	midi::Message out;
	REQUIRE(processOutMessage(m, port, out, ticks));
	REQUIRE(port == 1);
	REQUIRE(out.getStatus() == 0x8);   // note off
	REQUIRE(out.getNote() == 60);

}


TEST_CASE("The destructor waits for onUnload before draining", "[MidiKit][CrossEngine][Async]") {
	// Teardown's ordering contract: host.unload() blocks, so by the time
	// the out-queue is flushed the worker has finished producing. If the close
	// were async, the drain would race it and run on an empty queue, leaving
	// onUnload()'s message stranded - a hung note on module removal - and the
	// worker would run the task on a freed module.
	auto check = [](const std::string& script) {
		Device dev;
		auto worker = asyncWorker();
		MidiKitModule* m = createModule(worker);
		m->midiOuts.ports[0].outputDevice = &dev;
		m->midiOuts.ports[0].channel = -1;
		m->loadScript(script);
		barrier(worker);
		drainLog(m);
		REQUIRE(m->host.getActiveEngine() != nullptr);

		delete m;        // no barrier: the destructor must do the waiting

		REQUIRE(dev.sent.size() == 1);
		REQUIRE(dev.sent[0].status == 0x8);
	};
	check(JS_ON_UNLOAD);
	check(LUA_ON_UNLOAD);
}


TEST_CASE("onReset() closes the active engine synchronously", "[MidiKit][CrossEngine][Async]") {
	// onReset() switched from two async loadScript("") calls to a single
	// blocking host.unload() on the active engine: once onReset() returns,
	// onUnload() has run and its output has been flushed, before the ports were
	// reset (a reset port has no device left to send to).
	auto check = [](const std::string& script) {
		auto worker = asyncWorker();
		Kit<> kit(worker);
		MidiKitModule* m = kit.m;
		m->loadScript(script);
		barrier(worker);
		drainLog(m);

		m->onReset();                // no barrier

		std::string log = drainLog(m);
		REQUIRE(log.find("onUnload ran") != std::string::npos);
		REQUIRE(m->host.getActiveEngine() == nullptr);

		// Flushed by onReset(), not left for a reset port.
		int port, ticks;
		midi::Message out;
		REQUIRE_FALSE(processOutMessage(m, port, out, ticks));

	};
	check(JS_ON_UNLOAD);
	check(LUA_ON_UNLOAD);
}

// Script swap
// A load, reload or clear is one worker task: the outgoing script's onUnload()
// with everything it set up, endUnload(), then the new script. The audio
// thread does its half (syncScriptGen()) once it sees the new generation, and
// whatever the replaced script handed it is told apart by that generation, not
// by when either thread gets there. These pin the order and what is dropped.

struct SwapRig : DeviceKit<> {
	Device& rec;

	SwapRig() : rec(dev[0]) {
		frame = 1;
		m->inputs[MidiKitModule::INPUT_TRIG].channels = 1;
		m->outputs[MidiKitModule::OUTPUT_TRIG].channels = 1;
	}

	float trigOut() const {
		return m->outputs[MidiKitModule::OUTPUT_TRIG].getVoltage(0);
	}

	// Runs `n` samples; false if trigger output 1 was ever non-zero.
	bool step(int n = 1) {
		bool low = true;
		for (int i = 0; i < n; i++) {
			DeviceKit<>::step();
			if (trigOut() != 0.f) low = false;
		}
		return low;
	}

	// One rising edge on trigger input 1, over two samples.
	bool pulse() {
		m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f);
		bool low = step();
		m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f);
		return step() && low;
	}
};

static const char* SWAP_JS_PLAIN = R"(/**
 * @engine QuickJs@v1
 */
)";

static const char* SWAP_LUA_PLAIN = R"(--[[
@engine minilua@v1
--]]
)";


static const char* SWAP_JS_UNLOAD_TICKS = R"(/**
 * @engine QuickJs@v1
 */
trig.enableIn(1);
rack.onUnload = function() {
    rack.log("ticks:" + trig.getTicks(1));
};
)";

static const char* SWAP_LUA_UNLOAD_TICKS = R"(--[[
@engine minilua@v1
--]]
trig.enableIn(1)
rack.onUnload = function()
    rack.log(string.format("ticks:%d", trig.getTicks(1)))
end
)";

TEST_CASE("Swap: the outgoing onUnload() still sees the ticks its script counted", "[MidiKit][Swap]") {
	// The tick counters used to be reset on the UI thread before the swap, so
	// onUnload() read 0. They now restart on the audio thread, after it.
	const char* script = GENERATE(SWAP_JS_UNLOAD_TICKS, SWAP_LUA_UNLOAD_TICKS);
	// Reload into the same engine, switch engines, clear.
	std::string next = GENERATE(as<std::string>{}, "same", "other", "");
	CATCH_INFO("next: " << next);
	std::string nextScript = next;
	if (next == "same") nextScript = script;
	else if (next == "other") nextScript = (script == SWAP_JS_UNLOAD_TICKS) ? SWAP_LUA_PLAIN : SWAP_JS_PLAIN;

	SwapRig r;
	r.m->loadScript(script);
	r.step();
	for (int i = 0; i < 3; i++) r.pulse();
	REQUIRE(r.m->triggerIns.triggerTick[0][0] == 3);
	drainLog(r.m);

	r.m->loadScript(nextScript);
	REQUIRE(drainLog(r.m).find("ticks:3") != std::string::npos);

	// The new script's counters start at 0.
	r.step();
	REQUIRE(r.m->triggerIns.triggerTick[0][0] == 0);
}


TEST_CASE("Swap: the Tipsy input claim is released with the script", "[MidiKit][Swap][Tipsy]") {
	SwapRig r;
	r.m->loadScript(R"(/**
 * @engine QuickJs@v1
 */
trig.enableTipsyIn();
)");
	REQUIRE(r.m->tipsyIn.claimed() == 0);

	r.m->loadScript(SWAP_JS_PLAIN);
	REQUIRE(r.m->tipsyIn.claimed() == -1);
}


static const char* SWAP_JS_UNLOAD_ALL_KINDS = R"(/**
 * @engine QuickJs@v1
 */
trig.enableIn(1);
rack.onUnload = function() {
    let a = midi.create(); midi.setNoteOff(a, 1, 60); midiOut.send(a);
    let b = midi.create(); midi.setNoteOff(b, 1, 61); midiOut.sendAfterMs(b, 0);
    let c = midi.create(); midi.setNoteOff(c, 1, 62); midiOut.sendAfterTrigger(c, 1);
    let d = midi.create(); midi.setNoteOff(d, 1, 63); midiOut.sendAtFrame(d, 0);
    trig.setHigh(1);
    trig.sendTipsy("x");
};
)";

static const char* SWAP_LUA_UNLOAD_ALL_KINDS = R"(--[[
@engine minilua@v1
--]]
trig.enableIn(1)
rack.onUnload = function()
    local a = midi.create(); midi.setNoteOff(a, 1, 60); midiOut.send(a)
    local b = midi.create(); midi.setNoteOff(b, 1, 61); midiOut.sendAfterMs(b, 0)
    local c = midi.create(); midi.setNoteOff(c, 1, 62); midiOut.sendAfterTrigger(c, 1)
    local d = midi.create(); midi.setNoteOff(d, 1, 63); midiOut.sendAtFrame(d, 0)
    trig.setHigh(1)
    trig.sendTipsy("x")
end
)";

TEST_CASE("Swap: onUnload() of a replaced script gets only immediate MIDI out", "[MidiKit][Swap]") {
	// What it schedules for later, its trigger writes and its Tipsy messages
	// would outlive the script.
	const char* script = GENERATE(SWAP_JS_UNLOAD_ALL_KINDS, SWAP_LUA_UNLOAD_ALL_KINDS);
	bool clear = GENERATE(false, true);
	CATCH_INFO("clear: " << clear);

	SwapRig r;
	r.m->loadScript(script);
	r.step();
	drainLog(r.m);

	r.m->loadScript(clear ? "" : script);
	std::string unloadLog = drainLog(r.m);
	CATCH_INFO("log: " << unloadLog);
	REQUIRE(unloadLog.find("rror") == std::string::npos);

	// Long enough for the Tipsy encoder and the trigger tick to show up.
	bool low = r.step(64);
	for (int i = 0; i < 4; i++) low = r.pulse() && low;
	low = r.step(512) && low;

	REQUIRE(r.rec.notes(0x8) == std::vector<int>{60});
	REQUIRE(low);
	REQUIRE(r.m->midiOuts.ports[0].frameQueue.empty());
	REQUIRE(r.m->midiOuts.ports[0].tickQueue[0].empty());
}


TEST_CASE("Swap: the replaced script's scheduled MIDI is dropped, its due MIDI goes out", "[MidiKit][Swap]") {
	// Same outcome whether the audio thread had already moved the messages into
	// the port queues or they were still in the worker -> audio queue.
	bool drainedFirst = GENERATE(false, true);
	CATCH_INFO("drained first: " << drainedFirst);

	SwapRig r;
	r.m->loadScript(SWAP_JS_UNLOAD_TICKS);   // enables trigger input 1
	r.step();

	midi::Message afterTick = noteOn(0, 60, 100);
	midi::Message later = noteOn(0, 61, 100);
	later.frame = r.frame + 100;
	midi::Message now = noteOn(0, 62, 100);
	REQUIRE(r.m->sendMidi(0, &afterTick, 1, 0, 1));
	REQUIRE(r.m->sendMidi(0, &later, 1, 0, 0));
	REQUIRE(r.m->sendMidi(0, &now, 1, 0, 0));
	if (drainedFirst) {
		r.step(8);
		REQUIRE(r.rec.notes(0x9) == std::vector<int>{62});
	}

	r.m->loadScript(SWAP_JS_PLAIN);
	r.step(8);
	for (int i = 0; i < 4; i++) r.pulse();
	r.step(200);

	REQUIRE(r.rec.notes(0x9) == std::vector<int>{62});
}


TEST_CASE("Swap: a trigger write of the new script is kept, the replaced script's are dropped", "[MidiKit][Swap]") {
	SwapRig r;
	r.m->loadScript(SWAP_JS_PLAIN);
	r.step();

	SECTION("the new script writes before the audio thread has caught up") {
		// The worker has reset, the audio thread not yet: a clear request raised
		// by the reset used to drop this stamped write too.
		r.m->loadScript(SWAP_JS_PLAIN);
		r.m->setTrigVoltage(0, 0, 10.f, r.frame);
		r.step();
		REQUIRE(r.trigOut() == 10.f);
	}

	SECTION("the replaced script's writes, pending or still queued") {
		r.m->setTrigVoltage(0, 0, 10.f, r.frame + 50);   // pending after the next sample
		r.step();
		r.m->setTrigVoltage(0, 0, 10.f);                 // still in the queue
		r.m->loadScript(SWAP_JS_PLAIN);
		REQUIRE(r.step(100));
	}
}


TEST_CASE("Swap: events captured for the replaced script never reach the new one", "[MidiKit][Swap]") {
	static const char* JS_LOGS_MESSAGES = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    rack.log("got note " + midi.getNote(msg));
};
)";
	SwapRig r;
	r.m->loadScript(JS_LOGS_MESSAGES);
	r.step();

	SECTION("still in the engine's queue when the swap runs") {
		r.m->host.getActiveEngine()->processInMessage(0, QueuedMessage(noteOn(0, 60, 100)));
		r.m->loadScript(JS_LOGS_MESSAGES);
		drainLog(r.m);
		r.step(16);
		REQUIRE(drainLog(r.m).find("got note") == std::string::npos);
	}

	SECTION("decoded before the audio thread has caught up with the swap") {
		r.m->loadScript(JS_LOGS_MESSAGES);
		drainLog(r.m);
		r.m->midiIns.ports[0].processor.processMessage(noteOn(0, 61, 100));
		r.step(16);
		REQUIRE(drainLog(r.m).find("got note") == std::string::npos);

		// Afterwards messages reach the new script.
		r.m->midiIns.ports[0].processor.processMessage(noteOn(0, 62, 100));
		r.step(16);
		REQUIRE(drainLog(r.m).find("got note 62") != std::string::npos);
	}
}


TEST_CASE("Swap: onReset() sends onUnload()'s MIDI before it resets the ports", "[MidiKit][Swap]") {
	// A reset port has no device: the note-off used to be drained into it.
	SwapRig r;
	r.m->loadScript(SWAP_JS_UNLOAD_ALL_KINDS);
	r.step();

	r.m->onReset();
	REQUIRE(r.rec.notes(0x8) == std::vector<int>{60});
	REQUIRE(r.step(100));
}


TEST_CASE("Swap: a script that fails to load is reset, and the log keeps the error", "[MidiKit][Swap]") {
	// Ending a script always resets what it set up, also when the engine ends
	// it; the reason is logged after the reset.
	const char* script = GENERATE(R"(/**
 * @engine QuickJs@v1
 */
midiOut.enablePorts(2);
trig.enableTipsyIn();
throw new Error("boom");
)", R"(--[[
@engine minilua@v1
--]]
midiOut.enablePorts(2)
trig.enableTipsyIn()
error("boom")
)");
	SwapRig r;
	r.m->loadScript(script);

	REQUIRE(r.m->midiOuts.enabledCount() == 1);
	REQUIRE(r.m->tipsyIn.claimed() == -1);
	std::vector<std::tuple<LOG_FORMAT, std::string>> log = drainLogEntries(r.m);
	REQUIRE_FALSE(log.empty());
	REQUIRE(std::get<0>(log.back()) != LOG_FORMAT::RESET);
	bool sawError = false;
	for (const auto& e : log) {
		if (std::get<1>(e).find("boom") != std::string::npos) sawError = true;
	}
	REQUIRE(sawError);
}


TEST_CASE("Swap: onUnload()'s held note-off survives a second reset", "[MidiKit][Swap]") {
	// In timing mode the note-off waits behind Rack's output queue. A reload into
	// a script that fails to load resets twice in one task, and the audio thread
	// may run between the two resets; neither may drop it.
	static const char* JS_TIMED_UNLOAD = R"(/**
 * @engine QuickJs@v1
 */
midiOut.enableTiming();
rack.onUnload = function() {
    let m = midi.create();
    midi.setNoteOff(m, 1, 60);
    midiOut.send(m);
};
)";
	// A block size, so the note-off is held for two blocks and a frame.
	EngineScope engine;
	engine.mock.blockFrames = 256;

	bool audioBetween = GENERATE(false, true);
	CATCH_INFO("audio thread runs between the resets: " << audioBetween);

	SwapRig r;
	r.m->loadScript(JS_TIMED_UNLOAD);
	r.step(8);
	engine.mock.frame = r.frame;

	if (audioBetween) {
		r.m->loadScript("");
		r.step(8);                    // moved into the port's frame queue
		r.m->endUnload();      // a second reset, as a failing load does
	}
	else {
		r.m->loadScript(R"(/**
 * @engine QuickJs@v1
 */
throw new Error("boom");
)");
	}
	REQUIRE(r.rec.notes(0x8).empty());   // held, not sent at once

	for (int i = 0; i < 100 && r.rec.notes(0x8).empty(); i++) r.step(100);
	REQUIRE(r.rec.notes(0x8) == std::vector<int>{60});
}


// ── midiOut.cancel() ────────────────────────────────────────────────────────

static const char* SWAP_JS_UNLOAD_CANCEL = R"(/**
 * @engine QuickJs@v1
 */
rack.onUnload = function() {
    let a = midi.create(); midi.setNoteOff(a, 1, 61, 0); midiOut.send(a);
    midiOut.cancel();
};
)";

static const char* SWAP_LUA_UNLOAD_CANCEL = R"(--[[
@engine minilua@v1
--]]
rack.onUnload = function()
    local a = midi.create(); midi.setNoteOff(a, 1, 61, 0); midiOut.send(a)
    midiOut.cancel()
end
)";

// A scheduled message `later` frames from now, as sendAtFrame() queues it.
static bool sendScheduled(SwapRig& r, int note, int64_t later) {
	midi::Message msg = noteOff(0, note);
	msg.frame = r.frame + later;
	OutTag tag;
	tag.scheduled = true;
	return r.m->sendMidi(0, &msg, 1, 0, 0, 0, tag);
}

TEST_CASE("Swap: midiOut.cancel() in onUnload() is ignored", "[MidiKit][Swap][Cancel]") {
	const char* script = GENERATE(SWAP_JS_UNLOAD_CANCEL, SWAP_LUA_UNLOAD_CANCEL);

	SwapRig r;
	r.m->loadScript(script);
	r.step();
	drainLog(r.m);

	r.m->loadScript(SWAP_JS_PLAIN);
	std::string unloadLog = drainLog(r.m);
	CATCH_INFO("log: " << unloadLog);
	REQUIRE(unloadLog.find("rror") == std::string::npos);

	// The cancel never reached the queue: only onUnload()'s own message did.
	int port, ticks;
	midi::Message msg;
	bool cancel = false;
	int entries = 0;
	while (processOutMessage(r.m, port, msg, ticks, &cancel)) {
		REQUIRE_FALSE(cancel);
		entries++;
	}
	REQUIRE(entries == 1);

	// What the new script schedules is untouched.
	REQUIRE(sendScheduled(r, 62, 100));
	r.step(300);
	REQUIRE(r.rec.notes(0x8) == std::vector<int>{62});
}

TEST_CASE("Swap: the unload message survives, and so does the new script's scheduled message", "[MidiKit][Swap][Cancel]") {
	const char* script = GENERATE(SWAP_JS_UNLOAD_CANCEL, SWAP_LUA_UNLOAD_CANCEL);

	SwapRig r;
	r.m->loadScript(script);
	r.step();
	r.m->loadScript(SWAP_JS_PLAIN);
	REQUIRE(sendScheduled(r, 62, 100));
	r.step(300);

	REQUIRE(r.rec.notes(0x8) == std::vector<int>{61, 62});
}

TEST_CASE("Swap: a cancel of the replaced script does not touch the new script's messages", "[MidiKit][Swap][Cancel]") {
	// End to end: the old script's cancel is still in the worker -> audio queue
	// when the new script loads, and the new script's scheduled message goes
	// out. The queue is first-in first-out, so the new message is always behind
	// the old cancel and this cannot catch a missing generation check: the next
	// test does.
	bool drainedFirst = GENERATE(false, true);
	CATCH_INFO("drained first: " << drainedFirst);

	SwapRig r;
	r.m->loadScript(SWAP_JS_PLAIN);
	r.step();

	REQUIRE(sendScheduled(r, 60, 100));
	if (drainedFirst) r.step(8);
	REQUIRE(r.m->cancelMidi(0, CancelMode::ALL, midi::Message(), OutGroup()));

	r.m->loadScript(SWAP_JS_PLAIN);
	REQUIRE(sendScheduled(r, 61, 100));
	r.step(300);

	// 60 belongs to the replaced script: dropped, by the swap or by its cancel.
	REQUIRE(r.rec.notes(0x8) == std::vector<int>{61});
}

TEST_CASE("Swap: the audio thread drops a cancel of an older generation", "[MidiKit][Swap][Cancel]") {
	// A message of the current script already waits in the port queue when a
	// cancel of an older generation is drained. Through the ring this order
	// cannot happen, so the guard is exercised on the queue directly.
	SwapRig r;
	r.m->loadScript(SWAP_JS_PLAIN);
	r.step();
	uint32_t gen = r.m->scriptGen.load();

	midi::Message msg = noteOff(0, 60);
	msg.frame = r.frame + 100000;
	OutTag tag;
	tag.scheduled = true;
	r.m->midiOuts.ports[0].send(msg, 0, 0, 0, -1, false, tag);
	REQUIRE(r.m->midiOuts.ports[0].frameQueue.size() == 1);

	SECTION("an older generation is dropped") {
		REQUIRE(r.m->midiOuts.enqueueCancel(0, CancelMode::ALL, midi::Message(), OutGroup(), gen - 1));
		r.step(16);
		REQUIRE(r.m->midiOuts.ports[0].frameQueue.size() == 1);
	}
	SECTION("the current generation applies (control)") {
		REQUIRE(r.m->midiOuts.enqueueCancel(0, CancelMode::ALL, midi::Message(), OutGroup(), gen));
		r.step(16);
		REQUIRE(r.m->midiOuts.ports[0].frameQueue.empty());
	}
}

TEST_CASE("Swap: a cancel still queued at teardown never reaches the device", "[MidiKit][Swap][Cancel]") {
	// flush() sends what the queue holds; a cancel entry holds a pattern, not a
	// message, and would go out as three zero bytes.
	SwapRig r;
	r.m->loadScript(SWAP_JS_PLAIN);
	midi::Message msg = noteOff(0, 60);
	REQUIRE(r.m->sendMidi(0, &msg, 1, 0, 0));
	REQUIRE(r.m->cancelMidi(0, CancelMode::ALL, midi::Message(), OutGroup()));

	r.m->midiOuts.flush(0);

	// The message went out, the cancel did not.
	REQUIRE(r.rec.sent.size() == 1);
	REQUIRE(r.rec.notes(0x8) == std::vector<int>{60});
}

TEST_CASE("Swap: a cancel that finds the output queue full is dropped and logged", "[MidiKit][Swap][Cancel]") {
	SwapRig r;
	r.m->loadScript(SWAP_JS_PLAIN);
	r.step();
	drainLog(r.m);

	// Fill the ring exactly, so that only the cancel is the one that does not fit.
	midi::Message msg = noteOn(0, 60, 100);
	while (r.m->midiOuts.queue.capacity() > 0) REQUIRE(r.m->sendMidi(0, &msg, 1, 0, 0));
	REQUIRE_FALSE(r.m->cancelMidi(0, CancelMode::ALL, midi::Message(), OutGroup()));

	r.step(16);
	REQUIRE(drainLog(r.m).find("MIDI output queue full") != std::string::npos);
}
