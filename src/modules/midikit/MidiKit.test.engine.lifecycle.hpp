// Handle-store warnings, onLoad/onUnload/onRemove/onReset ordering, out-queue draining and overflow.
//
// Part of the cross-engine suite: included into the __engine namespace by
// MidiKit.test.cpp after MidiKit.test.engine.hpp, which defines the shared helpers.

// midi.create() / midi.createNRPN() outside a callback
// A message handle is only valid inside the callback that created it, so a
// creator called anywhere else (top level, param.getName, ...) raises. At top
// level that fails the load with the script line. These tests pin the error
// and that the two engines use the same wording for it (unlike parse-error
// text, which #13's write-up notes differs deliberately).

static const char* OUTSIDE_CALLBACK_ERROR = "only allowed inside a callback";

// The whole log of loading `script`, errors included (run() insists on none).
static std::string loadLogOf(const std::string& script) {
	MidiKitModule* m = createModule();
	m->loadScript(script);
	std::string log = drainLog(m);
	Test::destroyModule(m);
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

TEST_CASE("midi.create at top level fails the load identically", "[MidiKit][CrossEngine]") {
	requireLoadError(JS_TOPLEVEL_CREATE, LUA_TOPLEVEL_CREATE, OUTSIDE_CALLBACK_ERROR);
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
	const char* calls[] = { "midiOut.send(m)", "midiOut.sendAfterMs(m, 10)", "midiOut.sendAtFrame(m, 200)", "midiOut.sendAfterTrigger(m, 1)" };
	for (const char* call : calls) {
		std::string js = std::string("/**\n * @engine QuickJs@v1\n */\ntrig.enableIn(1, 1);\nlet m = midi.create();\nmidi.setNoteOn(m, 1, 60, 100);\n")
			+ call + ";\nmidi.onMessage = function(port, msg) {};\n";
		std::string lua = std::string("--[[\n@engine minilua@v1\n--]]\ntrig.enableIn(1, 1)\nlocal m = midi.create()\nmidi.setNoteOn(m, 1, 60, 100)\n")
			+ call + "\nmidi.onMessage = function(port, msg) end\n";
		requireLoadError(js, lua, OUTSIDE_CALLBACK_ERROR);
		for (const std::string& script : { js, lua }) {
			MidiKitModule* m = createModule();
			m->loadScript(script);
			drainLog(m);
			int port, ticks;
			midi::Message out;
			REQUIRE(!processOutMessage(m, port, out, ticks));
			Test::destroyModule(m);
		}
	}
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

TEST_CASE("onLoad runs once and sends an identical message in both engines", "[MidiKit][CrossEngine]") {
	ModuleScaffold mods;
	auto checkOnLoad = [](const std::string& script) {
		MidiKitModule* m = createModule();
		m->loadScript(script);

		std::string loadLog = drainLog(m);
		REQUIRE(loadLog.find("onLoad ran") != std::string::npos);

		int port, ticks;
		midi::Message out;
		REQUIRE(processOutMessage(m, port, out, ticks));
		auto sent = toSent(port, ticks, out);
		Test::destroyModule(m);
		return sent;
	};

	auto js = checkOnLoad(JS_ON_LOAD);
	auto lua = checkOnLoad(LUA_ON_LOAD);
	REQUIRE(js.port == lua.port);
	REQUIRE(js.bytes == lua.bytes);
}


static const char* JS_NO_ON_LOAD = R"(/**
 * @engine QuickJs@v1
 */
let x = Math.max(3, 7);
)";

static const char* LUA_NO_ON_LOAD = R"(--[[
@engine minilua@v1
--]]
x = math.max(3, 7)
)";

TEST_CASE("Script without onLoad loads without any onLoad log noise in either engine", "[MidiKit][CrossEngine]") {
	requireEquivalentLog(JS_NO_ON_LOAD, LUA_NO_ON_LOAD, "onLoad", false);
}


