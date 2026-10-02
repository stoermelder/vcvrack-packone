// Store capacity limits, scripts that clobber predefined objects, and concurrent config access.
//
// Part of the cross-engine suite: included into the __engine namespace by
// MidiKit.test.cpp after MidiKit.test.engine.hpp, which defines the shared helpers.

// Store cap (msgStoreDefault handles)
// midi.create()/midi.clone()/midi.createNRPN() fail once the per-callback
// store is full, aborting the rest of the callback. The error
// wording is identical in both engines (unified: "midi.create: message store
// full" etc.). Per A3, messages sent before the error have already gone out —
// a multi-message sequence can be emitted partially — while anything created
// after the error is dropped.

// Store size of a script without "@requires messages": counts below come from
// it, so the next change of the default needs no new numbers. Scripts carry
// "{N}" where a loop count goes.
static const int STORE = StoermelderPackOne::MidiScript::MidiScriptEngine::msgStoreDefault;

static std::string fillN(const char* script, int n) {
	std::string s = script;
	for (size_t pos; (pos = s.find("{N}")) != std::string::npos;) s.replace(pos, 3, std::to_string(n));
	return s;
}

static const char* JS_STORE_FULL = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(midiPort, msg) {
    // Two messages sent before the overflow — these have already gone out.
    let m1 = midi.create();
    midi.setNoteOn(m1, 1, 60, 100);
    midiOut.send(m1);
    let m2 = midi.create();
    midi.setCc(m2, 1, 20, 100);
    midiOut.send(m2);

    // Slot 0 is the incoming message (msgCount starts at 1) and m1/m2 above
    // each consume a slot, so this loop crosses the store's cap and
    // midi.create() throws mid-callback. The exact overflow point doesn't
    // matter — the point is that it throws here.
    for (let i = 0; i < {N}; i++) {
        midi.create();
    }

    // Never reached — the error above aborts the callback.
    let m3 = midi.create();
    midi.setNoteOn(m3, 1, 61, 100);
    midiOut.send(m3);
};
)";

static const char* LUA_STORE_FULL = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local m1 = midi.create()
    midi.setNoteOn(m1, 1, 60, 100)
    midiOut.send(m1)
    local m2 = midi.create()
    midi.setCc(m2, 1, 20, 100)
    midiOut.send(m2)

    for i = 1, {N} do
        midi.create()
    end

    local m3 = midi.create()
    midi.setNoteOn(m3, 1, 61, 100)
    midiOut.send(m3)
end
)";

TEST_CASE("midi.create past the store cap errors and keeps the sends made before it", "[MidiKit][CrossEngine]") {
	EngineResult js = run(fillN(JS_STORE_FULL, STORE + 1));
	EngineResult lua = run(fillN(LUA_STORE_FULL, STORE + 1));

	// Identical error wording in both engines (unified wording).
	CATCH_INFO("JS log:\n" << js.log);
	CATCH_INFO("Lua log:\n" << lua.log);
	REQUIRE(js.log.find("midi.create: message store full") != std::string::npos);
	REQUIRE(lua.log.find("midi.create: message store full") != std::string::npos);

	// Partial output (A3): both pre-error messages went out, identically, with
	// the exact bytes the scripts requested. size == 2 also proves the
	// message created after the error never went out.
	REQUIRE(js.sent.size() == 2);
	REQUIRE(lua.sent.size() == 2);
	REQUIRE(js.sent[0].bytes == lua.sent[0].bytes);
	REQUIRE(js.sent[1].bytes == lua.sent[1].bytes);
	REQUIRE(js.sent[0].bytes == std::vector<uint8_t>({0x90, 0x3c, 0x64})); // note-on ch1 note 60 vel 100
	REQUIRE(js.sent[1].bytes == std::vector<uint8_t>({0xb0, 0x14, 0x64})); // cc ch1 cc 20 val 100
}


// createNRPN/createCc14bit store-boundary
// Slot 0 of the store is the incoming message, so msgCount starts at 1 inside
// onMessage. createNRPN() needs 4 consecutive slots and createCc14bit() 2; the
// last valid starting positions are STORE - 4 and STORE - 2. The
// QuickJS bounds checks used >= instead of >, wrongly rejecting those last
// valid positions (Lua was already correct). These pin the boundary: at the
// last valid slot the calls succeed and emit spec-compliant bytes; one slot
// past, they error "store full".

static const char* JS_NRPN_AT_BOUNDARY = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    // The creates fill every slot but the last 4; createNRPN() fits in those.
    for (let i = 0; i < {N}; i++) {
        midi.create();
    }
    let nrpn = midi.createNRPN();
    midi.setNRPN(nrpn, 9, 1234, 5678);
    midiOut.send(nrpn);
};
)";

