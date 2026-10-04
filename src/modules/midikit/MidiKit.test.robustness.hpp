// Store capacity limits, scripts that clobber predefined objects, and concurrent config access.
//
// Part of the cross-engine suite: the shared helpers (run, requireEquivalent, EngineRun, ...)
// are in MidiKit.test.hpp.

// Store cap (msgStoreDefault handles)
// midi.create()/midi.clone()/midi.createNRPN() fail once the per-callback
// store is full, aborting the rest of the callback. The error
// wording is identical in both engines (unified: "midi.create: message store
// full" etc.). Messages sent before the error have already gone out —
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
	EngineRun js = run(fillN(JS_STORE_FULL, STORE + 1));
	EngineRun lua = run(fillN(LUA_STORE_FULL, STORE + 1));

	// Identical error wording in both engines (unified wording).
	CATCH_INFO("JS log:\n" << js.log);
	CATCH_INFO("Lua log:\n" << lua.log);
	REQUIRE(js.log.find("midi.create: message store full") != std::string::npos);
	REQUIRE(lua.log.find("midi.create: message store full") != std::string::npos);

	// Partial output: both pre-error messages went out, identically, with
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
    midi.setCc14bit(cc14, 8, 1, 12864);
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
    midi.setCc14bit(cc14, 8, 1, 12864)
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
	EngineRun js = run(fillN(JS_NRPN_AT_BOUNDARY, STORE - 5));
	EngineRun lua = run(fillN(LUA_NRPN_AT_BOUNDARY, STORE - 5));
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
	EngineRun js = run(fillN(JS_CC14_AT_BOUNDARY, STORE - 3));
	EngineRun lua = run(fillN(LUA_CC14_AT_BOUNDARY, STORE - 3));
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
	EngineRun js = run(fillN(JS_NRPN_PAST_BOUNDARY, STORE - 4));
	EngineRun lua = run(fillN(LUA_NRPN_PAST_BOUNDARY, STORE - 4));
	REQUIRE(js.log.find("midi.createNRPN: message store full") != std::string::npos);
	REQUIRE(lua.log.find("midi.createNRPN: message store full") != std::string::npos);
	REQUIRE(js.sent.empty());
	REQUIRE(lua.sent.empty());
}