// A top-level message handle can't be built at all: the script fails at
// midi.create() instead of getting a handle that dies with the next callback.

static const char* JS_TOPLEVEL_SYSEX = R"(/**
 * @engine QuickJs@v1
 */
let msg = midi.create();
midi.setSysEx(msg, "43104c0000");
rack.log("PROBE:" + (midi.isSysEx(msg) ? "yes" : "no"));
)";

static const char* LUA_TOPLEVEL_SYSEX = R"(--[[
@engine minilua@v1
--]]
msg = midi.create()
midi.setSysEx(msg, "43104c0000")
rack.log("PROBE:" .. (midi.isSysEx(msg) and "yes" or "no"))
)";

TEST_CASE("A top-level message handle fails the load in both engines (#D4)", "[MidiKit][CrossEngine]") {
	requireLoadError(JS_TOPLEVEL_SYSEX, LUA_TOPLEVEL_SYSEX, OUTSIDE_CALLBACK_ERROR);
	REQUIRE(loadLogOf(JS_TOPLEVEL_SYSEX).find("PROBE:") == std::string::npos);
	REQUIRE(loadLogOf(LUA_TOPLEVEL_SYSEX).find("PROBE:") == std::string::npos);
}


// onUnload

static const char* JS_ON_UNLOAD = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(midiPort, msg) {};
rack.onUnload = function() {
    rack.log("onUnload ran");
    let msg = midi.create();
    midi.setNoteOff(msg, 1, 60);
    midiOut.send(msg);
};
)";

static const char* LUA_ON_UNLOAD = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg) end
rack.onUnload = function()
    rack.log("onUnload ran")
    local msg = midi.create()
    midi.setNoteOff(msg, 1, 60)
    midiOut.send(msg)
end
)";

TEST_CASE("onUnload runs when replaced and sends an identical message in both engines", "[MidiKit][CrossEngine]") {
	ModuleScaffold mods;
	auto checkOnUnload = [](const std::string& script) {
		MidiKitModule* m = createModule();
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
		auto sent = toSent(port, ticks, out);
		Test::destroyModule(m);
		return sent;
	};

	auto js = checkOnUnload(JS_ON_UNLOAD);
	auto lua = checkOnUnload(LUA_ON_UNLOAD);
	REQUIRE(js.port == lua.port);
	REQUIRE(js.bytes == lua.bytes);
}


TEST_CASE("onRemove() sends onUnload's message to the device rather than leaving it queued", "[MidiKit][CrossEngine]") {
	// Exercises onRemove() directly rather than through Test::destroyModule(),
	// so the module survives the call and its state (not just the absence of a
	// crash) can be asserted afterward: onUnload() ran and its message left the
	// module's out-queue instead of sitting there to be freed with the module.
	auto check = [](const std::string& script) {
		MidiKitModule* m = createModule();
		m->loadScript(script);
		drainLog(m);
		REQUIRE(m->host.getActiveEngine() != nullptr);

		Module::RemoveEvent eRemove;
		m->onRemove(eRemove);

		std::string log = drainLog(m);
		REQUIRE(log.find("onUnload ran") != std::string::npos);

		int port, ticks;
		midi::Message out;
		REQUIRE_FALSE(processOutMessage(m, port, out, ticks));

		delete m;
	};
	check(JS_ON_UNLOAD);
	check(LUA_ON_UNLOAD);
}


TEST_CASE("onRemove() twice does not crash or re-run onUnload", "[MidiKit][CrossEngine]") {
	// The null-then-check in onRemove() (capture activeEngine, null it, only
	// then host.unload()) makes a second call a no-op: activeEngine is already
	// null, so there is no engine left to close. Undo/redo can plausibly
	// produce a repeat RemoveEvent dispatch, so this must be safe.
	auto check = [](const std::string& script) {
		MidiKitModule* m = createModule();
		m->loadScript(script);
		drainLog(m);

		Module::RemoveEvent eRemove;
		m->onRemove(eRemove);
		drainLog(m);

		m->onRemove(eRemove);
		std::string log = drainLog(m);
		REQUIRE(log.find("onUnload ran") == std::string::npos);

		delete m;
	};
	check(JS_ON_UNLOAD);
	check(LUA_ON_UNLOAD);
}