static const char* LUA_NRPN_AT_BOUNDARY = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    for i = 1, {N} do
        midi.create()
    end
    local nrpn = midi.createNRPN()
    midi.setNRPN(nrpn, 9, 1234, 5678)
    midiOut.send(nrpn)
end
)";

static const char* JS_CC14_AT_BOUNDARY = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    // The creates fill every slot but the last 2; createCc14bit() fits in those.
    for (let i = 0; i < {N}; i++) {
        midi.create();
    }
    let cc14 = midi.createCc14bit();
    midi.setCc14bit(cc14, 8, 1, 100.5);
    midiOut.send(cc14);
};
)";

static const char* LUA_CC14_AT_BOUNDARY = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    for i = 1, {N} do
        midi.create()
    end
    local cc14 = midi.createCc14bit()
    midi.setCc14bit(cc14, 8, 1, 100.5)
    midiOut.send(cc14)
end
)";

static const char* JS_NRPN_PAST_BOUNDARY = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    for (let i = 0; i < {N}; i++) {
        midi.create();
    }
    midi.createNRPN();   // needs 4 slots; only 3 are left
};
)";

static const char* LUA_NRPN_PAST_BOUNDARY = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    for i = 1, {N} do
        midi.create()
    end
    midi.createNRPN()
end
)";

static const char* JS_CC14_PAST_BOUNDARY = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    for (let i = 0; i < {N}; i++) {
        midi.create();
    }
    midi.createCc14bit();   // needs 2 slots; only 1 is left
};
)";

static const char* LUA_CC14_PAST_BOUNDARY = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    for i = 1, {N} do
        midi.create()
    end
    midi.createCc14bit()
end
)";

TEST_CASE("createNRPN at the last valid store slot succeeds in both engines", "[MidiKit][CrossEngine]") {
	EngineResult js = run(fillN(JS_NRPN_AT_BOUNDARY, STORE - 5));
	EngineResult lua = run(fillN(LUA_NRPN_AT_BOUNDARY, STORE - 5));
	CATCH_INFO("JS log:\n" << js.log);
	CATCH_INFO("Lua log:\n" << lua.log);
	REQUIRE(js.log.find("message store full") == std::string::npos);
	REQUIRE(lua.log.find("message store full") == std::string::npos);
	// ch 9, number=1234 -> CC99=9, CC98=82; value=5678 -> CC6=44, CC38=46
	std::vector<uint8_t> expect[4] = {{0xb8, 99, 9}, {0xb8, 98, 82}, {0xb8, 6, 44}, {0xb8, 38, 46}};
	REQUIRE(js.sent.size() == 4);
	REQUIRE(lua.sent.size() == 4);
	for (int i = 0; i < 4; i++) {
		REQUIRE(js.sent[i].bytes == expect[i]);
		REQUIRE(lua.sent[i].bytes == expect[i]);
	}
}

TEST_CASE("createCc14bit at the last valid store slot succeeds in both engines", "[MidiKit][CrossEngine]") {
	EngineResult js = run(fillN(JS_CC14_AT_BOUNDARY, STORE - 3));
	EngineResult lua = run(fillN(LUA_CC14_AT_BOUNDARY, STORE - 3));
	REQUIRE(js.log.find("message store full") == std::string::npos);
	REQUIRE(lua.log.find("message store full") == std::string::npos);
	// ch 8, cc=1 value=100.5 -> CC1=100 (MSB), CC33=64 (LSB)
	std::vector<uint8_t> m0 = {0xb7, 1, 100};
	std::vector<uint8_t> m1 = {0xb7, 33, 64};
	REQUIRE(js.sent.size() == 2);
	REQUIRE(lua.sent.size() == 2);
	REQUIRE(js.sent[0].bytes == m0);
	REQUIRE(js.sent[1].bytes == m1);
	REQUIRE(lua.sent[0].bytes == m0);
	REQUIRE(lua.sent[1].bytes == m1);
}

TEST_CASE("createNRPN one slot past the boundary errors in both engines", "[MidiKit][CrossEngine]") {
	EngineResult js = run(fillN(JS_NRPN_PAST_BOUNDARY, STORE - 4));
	EngineResult lua = run(fillN(LUA_NRPN_PAST_BOUNDARY, STORE - 4));
	REQUIRE(js.log.find("midi.createNRPN: message store full") != std::string::npos);
	REQUIRE(lua.log.find("midi.createNRPN: message store full") != std::string::npos);
	REQUIRE(js.sent.empty());
	REQUIRE(lua.sent.empty());
}

TEST_CASE("createCc14bit one slot past the boundary errors in both engines", "[MidiKit][CrossEngine]") {
	EngineResult js = run(fillN(JS_CC14_PAST_BOUNDARY, STORE - 2));
	EngineResult lua = run(fillN(LUA_CC14_PAST_BOUNDARY, STORE - 2));
	REQUIRE(js.log.find("midi.createCc14bit: message store full") != std::string::npos);
	REQUIRE(lua.log.find("midi.createCc14bit: message store full") != std::string::npos);
	REQUIRE(js.sent.empty());
	REQUIRE(lua.sent.empty());
}