TEST_CASE("createCc14bit one slot past the boundary errors in both engines", "[MidiKit][CrossEngine]") {
	EngineRun js = run(fillN(JS_CC14_PAST_BOUNDARY, STORE - 2));
	EngineRun lua = run(fillN(LUA_CC14_PAST_BOUNDARY, STORE - 2));
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
	Kit<> kit;
	MidiKitModule* m = kit.m;
	m->loadScript(script);
	drainLog(m);

	TwoDispatchResult r;
	midi::Message in1 = noteOn(1, 60, 100);
	m->host.getActiveEngine()->processInMessage(0, QueuedMessage(in1));
	m->host.getActiveEngine()->process();
	r.log1 = drainLog(m);

	midi::Message in2 = noteOn(1, 61, 100);
	m->host.getActiveEngine()->processInMessage(0, QueuedMessage(in2));
	m->host.getActiveEngine()->process();
	r.log2 = drainLog(m);

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
	// but both engines must be equally non-silent here.
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
	auto checkLateDefine = [](const std::string& script) {
		Kit<> kit;
		MidiKitModule* m = kit.m;
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
		m->host.getActiveEngine()->processInMessage(0, QueuedMessage(in));
		m->host.getActiveEngine()->process();
		std::string midiLog = drainLog(m);
		REQUIRE(midiLog.find("late midi.onMessage called") == std::string::npos);

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
	auto checkNumberClobber = [](const std::string& script) {
		Kit<> kit;
		MidiKitModule* m = kit.m;
		m->loadScript(script);
		std::string loadLog = drainLog(m);
		// midi is 42 (not an object) by the time hooks are resolved, so
		// midi.onMessage is not found — same "not defined" outcome as a script
		// that never assigned it.
		REQUIRE(loadLog.find("No midi.onMessage") != std::string::npos);

		midi::Message in = noteOn(1, 60, 100);
		m->host.getActiveEngine()->processInMessage(0, QueuedMessage(in));
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
		m->host.getActiveEngine()->processInMessage(0, QueuedMessage(in));
		m->host.getActiveEngine()->process();
		std::string reloadMidiLog = drainLog(m);
		REQUIRE(reloadMidiLog.find("call 1") != std::string::npos);

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
	Kit<> kit;
	MidiKitModule* m = kit.m;
	m->loadScript(JS_MIDI_NULL_AT_LOAD);
	std::string loadLog = drainLog(m);
	// midi is null by the time hooks are resolved (top-level code, including
	// "midi = null;", runs to completion before caching happens) — so
	// midi.onMessage is not found, matching the same "not defined" outcome a
	// script that never assigned it would get.
	REQUIRE(loadLog.find("No midi.onMessage") != std::string::npos);

	midi::Message in = noteOn(1, 60, 100);
	m->host.getActiveEngine()->processInMessage(0, QueuedMessage(in));
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
	m->host.getActiveEngine()->processInMessage(0, QueuedMessage(in));
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
	auto check = [](const std::string& script) {
		auto worker = asyncWorker();
		Kit<> kit(worker);
		MidiKitModule* m = kit.m;
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
static EngineRun runMessages(const std::string& script, int count) {
	Kit<> kit;
	MidiKitModule* m = kit.m;
	m->loadScript(script);

	EngineRun r;
	r.loadLog = drainLog(m);
	CATCH_INFO("load log:\n" << r.loadLog);
	REQUIRE(r.loadLog.find("rror") == std::string::npos);

	for (int i = 0; i < count; i++) {
		midi::Message in = noteOn(1, 60 + i, 100);
		m->host.getActiveEngine()->processInMessage(0, QueuedMessage(in));
		m->host.getActiveEngine()->process();
	}
	int port, ticks;
	midi::Message out;
	while (processOutMessage(m, port, out, ticks)) {
		r.sent.push_back(Out::of(out, port, ticks));
	}
	r.log = drainLog(m);
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
	EngineRun js = run(JS_SEND_TWICE);
	EngineRun lua = run(LUA_SEND_TWICE);
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
	EngineRun js = run(JS_CHANGE_AFTER_SEND);
	EngineRun lua = run(LUA_CHANGE_AFTER_SEND);
	for (const EngineRun* r : { &js, &lua }) {
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
	EngineRun js = run(JS_NRPN_TWICE);
	EngineRun lua = run(LUA_NRPN_TWICE);
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
	EngineRun js = run(JS_REUSE_HANDLE);
	EngineRun lua = run(LUA_REUSE_HANDLE);
	for (const EngineRun* r : { &js, &lua }) {
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
	Pair scripts{JS_STALE_HANDLE, LUA_STALE_HANDLE};
	FOR_EACH_LANG;
	const char* script = scripts.get(lang);
	CATCH_INFO(script);
	EngineRun r = runMessages(script, 2);
	CATCH_INFO("log:\n" << r.log);
	REQUIRE(r.log.find("onMessage error") != std::string::npos);
	// Only the first callback's message went out.
	REQUIRE(r.sent.size() == 1);
	REQUIRE(r.sent[0].bytes == std::vector<uint8_t>({0x90, 60, 100}));
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
	Pair scripts{JS_ONLOAD_HANDLE, LUA_ONLOAD_HANDLE};
	FOR_EACH_LANG;
	const char* script = scripts.get(lang);
	CATCH_INFO(script);
	EngineRun r = runMessages(script, 1);
	REQUIRE(r.log.find("onMessage error") != std::string::npos);
	REQUIRE(r.sent.empty());
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
	Pair scripts{JS_INCOMING_HANDLE, LUA_INCOMING_HANDLE};
	FOR_EACH_LANG;
	const char* script = scripts.get(lang);
	CATCH_INFO(script);
	EngineRun r = runMessages(script, 2);
	REQUIRE(r.log.find("onMessage error") != std::string::npos);
	REQUIRE(r.sent.empty());
}

static const char* JS_GETNAME_CREATE = R"(/**
 * @engine QuickJs@v1
 */
param.onTooltip = function(i) {
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
param.onTooltip = function(i)
    if i == 1 then
        local m = midi.create()
        return "A"
    end
    return "B"
end
)";

TEST_CASE("midi.create inside param.onTooltip raises and the name falls back, in both engines", "[MidiKit][CrossEngine]") {
	Pair scripts{JS_GETNAME_CREATE, LUA_GETNAME_CREATE};
	FOR_EACH_LANG;
	const char* script = scripts.get(lang);
	CATCH_INFO(script);
	Kit<> kit;
	MidiKitModule* m = kit.m;
	m->loadScript(script);
	drainLog(m);
	REQUIRE(m->host.getActiveEngine()->getParamName(0) == "");
	REQUIRE(m->host.getActiveEngine()->getParamName(1) == "B");
}

template <typename MODULE>
static std::string loadAndDrainLog(const std::string& script) {
	Kit<MODULE> kit;
	MODULE* m = kit.m;
	m->loadScript(script);
	std::string log;
	ScriptLog::Entry t;
	while (m->log.tryPop(t)) log += std::get<2>(t) + "\n";
	return log;
}

TEST_CASE("@requires params refuses scripts the variant can't run", "[MidiKit][Micro]") {
	bool lua = GENERATE(true, false);
	CATCH_INFO(std::string(lua ? "Lua" : "JS"));

	// Micro has 2 params.
	std::string ok = loadAndDrainLog<MidiKitMicroModule>(requiresScript(lua, "params=2"));
	REQUIRE(ok.find("onload-ran") != std::string::npos);

	std::string tooMany = loadAndDrainLog<MidiKitMicroModule>(requiresScript(lua, "params=4"));
	REQUIRE(tooMany.find("onload-ran") == std::string::npos);
	REQUIRE(tooMany.find("requires 4 params, this module has 2") != std::string::npos);

	// The same script loads on the full module, which has at least 4.
	std::string full = loadAndDrainLog<MidiKitModule>(requiresScript(lua, "params=4"));
	REQUIRE(full.find("onload-ran") != std::string::npos);

	// Unverifiable requirements refuse rather than assume.
	std::string unknown = loadAndDrainLog<MidiKitModule>(requiresScript(lua, "api=2"));
	REQUIRE(unknown.find("onload-ran") == std::string::npos);
	REQUIRE(unknown.find("unknown @requires key") != std::string::npos);
	std::string bad = loadAndDrainLog<MidiKitModule>(requiresScript(lua, "params=x"));
	REQUIRE(bad.find("onload-ran") == std::string::npos);
	REQUIRE(bad.find("invalid @requires value") != std::string::npos);
}


// @requires messages=N: the store holds max(N, default) handles, refused beyond
// the maximum. onLoad starts with an empty store, so `count` creates there fill
// exactly `count` slots.
static std::string storeScript(bool lua, const std::string& tag, int count) {
	std::string n = std::to_string(count);
	std::string body = lua ? "rack.onLoad = function() for i = 1, " + n + " do midi.create() end rack.log('creates-ok') end"
	                       : "rack.onLoad = function() { for (let i = 0; i < " + n + "; i++) midi.create(); rack.log('creates-ok'); };";
	std::string requires = tag.empty() ? "" : (lua ? "@requires " + tag + "\n" : " * @requires " + tag + "\n");
	if (lua) return "--[[\n@engine minilua@v1\n" + requires + "--]]\n" + body + "\n";
	return "/**\n * @engine QuickJs@v1\n" + requires + " */\n" + body + "\n";
}

// Whether `count` creates fit in the store a script with `tag` gets.
static bool storeFits(bool lua, const std::string& tag, int count) {
	std::string log = loadAndDrainLog<MidiKitModule>(storeScript(lua, tag, count));
	return log.find("creates-ok") != std::string::npos && log.find("message store full") == std::string::npos;
}

TEST_CASE("@requires messages sizes the message store", "[MidiKit][CrossEngine]") {
	bool lua = GENERATE(true, false);
	CATCH_INFO(std::string(lua ? "Lua" : "JS"));
	using StoermelderPackOne::MidiScript::MidiScriptEngine;
	const int def = MidiScriptEngine::msgStoreDefault;
	const int max = MidiScriptEngine::msgStoreMax;

	// No tag: the default.
	REQUIRE(storeFits(lua, "", def));
	REQUIRE(!storeFits(lua, "", def + 1));

	// A minimum, not an exact size: smaller values keep the default.
	REQUIRE(storeFits(lua, "messages=16", def));
	REQUIRE(!storeFits(lua, "messages=16", def + 1));
	REQUIRE(storeFits(lua, "messages=64", 64));
	REQUIRE(!storeFits(lua, "messages=64", 65));
	REQUIRE(storeFits(lua, "messages=512", 512));
	REQUIRE(!storeFits(lua, "messages=512", 513));
	REQUIRE(storeFits(lua, "messages=" + std::to_string(max), max));

	// The store-full error names the fix.
	std::string log = loadAndDrainLog<MidiKitModule>(storeScript(lua, "", def + 1));
	REQUIRE(log.find("message store full (32 handles; reuse a handle or raise it with @requires messages=N)") != std::string::npos);
}

// The chain creators need 4 (NRPN/RPN) or 2 (14-bit CC) consecutive slots, so
// their last valid position depends on the store size.
TEST_CASE("Chain creators fit exactly at the end of a larger store", "[MidiKit][CrossEngine]") {
	bool lua = GENERATE(true, false);
	CATCH_INFO(std::string(lua ? "Lua" : "JS"));
	const int size = 512;
	auto fits = [&](const char* create, int before) {
		std::string n = std::to_string(before);
		std::string body = lua ? "rack.onLoad = function() for i = 1, " + n + " do midi.create() end midi." + create + "() rack.log('creates-ok') end"
		                       : "rack.onLoad = function() { for (let i = 0; i < " + n + "; i++) midi.create(); midi." + create + "(); rack.log('creates-ok'); };";
		std::string script = lua ? "--[[\n@engine minilua@v1\n@requires messages=512\n--]]\n" + body + "\n"
		                         : "/**\n * @engine QuickJs@v1\n * @requires messages=512\n */\n" + body + "\n";
		std::string log = loadAndDrainLog<MidiKitModule>(script);
		return log.find("creates-ok") != std::string::npos && log.find("message store full") == std::string::npos;
	};
	REQUIRE(fits("createNRPN", size - 4));
	REQUIRE(!fits("createNRPN", size - 3));
	REQUIRE(fits("createRPN", size - 4));
	REQUIRE(!fits("createRPN", size - 3));
	REQUIRE(fits("createCc14bit", size - 2));
	REQUIRE(!fits("createCc14bit", size - 1));
}

TEST_CASE("@requires messages refuses what it can't give", "[MidiKit][CrossEngine]") {
	bool lua = GENERATE(true, false);
	CATCH_INFO(std::string(lua ? "Lua" : "JS"));

	std::string tooBig = loadAndDrainLog<MidiKitModule>(storeScript(lua, "messages=5000", 1));
	REQUIRE(tooBig.find("Script not loaded: @requires messages=5000 exceeds the maximum of 512") != std::string::npos);
	REQUIRE(tooBig.find("creates-ok") == std::string::npos);
	REQUIRE(loadAndDrainLog<MidiKitModule>(storeScript(lua, "messages=4097", 1)).find("exceeds the maximum") != std::string::npos);

	for (const char* bad : {"messages=-1", "messages=abc", "messages="}) {
		CATCH_INFO(bad);
		std::string log = loadAndDrainLog<MidiKitModule>(storeScript(lua, bad, 1));
		REQUIRE(log.find("invalid @requires value") != std::string::npos);
		REQUIRE(log.find("creates-ok") == std::string::npos);
	}
}

TEST_CASE("@requires params and messages combine in one tag", "[MidiKit][CrossEngine]") {
	bool lua = GENERATE(true, false);
	CATCH_INFO(std::string(lua ? "Lua" : "JS"));

	REQUIRE(storeFits(lua, "params=4 messages=512", 512));
	REQUIRE(!storeFits(lua, "params=4 messages=512", 513));

	// A 2-param variant still refuses the script for params.
	std::string micro = loadAndDrainLog<MidiKitMicroModule>(storeScript(lua, "params=4 messages=512", 1));
	REQUIRE(micro.find("requires 4 params, this module has 2") != std::string::npos);
	REQUIRE(micro.find("creates-ok") == std::string::npos);
}

// The size lasts for the script's lifetime: every load sets it again.
TEST_CASE("The message store goes back to its default on the next load", "[MidiKit][CrossEngine]") {
	bool lua = GENERATE(true, false);
	CATCH_INFO(std::string(lua ? "Lua" : "JS"));
	using StoermelderPackOne::MidiScript::MidiScriptEngine;
	const int def = MidiScriptEngine::msgStoreDefault;

	for (const char* first : {"messages=512", "messages=5000"}) {   // loaded big, refused
		CATCH_INFO(first);
		Kit<MidiKitModule> kit;
		MidiKitModule* m = kit.m;
		m->loadScript(storeScript(lua, first, 1));
		drainLog(m);
		m->loadScript(storeScript(lua, "", def + 1));
		std::string log = drainLog(m);
		REQUIRE(log.find("message store full") != std::string::npos);
		m->loadScript(storeScript(lua, "", def));
		REQUIRE(drainLog(m).find("message store full") == std::string::npos);
	}
}

// Handles advance past what the previous callback used, not by a fixed stride:
// a callback that issued none must not let an older handle become valid again.
TEST_CASE("A handle stays invalid across a callback that created nothing", "[MidiKit][CrossEngine]") {
	const char* js = R"(/**
 * @engine QuickJs@v1
 */
trig.enableIn(1, 1);
let n = 0;
let kept = -1;
trig.onTrigger = function(trigPort, channel) {
    n++;
    if (n === 1) {
        kept = midi.create();
        midi.setNoteOn(kept, 1, 60, 100);
    }
    else if (n === 3) {
        let fresh = midi.create();
        midi.setNoteOn(fresh, 1, 99, 100);
        midiOut.send(kept);
    }
};
)";
	const char* lua = R"(--[[
@engine minilua@v1
--]]
trig.enableIn(1, 1)
local n = 0
local kept = -1
trig.onTrigger = function(trigPort, channel)
    n = n + 1
    if n == 1 then
        kept = midi.create()
        midi.setNoteOn(kept, 1, 60, 100)
    elseif n == 3 then
        local fresh = midi.create()
        midi.setNoteOn(fresh, 1, 99, 100)
        midiOut.send(kept)
    end
end
)";
	Pair scripts{js, lua};
	FOR_EACH_LANG;
	const char* script = scripts.get(lang);
	CATCH_INFO(script);
	Kit<> kit;
	MidiKitModule* m = kit.load(script).m;
	kit.dispatchTick(0, 0);
	kit.dispatchTick(0, 0);
	auto ev = kit.dispatchTick(0, 0);
	REQUIRE(ev.empty());
	REQUIRE(drainLog(m).find("onTrigger error") != std::string::npos);
}