TEST_CASE("switching engines keeps the outgoing engine's onUnload output", "[MidiKit][CrossEngine]") {
	ModuleScaffold mods;
	// Covers the queue consolidation: the outgoing engine's onUnload() message
	// used to be queued on THAT engine's own out-queue, which nothing drained
	// once activeEngine moved on. With a module-owned queue nothing is keyed on
	// which engine is active, so the message is still observable here.
	//
	// This does NOT cover the switch being a blocking host.unload() rather than
	// an async loadScript("") — under SyncTaskWorker both run onUnload() inline,
	// so the message lands either way. See the async-worker test below.
	MidiKitModule* m = mods.create();
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
	MidiKitModule* m = createModule(worker);

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

	Test::destroyModule(m);
}


TEST_CASE("onRemove() waits for onUnload before draining", "[MidiKit][CrossEngine][Async]") {
	// Teardown's ordering contract: host.unload() blocks, so by the time
	// out.flush() runs the worker has finished producing. If the close were
	// async, the drain would race it and run on an empty queue, leaving
	// onUnload()'s message stranded — a hung note on module removal.
	auto check = [](const std::string& script) {
		auto worker = asyncWorker();
		MidiKitModule* m = createModule(worker);
		m->loadScript(script);
		barrier(worker);
		drainLog(m);
		REQUIRE(m->host.getActiveEngine() != nullptr);

		Module::RemoveEvent eRemove;
		m->onRemove(eRemove);        // no barrier: onRemove() must do the waiting

		std::string log = drainLog(m);
		REQUIRE(log.find("onUnload ran") != std::string::npos);

		// Drained by out.flush(), not left queued.
		int port, ticks;
		midi::Message out;
		REQUIRE_FALSE(processOutMessage(m, port, out, ticks));

		delete m;
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
		MidiKitModule* m = createModule(worker);
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

		Test::destroyModule(m);
	};
	check(JS_ON_UNLOAD);
	check(LUA_ON_UNLOAD);
}


// process() drains the module queue
// Runs process() over one full divider period (division 8), so the drain block
// inside `if (processDivider.process())` is reached exactly once.
static void processOneDividerPeriod(MidiKitModule* m, int64_t startFrame = 0) {
	for (int64_t f = 0; f < 8; f++) {
		m->process(Test::makeProcessArgs(startFrame + f));
	}
}

TEST_CASE("process() drains the out-queue after the script is cleared", "[MidiKit][CrossEngine]") {
	ModuleScaffold mods;
	// The drain in process() sits ABOVE the activeEngine null check, on purpose.
	// clearScript() runs onUnload() (queuing its message) and leaves
	// activeEngine null; if the drain were still gated on activeEngine, that
	// message would sit in the queue forever — the script's all-notes-off never
	// reaching the device. Asserting via process() rather than reading the queue
	// directly is what makes this cover the hoist.
	auto check = [](const std::string& script) {
		MidiKitModule* m = createModule();
		m->loadScript(script);
		drainLog(m);

		m->clearScript();
		REQUIRE(m->host.getActiveEngine() == nullptr);
		REQUIRE_FALSE(m->midiOuts.queue.empty());   // onUnload()'s message is queued

		processOneDividerPeriod(m);

		// process() moved it out of the module queue even with no active engine.
		REQUIRE(m->midiOuts.queue.empty());

		Test::destroyModule(m);
	};
	check(JS_ON_UNLOAD);
	check(LUA_ON_UNLOAD);
}