// runtime API mutation: forbidden by contract, must not crash
// SCRIPTING.md documents that midi.onMessage/onLoad/onUnload and
// trig.onTrigger (and the predefined objects rack/midi/midiOut/trig/input/
// param/number) are
// resolved ONCE at load time; a script that reassigns any of them afterward,
// or defines a hook late (e.g. from inside onTrigger), has no effect on
// what runs — the engine keeps calling whatever was present at load. This
// section verifies that contract holds identically in both engines and that
// a script violating it (deliberately or by accident) degrades gracefully
// rather than crashing or diverging in observable behavior between engines.

// Feeds two incoming messages through the same loaded script and returns the
// log text after each dispatch separately, so a test can assert what
// happened on the first call vs. the second (e.g. "did a reassignment made
// during call 1 take effect on call 2").
struct TwoDispatchResult {
	std::string log1, log2;
};

static TwoDispatchResult runTwoMidiDispatches(const std::string& script) {
	MidiKitModule* m = createModule();
	m->loadScript(script);
	drainLog(m);

	TwoDispatchResult r;
	midi::Message in1 = noteOn(1, 60, 100);
	m->host.getActiveEngine()->processInMessage(0, in1);
	m->host.getActiveEngine()->process();
	r.log1 = drainLog(m);

	midi::Message in2 = noteOn(1, 61, 100);
	m->host.getActiveEngine()->processInMessage(0, in2);
	m->host.getActiveEngine()->process();
	r.log2 = drainLog(m);

	Test::destroyModule(m);
	return r;
}

static const char* JS_REASSIGN_ON_MIDI_MESSAGE = R"(/**
 * @engine QuickJs@v1
 */
let n = 0;
midi.onMessage = function(port, msg) {
    n++;
    rack.log("call " + n);
    if (n === 1) {
        // Reassignment must have no effect: dispatch already resolved and
        // cached the function above at load time.
        midi.onMessage = function(port, msg) {
            rack.log("REPLACED");
        };
    }
};
)";

static const char* LUA_REASSIGN_ON_MIDI_MESSAGE = R"(--[[
@engine minilua@v1
--]]
local n = 0
midi.onMessage = function(port, msg)
    n = n + 1
    rack.log("call " .. n)
    if n == 1 then
        midi.onMessage = function(port, msg)
            rack.log("REPLACED")
        end
    end
end
)";

TEST_CASE("Reassigning midi.onMessage after load has no effect, in both engines", "[MidiKit][CrossEngine]") {
	auto checkReassign = [](const std::string& script) {
		TwoDispatchResult r = runTwoMidiDispatches(script);
		REQUIRE(r.log1.find("call 1") != std::string::npos);
		REQUIRE(r.log2.find("call 2") != std::string::npos);
		REQUIRE(r.log2.find("REPLACED") == std::string::npos);
	};
	checkReassign(JS_REASSIGN_ON_MIDI_MESSAGE);
	checkReassign(LUA_REASSIGN_ON_MIDI_MESSAGE);
}

static const char* JS_ON_MIDI_MESSAGE_NONFUNC = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    rack.log("call");
    // Assigning a non-function must not affect dispatch: the cached function
    // reference keeps running regardless of what midi.onMessage is now.
    midi.onMessage = 42;
};
)";

static const char* LUA_ON_MIDI_MESSAGE_NONFUNC = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(port, msg)
    rack.log("call")
    midi.onMessage = 42
end
)";

TEST_CASE("Assigning a non-function to midi.onMessage does not break later dispatch, in both engines", "[MidiKit][CrossEngine]") {
	auto checkNonFunc = [](const std::string& script) {
		TwoDispatchResult r = runTwoMidiDispatches(script);
		REQUIRE(r.log1.find("call") != std::string::npos);
		REQUIRE(r.log1.find("rror") == std::string::npos);
		REQUIRE(r.log2.find("call") != std::string::npos);
		REQUIRE(r.log2.find("rror") == std::string::npos);
	};
	checkNonFunc(JS_ON_MIDI_MESSAGE_NONFUNC);
	checkNonFunc(LUA_ON_MIDI_MESSAGE_NONFUNC);
}

static const char* JS_RACK_CLOBBER = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    rack.log("call");
    rack = 42;
};
)";

static const char* LUA_RACK_CLOBBER = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(port, msg)
    rack.log("call")
    rack = 42
end
)";