TEST_CASE("process() drains a tick-scheduled message into midiOutput", "[MidiKit]") {
	ModuleScaffold mods;
	// End-to-end for the drain: a message the engine queued with a non-zero tick
	// must reach out.ports[0]'s tick queue, not merely leave the module queue.
	// midi::Output::sendMessage() no-ops without a subscribed device, so
	// out.ports[0]'s scheduling queues are the observable endpoint.
	MidiKitModule* m = mods.create();
	midi::Message msg = noteOn(1, 60, 100);

	REQUIRE(m->sendMidi(0, &msg, 1, 0, 5));   // tick 5: lands in tickQueue
	REQUIRE(m->midiOuts.ports[0].tickQueue[0].size() == 0);

	processOneDividerPeriod(m);

	REQUIRE(m->midiOuts.queue.empty());
	REQUIRE(m->midiOuts.ports[0].tickQueue[0].size() == 1);
	REQUIRE(m->midiOuts.ports[0].tickQueue[0].top().tick == 5);

}


TEST_CASE("process() drains the queue in FIFO order across an engine switch", "[MidiKit][CrossEngine]") {
	ModuleScaffold mods;
	// The per-engine queues could never interleave; the shared one can. Messages
	// queued by the outgoing engine must still precede those from the incoming
	// engine — a switch must not reorder output. Distinguishable notes stand in
	// for the two producers.
	MidiKitModule* m = mods.create();

	midi::Message first = noteOn(1, 60, 100);
	midi::Message second = noteOn(1, 61, 100);
	midi::Message third = noteOn(1, 62, 100);
	REQUIRE(m->sendMidi(0, &first, 1, 0, 0));
	REQUIRE(m->sendMidi(0, &second, 1, 0, 0));
	REQUIRE(m->sendMidi(0, &third, 1, 0, 0));

	int port, ticks;
	midi::Message out;
	REQUIRE(processOutMessage(m, port, out, ticks));
	REQUIRE(out.getNote() == 60);
	REQUIRE(processOutMessage(m, port, out, ticks));
	REQUIRE(out.getNote() == 61);
	REQUIRE(processOutMessage(m, port, out, ticks));
	REQUIRE(out.getNote() == 62);
	REQUIRE_FALSE(processOutMessage(m, port, out, ticks));

}


TEST_CASE("An NRPN group is queued whole and in order", "[MidiKit]") {
	ModuleScaffold mods;
	// The atomicity contract has two halves: dropped whole when short on room
	// (below), and — here — queued as four consecutive messages in the order
	// given, with no interleaving from a message queued after it.
	MidiKitModule* m = mods.create();

	midi::Message group[4] = {noteOn(1, 60, 100), noteOn(1, 61, 100), noteOn(1, 62, 100), noteOn(1, 63, 100)};
	midi::Message after = noteOn(1, 70, 100);
	REQUIRE(m->sendMidi(0, group, 4, 0, 0));
	REQUIRE(m->sendMidi(0, &after, 1, 0, 0));

	int port, ticks;
	midi::Message out;
	for (int i = 0; i < 4; i++) {
		REQUIRE(processOutMessage(m, port, out, ticks));
		REQUIRE(out.getNote() == 60 + i);
	}
	REQUIRE(processOutMessage(m, port, out, ticks));
	REQUIRE(out.getNote() == 70);

}


TEST_CASE("onRemove() flushes due output immediately and drops what is scheduled", "[MidiKit]") {
	// out.flush() sets frame = -1 and calls out.ports[0].sendMessage() directly
	// rather than out.ports[0].send(): the frame and tick queues are drained only
	// by process(), which will never run again. A tick-scheduled message belongs
	// to the script being removed and is dropped with it, not parked in a queue.
	MidiKitModule* m = createModule();
	midi::Message msg = noteOn(1, 60, 100);

	REQUIRE(m->sendMidi(0, &msg, 1, 0, 5));   // would be tick-scheduled via send()

	Module::RemoveEvent eRemove;
	m->onRemove(eRemove);

	REQUIRE(m->midiOuts.queue.empty());
	// Dropped instead of being parked in a queue nothing will drain.
	REQUIRE(m->midiOuts.ports[0].tickQueue[0].size() == 0);
	REQUIRE(m->midiOuts.ports[0].frameQueue.size() == 0);

	delete m;
}


TEST_CASE("MIDI output overflow drops without corrupting the queue", "[MidiKit]") {
	ModuleScaffold mods;
	// dsp::RingBuffer::push() has no overflow check: on a full buffer it
	// overwrites unread entries and leaves size() > capacity, and
	// empty()/full() go incoherent from there. sendMidi() adds the check the
	// container lacks — this pins that the queue's invariants survive being
	// pushed past capacity.
	MidiKitModule* m = mods.create();
	midi::Message msg = noteOn(1, 60, 100);

	size_t capacity = m->midiOuts.queue.capacity();
	for (size_t i = 0; i < capacity; i++) {
		REQUIRE(m->sendMidi(0, &msg, 1, 0, 0));
	}
	REQUIRE(m->midiOuts.queue.full());

	// One more push has no room: dropped, not overwritten.
	REQUIRE_FALSE(m->sendMidi(0, &msg, 1, 0, 0));
	REQUIRE(m->midiOuts.queue.full());
	REQUIRE(m->midiOuts.queue.size() == capacity);
	REQUIRE_FALSE(m->midiOuts.queue.empty());

}


TEST_CASE("MIDI output overflow is reported once per episode, not once per drop", "[MidiKit]") {
	ModuleScaffold mods;
	MidiKitModule* m = mods.create();
	midi::Message msg = noteOn(1, 60, 100);

	size_t capacity = m->midiOuts.queue.capacity();
	for (size_t i = 0; i < capacity; i++) {
		REQUIRE(m->sendMidi(0, &msg, 1, 0, 0));
	}
	// Several drops in the same episode — only one log line should result once
	// process() next runs and consumes the rising edge of out.overflow.
	REQUIRE_FALSE(m->sendMidi(0, &msg, 1, 0, 0));
	REQUIRE_FALSE(m->sendMidi(0, &msg, 1, 0, 0));
	REQUIRE_FALSE(m->sendMidi(0, &msg, 1, 0, 0));

	m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f);
	for (int64_t f = 0; f < 8; f++) {
		m->process(Test::makeProcessArgs(f));
	}
	auto entries = drainLogEntries(m);
	size_t count = 0;
	for (auto& e : entries) {
		if (std::get<1>(e).find("dropped") != std::string::npos) count++;
	}
	REQUIRE(count == 1);

}


TEST_CASE("MIDI output overflow is reported again after the queue recovers", "[MidiKit]") {
	// The flag is edge-triggered via exchange(false), so reporting once per
	// episode must not mean once per module lifetime: a later, separate
	// saturation has to log again. Pins that process() CLEARS the flag rather
	// than latching it.
	MidiKitModule* m = createModule();
	midi::Message msg = noteOn(1, 60, 100);

	auto fillAndOverflow = [&]() {
		while (m->midiOuts.queue.capacity() > 0) {
			REQUIRE(m->sendMidi(0, &msg, 1, 0, 0));
		}
		REQUIRE_FALSE(m->sendMidi(0, &msg, 1, 0, 0));
	};
	auto countDropLines = [&]() {
		size_t count = 0;
		for (auto& e : drainLogEntries(m)) {
			if (std::get<1>(e).find("dropped") != std::string::npos) count++;
		}
		return count;
	};

	fillAndOverflow();
	processOneDividerPeriod(m, 0);            // drains the queue, logs once
	REQUIRE(countDropLines() == 1);
	REQUIRE(m->midiOuts.queue.empty());

	// A quiet period with no drops must log nothing. This is what pins the
	// CLEARING of the flag: a latched flag would keep reporting here.
	processOneDividerPeriod(m, 8);
	REQUIRE(countDropLines() == 0);

	// Second, independent episode: reports again rather than staying silent
	// after the first — the flag re-arms.
	fillAndOverflow();
	processOneDividerPeriod(m, 16);
	REQUIRE(countDropLines() == 1);

	Test::destroyModule(m);
}