TEST_CASE("Clobbering the global rack variable does not crash either engine", "[MidiKit][CrossEngine]") {
	// Unlike the two cases above, this one is NOT free of side effects: the
	// callback itself still holds a reference to the true rack object (it was
	// resolved once at load time), but the callback body reads the *global*
	// "rack" identifier fresh via rack.log(...) on the second call, and that
	// global was just overwritten with 42 on the first call. So the second
	// call errors — inside the script's own code, not in the engine's
	// dispatch mechanism — and neither engine crashes. Wording differs
	// (see #13/D6) but both engines must be equally non-silent here.
	auto checkClobber = [](const std::string& script) {
		TwoDispatchResult r = runTwoMidiDispatches(script);
		REQUIRE(r.log1.find("call") != std::string::npos);
		REQUIRE(r.log1.find("rror") == std::string::npos);
		REQUIRE(r.log2.find("rror") != std::string::npos);
	};
	checkClobber(JS_RACK_CLOBBER);
	checkClobber(LUA_RACK_CLOBBER);
}

static const char* JS_MIDIOUT_CLOBBER = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    midiOut = 42;
    let m = midi.create();
    midi.setNoteOn(m, 1, 60, 100);
    midiOut.send(m);
};
)";

static const char* LUA_MIDIOUT_CLOBBER = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(port, msg)
    midiOut = 42
    local m = midi.create()
    midi.setNoteOn(m, 1, 60, 100)
    midiOut.send(m)
end
)";

TEST_CASE("Clobbering the predefined midiOut object errors without crashing, in both engines", "[MidiKit][CrossEngine]") {
	// midiOut (unlike rack) is never cached by the engine — every midiOut.*
	// call already resolves it fresh each time, so this is a script clobbering
	// its own global and immediately paying for it, in both engines, on the
	// very first dispatch.
	auto checkClobber = [](const std::string& script) {
		TwoDispatchResult r = runTwoMidiDispatches(script);
		REQUIRE(r.log1.find("rror") != std::string::npos);
		REQUIRE(r.log2.find("rror") != std::string::npos);
	};
	checkClobber(JS_MIDIOUT_CLOBBER);
	checkClobber(LUA_MIDIOUT_CLOBBER);
}

static const char* JS_LATE_DEFINE_ON_MIDI_MESSAGE = R"(/**
 * @engine QuickJs@v1
 */
trig.enableIn(1);
trig.onTrigger = function(trigPort) {
    rack.log("onTrigger fired");
    // Defining midi.onMessage for the first time here, after load, must have
    // no effect: it did not exist when hooks were resolved at load time.
    midi.onMessage = function(port, msg) {
        rack.log("late midi.onMessage called");
    };
};
)";

static const char* LUA_LATE_DEFINE_ON_MIDI_MESSAGE = R"(--[[
@engine minilua@v1
--]]
trig.enableIn(1)
trig.onTrigger = function(trigPort)
    rack.log("onTrigger fired")
    midi.onMessage = function(port, msg)
        rack.log("late midi.onMessage called")
    end
end
)";

TEST_CASE("Defining midi.onMessage late (from onTrigger) never gets called, in both engines", "[MidiKit][CrossEngine]") {
	ModuleScaffold mods;
	auto checkLateDefine = [](const std::string& script) {
		MidiKitModule* m = createModule();
		m->loadScript(script);
		std::string loadLog = drainLog(m);
		// The load-time "no midi.onMessage" warning must fire in both engines:
		// the hook didn't exist when hooks were resolved at load time, even
		// though the script goes on to define it moments later.
		REQUIRE(loadLog.find("No midi.onMessage") != std::string::npos);

		m->host.getActiveEngine()->processInTick(0, 0);
		m->host.getActiveEngine()->process();
		std::string triggerLog = drainLog(m);
		REQUIRE(triggerLog.find("onTrigger fired") != std::string::npos);

		midi::Message in = noteOn(1, 60, 100);
		m->host.getActiveEngine()->processInMessage(0, in);
		m->host.getActiveEngine()->process();
		std::string midiLog = drainLog(m);
		REQUIRE(midiLog.find("late midi.onMessage called") == std::string::npos);

		Test::destroyModule(m);
	};
	checkLateDefine(JS_LATE_DEFINE_ON_MIDI_MESSAGE);
	checkLateDefine(LUA_LATE_DEFINE_ON_MIDI_MESSAGE);
}


// SCRIPTING.md claims both engines are "tested to degrade gracefully" when a
// script clobbers a predefined global at runtime, using "rack = 42" as its
// own example. That specific case — midi.onMessage = 42 at top level
// (before hooks are cached) — had no cross-engine test: "Clobbering the
// global rack variable" above clobbers rack *inside* midi.onMessage, after
// caching already succeeded, which exercises a different path (the script's
// own next statement erroring) than clobbering it beforehand at load time.
// This closes that gap for the literal case the doc promises coverage for.
static const char* JS_MIDI_NUMBER_AT_LOAD = R"(/**
 * @engine QuickJs@v1
 */
rack.onLoad = function(persisted) {
    rack.log("onLoad ran");
};
midi.onMessage = function(port, msg) {
    rack.log("call");
};
midi = 42;
)";

static const char* LUA_MIDI_NUMBER_AT_LOAD = R"(--[[
@engine minilua@v1
--]]
rack.onLoad = function(persisted)
    rack.log("onLoad ran")
end
midi.onMessage = function(port, msg)
    rack.log("call")
end
midi = 42
)";

TEST_CASE("Clobbering midi with a number at top-level load time does not crash either engine", "[MidiKit][CrossEngine]") {
	ModuleScaffold mods;
	auto checkNumberClobber = [](const std::string& script) {
		MidiKitModule* m = createModule();
		m->loadScript(script);
		std::string loadLog = drainLog(m);
		// midi is 42 (not an object) by the time hooks are resolved, so
		// midi.onMessage is not found — same "not defined" outcome as a script
		// that never assigned it.
		REQUIRE(loadLog.find("No midi.onMessage") != std::string::npos);

		midi::Message in = noteOn(1, 60, 100);
		m->host.getActiveEngine()->processInMessage(0, in);
		m->host.getActiveEngine()->process();
		std::string midiLog = drainLog(m);
		REQUIRE(midiLog.empty());

		// Reload a completely unrelated, valid script into the SAME module
		// afterward — the real assertion. Verifies nothing from the
		// number-clobbered load (a pending QuickJS exception, a stale Lua
		// registry ref, or anything else) corrupts state that would affect a
		// fresh, correctly-behaving script loaded right after.
		bool isJs = script.find("QuickJs") != std::string::npos;
		m->loadScript(isJs ? JS_REASSIGN_ON_MIDI_MESSAGE : LUA_REASSIGN_ON_MIDI_MESSAGE);
		drainLog(m);
		m->host.getActiveEngine()->processInMessage(0, in);
		m->host.getActiveEngine()->process();
		std::string reloadMidiLog = drainLog(m);
		REQUIRE(reloadMidiLog.find("call 1") != std::string::npos);

		Test::destroyModule(m);
	};
	checkNumberClobber(JS_MIDI_NUMBER_AT_LOAD);
	checkNumberClobber(LUA_MIDI_NUMBER_AT_LOAD);
}


// Regression test for a QuickJS-only bug: a script that clobbers the global
// "midi" binding with null/undefined during its own top-level code (before
// loadScript() gets to cache midi/onMessage) used to make
// cacheCallableProp()'s JS_GetPropertyStr throw a TypeError on ctx and leave
// it pending, uncleared, unconsumed by any JS_Call site since they all gate
// on JS_IsFunction first and skip the call rather than surface the
// exception. Fixed by only resolving hooks when midiObj is JS_IsObject;
// midi.onMessage stays JS_UNDEFINED (same end state) without ever touching
// JS_GetPropertyStr on a null/undefined receiver.
static const char* JS_MIDI_NULL_AT_LOAD = R"(/**
 * @engine QuickJs@v1
 */
rack.onLoad = function(persisted) {
    rack.log("onLoad ran");
};
midi.onMessage = function(port, msg) {
    rack.log("call");
};
midi = null;
)";

TEST_CASE("Clobbering midi with null during top-level load code does not leave a pending exception (QuickJS)", "[MidiKit][QuickJs]") {
	ModuleScaffold mods;
	MidiKitModule* m = mods.create();
	m->loadScript(JS_MIDI_NULL_AT_LOAD);
	std::string loadLog = drainLog(m);
	// midi is null by the time hooks are resolved (top-level code, including
	// "midi = null;", runs to completion before caching happens) — so
	// midi.onMessage is not found, matching the same "not defined" outcome a
	// script that never assigned it would get.
	REQUIRE(loadLog.find("No midi.onMessage") != std::string::npos);

	midi::Message in = noteOn(1, 60, 100);
	m->host.getActiveEngine()->processInMessage(0, in);
	m->host.getActiveEngine()->process();
	std::string midiLog = drainLog(m);
	REQUIRE(midiLog.empty());

	// The real assertion: reload a completely unrelated, valid script into
	// the SAME module afterward. If the first load's null-midi lookups had
	// left a pending exception corrupting ctx, this would be where it
	// surfaces — a fresh JS_Call site (this script's own midi.onMessage)
	// running for the first time on that ctx.
	m->loadScript(JS_REASSIGN_ON_MIDI_MESSAGE);
	drainLog(m);
	m->host.getActiveEngine()->processInMessage(0, in);
	m->host.getActiveEngine()->process();
	std::string reloadMidiLog = drainLog(m);
	REQUIRE(reloadMidiLog.find("call 1") != std::string::npos);

}