TEST_CASE("An NRPN group is dropped whole, never truncated, when free capacity is short", "[MidiKit]") {
	ModuleScaffold mods;
	// The all-or-nothing contract in sendMidi(): an NRPN is 4 messages sharing
	// one parameter change, and a partial group is a malformed parameter
	// change, worse than dropping it outright.
	MidiKitModule* m = mods.create();
	midi::Message msg = noteOn(1, 60, 100);

	// Leave exactly 3 free slots — one short of the 4-message group.
	size_t capacity = m->midiOuts.queue.capacity();
	for (size_t i = 0; i < capacity - 3; i++) {
		REQUIRE(m->sendMidi(0, &msg, 1, 0, 0));
	}
	REQUIRE(m->midiOuts.queue.capacity() == 3);

	midi::Message group[4] = {msg, msg, msg, msg};
	REQUIRE_FALSE(m->sendMidi(0, group, 4, 0, 0));
	// Rejected as a whole: the 3 free slots are still free, not partially
	// consumed by the first 3 messages of the group.
	REQUIRE(m->midiOuts.queue.capacity() == 3);

}


TEST_CASE("onUnload runs again when a second script replaces the first, in both engines", "[MidiKit][CrossEngine]") {
	ModuleScaffold mods;
	auto checkOnUnloadReplaced = [](const std::string& onUnloadScript, const std::string& replacementScript) {
		MidiKitModule* m = createModule();
		m->loadScript(onUnloadScript);
		drainLog(m);

		m->loadScript(replacementScript);

		std::string log = drainLog(m);
		Test::destroyModule(m);
		return log.find("onUnload ran") != std::string::npos;
	};

	REQUIRE(checkOnUnloadReplaced(JS_ON_UNLOAD, JS_NO_ON_LOAD) == true);
	REQUIRE(checkOnUnloadReplaced(LUA_ON_UNLOAD, LUA_NO_ON_LOAD) == true);
}


// rack.setConfig()/getConfig()
// Replaces the old rack.onSave()/rack.onLoad(persistedConfig) pull model: the
// script now PUSHES config via setConfig() whenever it changes something, and
// the engine just holds the latest published JSON — see
// var/MidiKit_config_redesign_plan.md. onUnload() is unaffected: still
// teardown-only, its return value still ignored, and it must not touch config
// on its own.

TEST_CASE("onUnload's return value is ignored on real teardown and does not touch published config, in both engines", "[MidiKit][CrossEngine]") {
	ModuleScaffold mods;
	auto check = [](const std::string& script) {
		MidiKitModule* m = createModule();
		m->loadScript(script);
		drainLog(m);

		// clearScript() tears the script down for real (the onUnload() path).
		// A save racing teardown must still see the last setConfig()'d value —
		// unloadScriptOnWorker() leaves publishedConfig untouched,
		// unlike workingConfig which is destroyed with the engine.
		std::string beforeUnload = publishedConfigJson(m->host.getActiveEngine());
		REQUIRE(configInt(beforeUnload, "real") == 42);

		m->clearScript();
		std::string log = drainLog(m);
		REQUIRE(log.find("onUnload ran") != std::string::npos);

		Test::destroyModule(m);
	};

	static const char* JS_ON_UNLOAD_SETS_CONFIG = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(midiPort, msg) {};
rack.setConfig("real", 42);
rack.onUnload = function() {
    rack.log("onUnload ran");
};
)";
	static const char* LUA_ON_UNLOAD_SETS_CONFIG = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg) end
rack.setConfig("real", 42)
rack.onUnload = function()
    rack.log("onUnload ran")
end
)";
	check(JS_ON_UNLOAD_SETS_CONFIG);
	check(LUA_ON_UNLOAD_SETS_CONFIG);
}