// the concurrency case the redesign makes trivially safe
// Under the old rack.onSave() design, dataToJson() had to reach INTO the
// interpreter via runSync() — a real cross-thread call racing whatever the
// worker was doing, bounded by a 500ms timeout that could time out under load.
// Under this design dataToJson() only ever reads publishedConfig.peek() (a
// SpscLatestValue): the script thread publishes an immutable, fully-
// formed json_t* on every setConfig(), so there is no partial state a reader
// can observe and nothing to race against the interpreter for. This test
// pins that invariant directly: hammer setConfig() from the script thread
// while dataToJson() runs concurrently from this thread (standing in for the
// UI thread, which is where Rack actually calls it), and require every
// result to be a complete, valid JSON object — never partial, never a crash.
// Run this test binary under SANITIZER=thread (make testrun SANITIZER=thread)
// for TSan coverage; the assertions below hold under
// any sanitizer, but only TSan can catch a torn/racing publish.

static const char* JS_HAMMER_SETCONFIG = R"(/**
 * @engine QuickJs@v1
 */
trig.enableIn(1);
midi.onMessage = function(midiPort, msg) {};
let counter = 0;
trig.onTrigger = function(trigPort) {
    counter++;
    rack.setConfig("counter", counter);
    rack.setConfig("nested", { a: counter, b: [counter, counter + 1] });
};
)";

static const char* LUA_HAMMER_SETCONFIG = R"(--[[
@engine minilua@v1
--]]
trig.enableIn(1)
midi.onMessage = function(midiPort, msg) end
local counter = 0
trig.onTrigger = function(trigPort)
    counter = counter + 1
    rack.setConfig("counter", counter)
    rack.setConfig("nested", { a = counter, b = { counter, counter + 1 } })
end
)";

TEST_CASE("dataToJson() concurrent with setConfig() on the worker thread always sees a complete, valid config, in both engines", "[MidiKit][CrossEngine][Async][TSan]") {
	ModuleScaffold mods;
	auto check = [](const std::string& script) {
		auto worker = asyncWorker();
		MidiKitModule* m = createModule(worker);
		m->loadScript(script);
		barrier(worker);
		drainLog(m);

		// Producer: the script thread. A real worker thread (not
		// SyncTaskWorker) drives trig.onTrigger -> rack.setConfig() in a tight
		// loop via the module's normal audio-thread-facing entry points
		// (queueTick + process()), exactly like real trigger input traffic.
		std::atomic<bool> stop{false};
		std::thread producer([&]() {
			while (!stop.load(std::memory_order_relaxed)) {
				m->host.queueTick(0, 0);
				m->host.process();
			}
		});

		// Consumer: this thread, standing in for Rack's UI thread, calls
		// dataToJson() — a plain peekConfig() read — in a tight loop
		// concurrently with the producer above.
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
		int iterations = 0;
		while (std::chrono::steady_clock::now() < deadline) {
			json_t* rootJ = m->dataToJson();
			REQUIRE(rootJ != nullptr);
			REQUIRE(json_is_object(rootJ));
			// "scriptConfig" is present once the first setConfig() has landed;
			// absent briefly at the very start is fine (nothing published
			// yet), but whenever present it must be a COMPLETE object, never
			// a torn/partial one — the read is peek()'d and the published
			// object is immutable, so this must never trip.
			json_t* configJ = json_object_get(rootJ, "scriptConfig");
			if (configJ) {
				REQUIRE(json_is_object(configJ));
				// If "counter" is there, "nested" must be too (and vice
				// versa): setConfig()'s two calls per callback either both
				// landed in this published snapshot or neither did — there is
				// no publish state where only one is visible, since a whole
				// new copy is published after each setConfigValue() call and
				// a reader only ever sees one complete generation.
				json_t* counterJ = json_object_get(configJ, "counter");
				json_t* nestedJ = json_object_get(configJ, "nested");
				if (counterJ) {
					REQUIRE(json_is_integer(counterJ));
				}
				if (nestedJ) {
					REQUIRE(json_is_object(nestedJ));
					json_t* aJ = json_object_get(nestedJ, "a");
					json_t* bJ = json_object_get(nestedJ, "b");
					REQUIRE(aJ != nullptr);
					REQUIRE(json_is_integer(aJ));
					REQUIRE(bJ != nullptr);
					REQUIRE(json_is_array(bJ));
					REQUIRE(json_array_size(bJ) == 2);
				}
			}
			json_decref(rootJ);
			iterations++;
		}
		// Sanity: the producer actually got to run enough to be a real test,
		// not zero iterations racing a slow CI box.
		REQUIRE(iterations > 0);

		stop.store(true, std::memory_order_relaxed);
		producer.join();
		barrier(worker);   // drain whatever the producer's last loop queued
		Test::destroyModule(m);
	};
	check(JS_HAMMER_SETCONFIG);
	check(LUA_HAMMER_SETCONFIG);
}


// ── Sending at call time, and handle lifetime ────────────────────────────────
// midiOut.send*() copies the message and sends it right away, so a handle keeps
// its contents and can be sent again. A handle is only valid inside the
// callback that created it: using one from an earlier callback is an error,
// not an alias of whatever the new callback built at the same slot.

// Feeds `count` Note-Ons (notes 60, 61, ...) one callback at a time and returns
// everything sent plus the log.
static EngineResult runMessages(const std::string& script, int count) {
	MidiKitModule* m = createModule();
	m->loadScript(script);

	EngineResult r;
	r.loadLog = drainLog(m);
	CATCH_INFO("load log:\n" << r.loadLog);
	REQUIRE(r.loadLog.find("rror") == std::string::npos);

	for (int i = 0; i < count; i++) {
		midi::Message in = noteOn(1, 60 + i, 100);
		m->host.getActiveEngine()->processInMessage(0, in);
		m->host.getActiveEngine()->process();
	}
	int port, ticks;
	midi::Message out;
	while (processOutMessage(m, port, out, ticks)) {
		r.sent.push_back(toSent(port, ticks, out));
	}
	r.log = drainLog(m);
	Test::destroyModule(m);
	return r;
}

static const char* JS_SEND_TWICE = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let m = midi.create();
    midi.setNoteOn(m, 1, 60, 100);
    midiOut.send(m);
    midiOut.send(m);
};
)";

static const char* LUA_SEND_TWICE = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(port, msg)
    local m = midi.create()
    midi.setNoteOn(m, 1, 60, 100)
    midiOut.send(m)
    midiOut.send(m)
end
)";

TEST_CASE("Sending the same handle twice sends two identical messages in both engines", "[MidiKit][CrossEngine]") {
	EngineResult js = run(JS_SEND_TWICE);
	EngineResult lua = run(LUA_SEND_TWICE);
	REQUIRE(js.sent.size() == 2);
	REQUIRE(js.sent[0].bytes == js.sent[1].bytes);
	requireEquivalent(js, lua);
}

static const char* JS_CHANGE_AFTER_SEND = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let m = midi.create();
    midi.setNoteOn(m, 1, 60, 100);
    midiOut.send(m);
    midi.setNote(m, 62);
    midiOut.send(m);
};
)";

static const char* LUA_CHANGE_AFTER_SEND = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(port, msg)
    local m = midi.create()
    midi.setNoteOn(m, 1, 60, 100)
    midiOut.send(m)
    midi.setNote(m, 62)
    midiOut.send(m)
end
)";

TEST_CASE("A change after send() does not affect what was sent, in both engines", "[MidiKit][CrossEngine]") {
	EngineResult js = run(JS_CHANGE_AFTER_SEND);
	EngineResult lua = run(LUA_CHANGE_AFTER_SEND);
	for (const EngineResult* r : { &js, &lua }) {
		REQUIRE(r->sent.size() == 2);
		REQUIRE(r->sent[0].bytes == std::vector<uint8_t>({0x90, 60, 100}));
		REQUIRE(r->sent[1].bytes == std::vector<uint8_t>({0x90, 62, 100}));
	}
}

static const char* JS_NRPN_TWICE = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let n = midi.createNRPN();
    midi.setNRPN(n, 1, 1, 100);
    midiOut.send(n);
    midiOut.send(n);
};
)";

static const char* LUA_NRPN_TWICE = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(port, msg)
    local n = midi.createNRPN()
    midi.setNRPN(n, 1, 1, 100)
    midiOut.send(n)
    midiOut.send(n)
end
)";

TEST_CASE("An NRPN leader sent twice sends two complete quads in both engines", "[MidiKit][CrossEngine]") {
	EngineResult js = run(JS_NRPN_TWICE);
	EngineResult lua = run(LUA_NRPN_TWICE);
	REQUIRE(js.sent.size() == 8);
	for (size_t i = 0; i < 4; i++) REQUIRE(js.sent[i].bytes == js.sent[i + 4].bytes);
	requireEquivalent(js, lua);
}

static const char* JS_REUSE_HANDLE = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let m = midi.create();
    midi.setNoteOn(m, 1, 60, 100);
    for (let i = 0; i < 200; i++) midiOut.send(m);
};
)";

static const char* LUA_REUSE_HANDLE = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(port, msg)
    local m = midi.create()
    midi.setNoteOn(m, 1, 60, 100)
    for i = 1, 200 do midiOut.send(m) end
end
)";

TEST_CASE("One handle can be sent past the store cap in both engines", "[MidiKit][CrossEngine]") {
	// The cap limits distinct live messages, not messages sent. The out queue
	// bounds the output instead: what doesn't fit is dropped and logged.
	EngineResult js = run(JS_REUSE_HANDLE);
	EngineResult lua = run(LUA_REUSE_HANDLE);
	for (const EngineResult* r : { &js, &lua }) {
		REQUIRE(r->log.find("store full") == std::string::npos);
		REQUIRE(r->sent.size() >= 100);
	}
	REQUIRE(js.sent.size() == lua.sent.size());
}

// Handle lifetime

static const char* JS_STALE_HANDLE = R"(/**
 * @engine QuickJs@v1
 */
let kept = -1;
midi.onMessage = function(port, msg) {
    if (kept < 0) {
        let m = midi.create();
        midi.setNoteOn(m, 1, 60, 100);
        kept = m;
        midiOut.send(m);
    }
    else {
        // Same slot as `kept` had in the first callback.
        let other = midi.create();
        midi.setNoteOn(other, 1, 99, 100);
        midiOut.send(kept);
    }
};
)";

static const char* LUA_STALE_HANDLE = R"(--[[
@engine minilua@v1
--]]
local kept = -1
midi.onMessage = function(port, msg)
    if kept < 0 then
        local m = midi.create()
        midi.setNoteOn(m, 1, 60, 100)
        kept = m
        midiOut.send(m)
    else
        local other = midi.create()
        midi.setNoteOn(other, 1, 99, 100)
        midiOut.send(kept)
    end
end
)";

TEST_CASE("A handle from an earlier callback is an error, not an alias, in both engines", "[MidiKit][CrossEngine]") {
	for (const char* script : { JS_STALE_HANDLE, LUA_STALE_HANDLE }) {
		CATCH_INFO(script);
		EngineResult r = runMessages(script, 2);
		CATCH_INFO("log:\n" << r.log);
		REQUIRE(r.log.find("onMessage error") != std::string::npos);
		// Only the first callback's message went out.
		REQUIRE(r.sent.size() == 1);
		REQUIRE(r.sent[0].bytes == std::vector<uint8_t>({0x90, 60, 100}));
	}
}

static const char* JS_ONLOAD_HANDLE = R"(/**
 * @engine QuickJs@v1
 */
let h = -1;
rack.onLoad = function() {
    h = midi.create();
    midi.setNoteOn(h, 1, 60, 100);
};
midi.onMessage = function(port, msg) {
    midiOut.send(h);
};
)";

static const char* LUA_ONLOAD_HANDLE = R"(--[[
@engine minilua@v1
--]]
local h = -1
rack.onLoad = function()
    h = midi.create()
    midi.setNoteOn(h, 1, 60, 100)
end
midi.onMessage = function(port, msg)
    midiOut.send(h)
end
)";

TEST_CASE("A handle stored in onLoad can't be used in onMessage in either engine", "[MidiKit][CrossEngine]") {
	for (const char* script : { JS_ONLOAD_HANDLE, LUA_ONLOAD_HANDLE }) {
		CATCH_INFO(script);
		EngineResult r = runMessages(script, 1);
		REQUIRE(r.log.find("onMessage error") != std::string::npos);
		REQUIRE(r.sent.empty());
	}
}

static const char* JS_INCOMING_HANDLE = R"(/**
 * @engine QuickJs@v1
 */
let prev = -1;
midi.onMessage = function(port, msg) {
    if (prev >= 0) midiOut.send(prev);
    prev = msg;
};
)";

static const char* LUA_INCOMING_HANDLE = R"(--[[
@engine minilua@v1
--]]
local prev = -1
midi.onMessage = function(port, msg)
    if prev >= 0 then midiOut.send(prev) end
    prev = msg
end
)";

TEST_CASE("The incoming message handle of one onMessage is invalid in the next, in both engines", "[MidiKit][CrossEngine]") {
	for (const char* script : { JS_INCOMING_HANDLE, LUA_INCOMING_HANDLE }) {
		CATCH_INFO(script);
		EngineResult r = runMessages(script, 2);
		REQUIRE(r.log.find("onMessage error") != std::string::npos);
		REQUIRE(r.sent.empty());
	}
}

static const char* JS_GETNAME_CREATE = R"(/**
 * @engine QuickJs@v1
 */
param.getName = function(i) {
    if (i === 1) {
        let m = midi.create();
        return "A";
    }
    return "B";
};
)";

static const char* LUA_GETNAME_CREATE = R"(--[[
@engine minilua@v1
--]]
param.getName = function(i)
    if i == 1 then
        local m = midi.create()
        return "A"
    end
    return "B"
end
)";

TEST_CASE("midi.create inside param.getName raises and the name falls back, in both engines", "[MidiKit][CrossEngine]") {
	for (const char* script : { JS_GETNAME_CREATE, LUA_GETNAME_CREATE }) {
		CATCH_INFO(script);
		MidiKitModule* m = createModule();
		m->loadScript(script);
		drainLog(m);
		REQUIRE(m->host.getActiveEngine()->getParamName(0) == "");
		REQUIRE(m->host.getActiveEngine()->getParamName(1) == "B");
		Test::destroyModule(m);
	}
}
