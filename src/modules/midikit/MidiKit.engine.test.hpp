#include "MidiKit.test.hpp"

using StoermelderPackOne::MidiScript::ScriptMenuItem;

// Cross-engine equivalence suite (review finding D7).
//
// QuickJs and Lua are two independent ~1000-line implementations of the same
// documented midi.*/midiOut.* API, with nothing structurally holding them in
// agreement. Findings #7 (sendAfterTrigger argument order), #11 (SysEx
// whitespace handling) and #13 (header-tag parsing) were all the same class
// of bug — a behaviour that quietly diverged between engines — found and
// fixed separately, three times. This file is the "shared table-driven test
// suite" option recommended in the review: one list of {script_js, script_lua}
// pairs, run against both engines, asserting the observable output (the sent
// MIDI message and the diagnostic log) is identical.
//
// This does not replace the per-engine test files — it only pins the
// contract *between* them, so a future change to one engine that isn't
// mirrored in the other fails here even if both engines individually still
// pass their own suite.

// One engine's observable result for a single script run: the messages it
// sent (in order) plus whatever landed in the log. Comparing this struct
// between engines is the actual equivalence check.
struct EngineResult {
	struct SentMessage {
		int port;
		int ticks;
		std::vector<uint8_t> bytes;

		bool operator==(const SentMessage& o) const {
			return port == o.port && ticks == o.ticks && bytes == o.bytes;
		}
	};
	std::vector<SentMessage> sent;
	std::string loadLog;
	std::string log;
};

static EngineResult::SentMessage toSent(int port, int ticks, const midi::Message& msg) {
	EngineResult::SentMessage s;
	s.port = port;
	s.ticks = ticks;
	s.bytes.assign(msg.bytes.begin(), msg.bytes.begin() + msg.getSize());
	return s;
}

// Loads the script, drains the load-time log (kept separately so a script
// that logs nothing at runtime doesn't get penalized for load-time chatter
// that has nothing to do with the behaviour under test), feeds one incoming
// message, runs the callback, then drains every pending output message.
static EngineResult run(const std::string& script, const midi::Message& in) {
	MidiKitModule* m = createModule();
	m->loadScript(script);

	EngineResult r;
	r.loadLog = drainLog(m);
	CATCH_INFO("load log:\n" << r.loadLog);
	REQUIRE(r.loadLog.find("rror") == std::string::npos);
	REQUIRE(m->host.getActiveEngine() != nullptr);

	midi::Message inCopy = in;
	m->host.getActiveEngine()->processInMessage(0, inCopy);
	m->host.getActiveEngine()->process();

	int port, ticks;
	midi::Message out;
	while (processOutMessage(m, port, out, ticks)) {
		r.sent.push_back(toSent(port, ticks, out));
	}
	r.log = drainLog(m);

	Test::destroyModule(m);
	return r;
}

// Default-input overload: most cases don't care what the incoming message
// is, only what the script does once midi.onMessage fires, so a plain NoteOn
// is enough to trigger it.
static EngineResult run(const std::string& script) {
	return run(script, noteOn(1, 60, 100));
}

// Runs both scripts and asserts they produced the same sent messages. Log
// text is intentionally not compared verbatim — the two engines' error
// strings differ in wording (see #13's write-up) — but both must be equally
// silent or equally non-silent, since a divergence there ("one engine warns,
// the other doesn't") is exactly the class of bug this file exists to catch.
static void requireEquivalent(EngineResult js, EngineResult lua) {
	REQUIRE(js.log.empty() == lua.log.empty());
	REQUIRE(js.sent.size() == lua.sent.size());
	for (size_t i = 0; i < js.sent.size(); i++) {
		REQUIRE(js.sent[i].port == lua.sent[i].port);
		REQUIRE(js.sent[i].bytes == lua.sent[i].bytes);
		// ticks is a scheduling detail, not wire content — only checked where
		// a case cares, via the "ticks" field name in the case table below.
	}
}

static void requireEquivalent(const std::string& jsScript, const std::string& luaScript) {
	CATCH_INFO("JS:\n" << jsScript);
	CATCH_INFO("Lua:\n" << luaScript);
	requireEquivalent(run(jsScript), run(luaScript));
}

// Same as requireEquivalent, but feeds a caller-supplied input message
// instead of the default NoteOn — for scripts whose behaviour depends on
// what kind of message comes in (e.g. a CC-only reroute).
static void requireEquivalent(const std::string& jsScript, const std::string& luaScript,
                               const midi::Message& in) {
	CATCH_INFO("JS:\n" << jsScript);
	CATCH_INFO("Lua:\n" << luaScript);
	requireEquivalent(run(jsScript, in), run(luaScript, in));
}

// Same run/compare shape as requireEquivalent, but for scripts that need to
// assert a specific substring is (or isn't) present in the log — e.g. "did
// this fire the outside-callback warning" — rather than "is the log empty".
// Checks the load log and the runtime log together: a script with no
// midi.onMessage runs entirely at load time (see the outside-callback-warning
// cases below), so restricting the check to the runtime log alone would miss
// it.
static void requireEquivalentLog(const std::string& jsScript, const std::string& luaScript,
                                  const std::string& logContains, bool present) {
	CATCH_INFO("JS:\n" << jsScript);
	CATCH_INFO("Lua:\n" << luaScript);

	EngineResult js = run(jsScript);
	EngineResult lua = run(luaScript);

	std::string jsAll = js.loadLog + js.log;
	std::string luaAll = lua.loadLog + lua.log;
	CATCH_INFO("JS log:\n" << jsAll);
	CATCH_INFO("Lua log:\n" << luaAll);
	REQUIRE((jsAll.find(logContains) != std::string::npos) == present);
	REQUIRE((luaAll.find(logContains) != std::string::npos) == present);
}

// Many "API getter"-style cases only need to prove a value a script computed
// at top level (or read from a getter) is the same across engines — there is
// no MIDI message to send. Both engines expose an identical log(string)
// global, and both format numbers identically via number.toString (see "API
// number.toString" in the per-engine files), so a script that logs each
// value under test turns "read this internal value" into the same kind of
// comparable, engine-agnostic side channel processInMessage/processOutMessage
// gives requireEquivalent. The script runs at load time (no midi.onMessage
// needed), so this bypasses run()'s incoming-NoteOn feed entirely.
//
// loadScript() itself writes framework chatter to the same log ("Script
// loaded", "No midi.onMessage(...) defined", ...), which would otherwise leak
// into the comparison. Probe scripts prefix every value they log with
// PROBE_PREFIX so loadAndDrainLog can pull out exactly the lines under test
// and nothing else, rather than trying to blacklist framework wording (which
// differs between engines anyway — see #13's write-up).
static const char* PROBE_PREFIX = "PROBE:";

static std::vector<std::string> loadAndDrainLog(const std::string& script) {
	MidiKitModule* m = createModule();
	m->loadScript(script);
	std::string log = drainLog(m);
	Test::destroyModule(m);

	std::vector<std::string> lines;
	size_t pos = 0;
	while (pos < log.size()) {
		size_t nl = log.find('\n', pos);
		if (nl == std::string::npos) break;
		std::string line = log.substr(pos, nl - pos);
		if (line.rfind(PROBE_PREFIX, 0) == 0) {
			lines.push_back(line.substr(strlen(PROBE_PREFIX)));
		}
		pos = nl + 1;
	}
	return lines;
}

// Compares the two scripts' logged lines directly against an expected list —
// not against each other — so a test can assert *what* the value is, not
// merely that both engines agree (two engines agreeing on a wrong answer
// would otherwise pass silently).
static void requireLoggedValues(const std::string& jsScript, const std::string& luaScript,
                                 const std::vector<std::string>& expected) {
	CATCH_INFO("JS:\n" << jsScript);
	CATCH_INFO("Lua:\n" << luaScript);

	std::vector<std::string> js = loadAndDrainLog(jsScript);
	std::vector<std::string> lua = loadAndDrainLog(luaScript);

	REQUIRE(js == expected);
	REQUIRE(lua == expected);
}

// rack.log() must coerce non-string arguments (numbers, booleans, null/nil)
// to the same text in both engines. The PROBE-prefix channel above can't be
// used here — a string concatenation would do the coercion before rack.log
// ever saw the value — so instead the script logs inside midi.onMessage and
// the whole runtime log is compared line-for-line. (The load-time framework
// chatter lives in loadLog and is ignored.) JS and Lua are asserted against
// their own expected list because QuickJs has an `undefined` value that Lua's
// `nil` has no direct counterpart for (it logs as "null" in both engines).
static void requireCoercedLog(const std::string& jsScript, const std::string& luaScript,
                               const std::vector<std::string>& jsExpected,
                               const std::vector<std::string>& luaExpected) {
	CATCH_INFO("JS:\n" << jsScript);
	CATCH_INFO("Lua:\n" << luaScript);

	auto extract = [](const std::string& script) {
		EngineResult r = run(script);
		std::vector<std::string> lines;
		size_t pos = 0;
		while (pos < r.log.size()) {
			size_t nl = r.log.find('\n', pos);
			if (nl == std::string::npos) break;
			lines.push_back(r.log.substr(pos, nl - pos));
			pos = nl + 1;
		}
		return lines;
	};

	REQUIRE(extract(jsScript) == jsExpected);
	REQUIRE(extract(luaScript) == luaExpected);
}


// --- number.* ------------------------------------------------------------
//
// Each value under test is logged at load time via number.toString, which
// the per-engine "API number.toString" tests already pin as producing
// identical text in both engines — that's what makes comparing logged lines
// a valid equivalence check rather than just an engine-internal readback.

static const char* JS_NUMBER_CROSSFADE = R"(/**
 * @engine QuickJs@v1
 */
rack.log("PROBE:" + number.toString(number.crossfade(0, 10, 0.5)));
rack.log("PROBE:" + number.toString(number.crossfade(100, 200, 0.25)));
rack.log("PROBE:" + number.toString(number.crossfade(-5, 5, 0.75)));
)";

static const char* LUA_NUMBER_CROSSFADE = R"(--[[
@engine minilua@v1
--]]
rack.log("PROBE:" .. number.toString(number.crossfade(0, 10, 0.5)))
rack.log("PROBE:" .. number.toString(number.crossfade(100, 200, 0.25)))
rack.log("PROBE:" .. number.toString(number.crossfade(-5, 5, 0.75)))
)";

TEST_CASE("number.crossfade is identical", "[MidiKit][CrossEngine]") {
	requireLoggedValues(JS_NUMBER_CROSSFADE, LUA_NUMBER_CROSSFADE, {"5", "125", "2.5"});
}


static const char* JS_NUMBER_RESCALE = R"(/**
 * @engine QuickJs@v1
 */
rack.log("PROBE:" + number.toString(number.rescale(5, 0, 10, 0, 100)));
)";

static const char* LUA_NUMBER_RESCALE = R"(--[[
@engine minilua@v1
--]]
rack.log("PROBE:" .. number.toString(number.rescale(5, 0, 10, 0, 100)))
)";

TEST_CASE("number.rescale is identical", "[MidiKit][CrossEngine]") {
	requireLoggedValues(JS_NUMBER_RESCALE, LUA_NUMBER_RESCALE, {"50"});
}


static const char* JS_NUMBER_TOSTRING = R"(/**
 * @engine QuickJs@v1
 */
rack.log("PROBE:" + number.toString(42));
rack.log("PROBE:" + number.toString(3.14));
rack.log("PROBE:" + number.toString(-100));
rack.log("PROBE:" + number.toString(1 / 3));
rack.log("PROBE:" + number.toString(0));
)";

static const char* LUA_NUMBER_TOSTRING = R"(--[[
@engine minilua@v1
--]]
rack.log("PROBE:" .. number.toString(42))
rack.log("PROBE:" .. number.toString(3.14))
rack.log("PROBE:" .. number.toString(-100))
rack.log("PROBE:" .. number.toString(1 / 3))
rack.log("PROBE:" .. number.toString(0))
)";

TEST_CASE("number.toString is identical", "[MidiKit][CrossEngine]") {
	requireLoggedValues(JS_NUMBER_TOSTRING, LUA_NUMBER_TOSTRING, {"42", "3.14", "-100", "0.333333", "0"});
}


// rack.random has no fixed expected value — both engines only need to stay
// within the documented [0, 1) range, and in agreement about that range, so
// this uses a bespoke check rather than requireLoggedValues.
static const char* JS_RACK_RANDOM = R"(/**
 * @engine QuickJs@v1
 */
rack.log("PROBE:" + number.toString(rack.random()));
)";

static const char* LUA_RACK_RANDOM = R"(--[[
@engine minilua@v1
--]]
rack.log("PROBE:" .. number.toString(rack.random()))
)";

TEST_CASE("rack.random stays within [0, 1) in both engines", "[MidiKit][CrossEngine]") {
	auto checkInRange = [](const std::string& script) {
		auto lines = loadAndDrainLog(script);
		REQUIRE(lines.size() == 1);
		double v = std::stod(lines[0]);
		REQUIRE(v >= 0.0);
		REQUIRE(v < 1.0);
	};
	checkInRange(JS_RACK_RANDOM);
	checkInRange(LUA_RACK_RANDOM);
}


// --- rack.getFrame ----------------------------------------------------------
//
// getFrame wraps APP->engine->getFrame(), the engine's sample-frame counter.
// Both engines must return it as a plain number. The script's top-level code
// runs inside loadScript() — before any engine processing could advance the
// counter — so the value it logs must match the counter read back in the test.

static const char* JS_RACK_GET_FRAME = R"(/**
 * @engine QuickJs@v1
 */
rack.log("PROBE:" + number.toString(rack.getFrame()));
)";

static const char* LUA_RACK_GET_FRAME = R"(--[[
@engine minilua@v1
--]]
rack.log("PROBE:" .. number.toString(rack.getFrame()))
)";

TEST_CASE("rack.getFrame returns the engine frame counter in both engines", "[MidiKit][CrossEngine]") {
	auto frameLogged = [](const std::string& script) {
		auto lines = loadAndDrainLog(script);
		REQUIRE(lines.size() == 1);
		return static_cast<uint64_t>(std::stoull(lines[0]));
	};

	uint64_t frame = APP->engine->getFrame();
	REQUIRE(frameLogged(JS_RACK_GET_FRAME) == frame);
	REQUIRE(frameLogged(LUA_RACK_GET_FRAME) == frame);
}


// --- rack.log coercion ---------------------------------------------------
//
// rack.log() takes any value, not just a string — numbers, booleans and
// null/undefined/nil are coerced. Numbers use the number.toString() format
// (pinned by the number.toString cases above), so `rack.log(1 / 3)` prints
// the same "0.333333" as `number.toString(1 / 3)`. `undefined` exists only
// in QuickJs, so the JS and Lua expected lists differ by exactly that line.

static const char* JS_RACK_LOG_COERCE = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    rack.log(42);
    rack.log(3.14);
    rack.log(-7);
    rack.log(1 / 3);
    rack.log(true);
    rack.log(false);
    rack.log("hello");
    rack.log(null);
    rack.log(undefined);
};
)";

static const char* LUA_RACK_LOG_COERCE = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    rack.log(42)
    rack.log(3.14)
    rack.log(-7)
    rack.log(1 / 3)
    rack.log(true)
    rack.log(false)
    rack.log("hello")
    rack.log(nil)
end
)";

TEST_CASE("rack.log coerces numbers, booleans, strings and null in both engines", "[MidiKit][CrossEngine]") {
	requireCoercedLog(JS_RACK_LOG_COERCE, LUA_RACK_LOG_COERCE,
	                  {"42", "3.14", "-7", "0.333333", "true", "false", "hello", "null", "undefined"},
	                  {"42", "3.14", "-7", "0.333333", "true", "false", "hello", "null"});
}


// --- rack.log multiple arguments ---------------------------------------
//
// rack.log() concatenates every argument into one line, coercing each value
// with the same per-type contract. So `rack.log("note on ch", ch, " note=",
// note)` prints the same line as the old `"note on ch" + number.toString(ch)
// + " note=" + number.toString(note)`, but without the number.toString()
// noise — each value is formatted by rack.log, not by the lossy `+` coercion.

static const char* JS_RACK_LOG_MULTI = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    rack.log("Member channels: ", 2, "-", 16);
    rack.log("note on ch", 3, " note=", 60, " -> ", 64);
    rack.log("ok=", true, " n=", 1 / 3, " nil=", null);
};
)";

static const char* LUA_RACK_LOG_MULTI = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    rack.log("Member channels: ", 2, "-", 16)
    rack.log("note on ch", 3, " note=", 60, " -> ", 64)
    rack.log("ok=", true, " n=", 1 / 3, " nil=", nil)
end
)";

TEST_CASE("rack.log concatenates multiple arguments in both engines", "[MidiKit][CrossEngine]") {
	requireCoercedLog(JS_RACK_LOG_MULTI, LUA_RACK_LOG_MULTI,
	                  {"Member channels: 2-16", "note on ch3 note=60 -> 64", "ok=true n=0.333333 nil=null"},
	                  {"Member channels: 2-16", "note on ch3 note=60 -> 64", "ok=true n=0.333333 nil=null"});
}


// --- setNoteOn / midiOut.send -----------------------------------------

static const char* JS_NOTE_ON = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let out = midi.create();
    midi.setNoteOn(out, 1, 60, 100);
    midiOut.send(out);
};
)";

static const char* LUA_NOTE_ON = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local out = midi.create()
    midi.setNoteOn(out, 1, 60, 100)
    midiOut.send(out)
end
)";

TEST_CASE("setNoteOn produces identical wire bytes", "[MidiKit][CrossEngine]") {
	requireEquivalent(JS_NOTE_ON, LUA_NOTE_ON);
}


// --- CC reroute (echoes incoming messages via getters/setters) -----------
//
// Increments the CC number of every incoming CC message by 1 and forwards
// it. Unlike the setNoteOn/setCc cases above, this exercises getters against
// the incoming message (handle 0) rather than only constructing a fresh one,
// and needs a CC message fed in rather than the default NoteOn.

static const char* JS_CC_REROUTE = R"(/**
 * @engine QuickJs@v1
 * @description CC number +1 passthrough
 */
midi.onMessage = function(port, msg) {
    if (midi.isCc(msg)) {
        midi.setNote(msg, midi.getNote(msg) + 1);
        midiOut.send(msg);
    }
};
)";

static const char* LUA_CC_REROUTE = R"(--[[
@engine minilua@v1
@description CC number +1 passthrough
--]]
midi.onMessage = function(port, msg)
    if midi.isCc(msg) then
        midi.setNote(msg, midi.getNote(msg) + 1)
        midiOut.send(msg)
    end
end
)";

TEST_CASE("CC reroute script produces identical output in both engines", "[MidiKit][CrossEngine]") {
	midi::Message cc;
	cc.setSize(3);
	cc.setStatus(0xb);   // CC
	cc.setChannel(0);    // channel 1 (0-based internally)
	cc.setNote(10);      // CC number 10
	cc.setValue(64);     // CC value

	requireEquivalent(JS_CC_REROUTE, LUA_CC_REROUTE, cc);
}


// --- setCc --------------------------------------------------------------

static const char* JS_CC = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let out = midi.create();
    midi.setCc(out, 2, 74, 127);
    midiOut.send(out);
};
)";

static const char* LUA_CC = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local out = midi.create()
    midi.setCc(out, 2, 74, 127)
    midiOut.send(out)
end
)";

TEST_CASE("setCc produces identical wire bytes", "[MidiKit][CrossEngine]") {
	requireEquivalent(JS_CC, LUA_CC);
}


// --- setCc clamping (documented: value clamped to 0-127) ---------------

static const char* JS_CC_CLAMP = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let out = midi.create();
    midi.setCc(out, 1, 10, 500);
    midiOut.send(out);
};
)";

static const char* LUA_CC_CLAMP = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local out = midi.create()
    midi.setCc(out, 1, 10, 500)
    midiOut.send(out)
end
)";

TEST_CASE("setCc clamps out-of-range value identically", "[MidiKit][CrossEngine]") {
	requireEquivalent(JS_CC_CLAMP, LUA_CC_CLAMP);
}


// --- setSysEx (finding #11/#17: payload-only convention) ---------------

static const char* JS_SYSEX = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let out = midi.create();
    midi.setSysEx(out, "43104c0000");
    midiOut.send(out);
};
)";

static const char* LUA_SYSEX = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local out = midi.create()
    midi.setSysEx(out, "43104c0000")
    midiOut.send(out)
end
)";

TEST_CASE("setSysEx frames the payload identically", "[MidiKit][CrossEngine]") {
	requireEquivalent(JS_SYSEX, LUA_SYSEX);
}


// --- getSysEx / getSysExLength round-trip (#D5) --------------------------
//
// The payload getter (renamed from getSysExData) plus its size guard: the
// script can check the payload length before reading it, and getSysEx returns
// the payload with the f0/f7 framing excluded. The empty payload round-trips
// as "[]".

static const char* JS_GET_SYSEX = R"(/**
 * @engine QuickJs@v1
 */
let m1 = midi.create();
midi.setSysEx(m1, "43104c0000");
rack.log("PROBE:" + number.toString(midi.getSysExLength(m1)));
rack.log("PROBE:" + midi.getSysEx(m1));

let m2 = midi.create();
midi.setSysEx(m2, "");
rack.log("PROBE:" + number.toString(midi.getSysExLength(m2)));
rack.log("PROBE:[" + midi.getSysEx(m2) + "]");
)";

static const char* LUA_GET_SYSEX = R"(--[[
@engine minilua@v1
--]]
local m1 = midi.create()
midi.setSysEx(m1, "43104c0000")
rack.log("PROBE:" .. number.toString(midi.getSysExLength(m1)))
rack.log("PROBE:" .. midi.getSysEx(m1))

local m2 = midi.create()
midi.setSysEx(m2, "")
rack.log("PROBE:" .. number.toString(midi.getSysExLength(m2)))
rack.log("PROBE:[" .. midi.getSysEx(m2) .. "]")
)";

TEST_CASE("getSysEx returns the payload and getSysExLength the size", "[MidiKit][CrossEngine]") {
	requireLoggedValues(JS_GET_SYSEX, LUA_GET_SYSEX, {"5", "43104c0000", "0", "[]"});
}


// --- sendAfterTrigger (finding #7: 3-arg form) --------------------------
//
// Regression coverage for the actual bug in #7: Lua used to misread the
// 3-arg form as (msg, trigPort, ticks) instead of matching QuickJs's
// (midiPort-selected, msg, ticks). Comparing the two engines directly is
// exactly the check that would have caught #7 the moment it was introduced,
// rather than requiring a human to notice the scripts behaved differently.

static const char* JS_SEND_AFTER_TRIGGER = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let out = midi.create();
    midi.setNoteOn(out, 1, 60, 100);
    midiOut.sendAfterTrigger(out, 10);
};
)";

static const char* LUA_SEND_AFTER_TRIGGER = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local out = midi.create()
    midi.setNoteOn(out, 1, 60, 100)
    midiOut.sendAfterTrigger(out, 10)
end
)";

TEST_CASE("sendAfterTrigger 2-arg form is identical", "[MidiKit][CrossEngine]") {
	requireEquivalent(JS_SEND_AFTER_TRIGGER, LUA_SEND_AFTER_TRIGGER);
}


// --- header-tag-only script (finding #13) -------------------------------
//
// A script whose header carries only @engine and nothing else must load in
// both engines — #13 was QuickJs-only failing on this exact shape.

static const char* JS_HEADER_ONLY = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let out = midi.create();
    midi.setNoteOn(out, 1, 60, 100);
    midiOut.send(out);
};
)";

static const char* LUA_HEADER_ONLY = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local out = midi.create()
    midi.setNoteOn(out, 1, 60, 100)
    midiOut.send(out)
end
)";

TEST_CASE("@engine-only header loads identically in both engines", "[MidiKit][CrossEngine]") {
	requireEquivalent(JS_HEADER_ONLY, LUA_HEADER_ONLY);
}


// --- getRaw / setRaw round trip -----------------------------------------

static const char* JS_RAW = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let out = midi.create();
    midi.setRaw(out, "f11a");
    midiOut.send(out);
};
)";

static const char* LUA_RAW = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local out = midi.create()
    midi.setRaw(out, "f11a")
    midiOut.send(out)
end
)";

TEST_CASE("setRaw writes identical bytes with no framing", "[MidiKit][CrossEngine]") {
	requireEquivalent(JS_RAW, LUA_RAW);
}


// --- setPitchWheel --------------------------------------------------------

static const char* JS_PITCH_WHEEL = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let out = midi.create();
    midi.setPitchWheel(out, 2, 12345);
    midiOut.send(out);
};
)";

static const char* LUA_PITCH_WHEEL = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local out = midi.create()
    midi.setPitchWheel(out, 2, 12345)
    midiOut.send(out)
end
)";

TEST_CASE("setPitchWheel produces identical wire bytes", "[MidiKit][CrossEngine]") {
	requireEquivalent(JS_PITCH_WHEEL, LUA_PITCH_WHEEL);
}


// --- setProgramChange ------------------------------------------------------

static const char* JS_PROGRAM_CHANGE = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let out = midi.create();
    midi.setProgramChange(out, 4, 10);
    midiOut.send(out);
};
)";

static const char* LUA_PROGRAM_CHANGE = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local out = midi.create()
    midi.setProgramChange(out, 4, 10)
    midiOut.send(out)
end
)";

TEST_CASE("setProgramChange produces identical wire bytes", "[MidiKit][CrossEngine]") {
	requireEquivalent(JS_PROGRAM_CHANGE, LUA_PROGRAM_CHANGE);
}


// --- getProgramChange round-trips the program number (#D5) ----------------

static const char* JS_GET_PROGRAM_CHANGE = R"(/**
 * @engine QuickJs@v1
 */
let low = midi.create();
midi.setProgramChange(low, 4, 0);
rack.log("PROBE:" + number.toString(midi.getProgramChange(low)));

let high = midi.create();
midi.setProgramChange(high, 4, 127);
rack.log("PROBE:" + number.toString(midi.getProgramChange(high)));
)";

static const char* LUA_GET_PROGRAM_CHANGE = R"(--[[
@engine minilua@v1
--]]
local low = midi.create()
midi.setProgramChange(low, 4, 0)
rack.log("PROBE:" .. number.toString(midi.getProgramChange(low)))

local high = midi.create()
midi.setProgramChange(high, 4, 127)
rack.log("PROBE:" .. number.toString(midi.getProgramChange(high)))
)";

TEST_CASE("getProgramChange round-trips the program number", "[MidiKit][CrossEngine]") {
	requireLoggedValues(JS_GET_PROGRAM_CHANGE, LUA_GET_PROGRAM_CHANGE, {"0", "127"});
}


// --- setChanPressure (2-byte message, #A2) --------------------------------

static const char* JS_CHAN_PRESSURE = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let out = midi.create();
    midi.setChanPressure(out, 5, 80);
    midiOut.send(out);
};
)";

static const char* LUA_CHAN_PRESSURE = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local out = midi.create()
    midi.setChanPressure(out, 5, 80)
    midiOut.send(out)
end
)";

TEST_CASE("setChanPressure produces identical 2-byte wire message", "[MidiKit][CrossEngine]") {
	requireEquivalent(JS_CHAN_PRESSURE, LUA_CHAN_PRESSURE);
}


// --- setKeyPressure --------------------------------------------------------

static const char* JS_KEY_PRESSURE = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let out = midi.create();
    midi.setKeyPressure(out, 6, 64, 90);
    midiOut.send(out);
};
)";

static const char* LUA_KEY_PRESSURE = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local out = midi.create()
    midi.setKeyPressure(out, 6, 64, 90)
    midiOut.send(out)
end
)";

TEST_CASE("setKeyPressure produces identical wire bytes", "[MidiKit][CrossEngine]") {
	requireEquivalent(JS_KEY_PRESSURE, LUA_KEY_PRESSURE);
}


// --- setKeyPressure clamping (#A5) -----------------------------------------

static const char* JS_KEY_PRESSURE_CLAMP = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let outHigh = midi.create();
    midi.setKeyPressure(outHigh, 6, 64, 200);
    midiOut.send(outHigh);
    let outLow = midi.create();
    midi.setKeyPressure(outLow, 6, 64, -1);
    midiOut.send(outLow);
};
)";

static const char* LUA_KEY_PRESSURE_CLAMP = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local outHigh = midi.create()
    midi.setKeyPressure(outHigh, 6, 64, 200)
    midiOut.send(outHigh)
    local outLow = midi.create()
    midi.setKeyPressure(outLow, 6, 64, -1)
    midiOut.send(outLow)
end
)";

TEST_CASE("setKeyPressure clamps out-of-range values identically (#A5)", "[MidiKit][CrossEngine]") {
	requireEquivalent(JS_KEY_PRESSURE_CLAMP, LUA_KEY_PRESSURE_CLAMP);
}


// --- setNoteOn clamping (#A5) -----------------------------------------------

static const char* JS_NOTE_ON_CLAMP = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let outHigh = midi.create();
    midi.setNoteOn(outHigh, 1, 60, 200);
    midiOut.send(outHigh);
    let outLow = midi.create();
    midi.setNoteOn(outLow, 1, 60, -1);
    midiOut.send(outLow);
};
)";

static const char* LUA_NOTE_ON_CLAMP = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local outHigh = midi.create()
    midi.setNoteOn(outHigh, 1, 60, 200)
    midiOut.send(outHigh)
    local outLow = midi.create()
    midi.setNoteOn(outLow, 1, 60, -1)
    midiOut.send(outLow)
end
)";

TEST_CASE("setNoteOn clamps out-of-range velocity identically (#A5)", "[MidiKit][CrossEngine]") {
	requireEquivalent(JS_NOTE_ON_CLAMP, LUA_NOTE_ON_CLAMP);
}


// --- midi.clone --------------------------------------------------------
//
// clone(msg) must produce an independent copy: same MIDI payload, but a
// fresh, unsent message. Editing the clone must not touch the source (a
// naive "return the same slot" alias would fail the first case below), and
// cloning the incoming message (handle 0) is the review's D8 idiom — "send
// a modified copy of the incoming message".

static const char* JS_MIDI_CLONE = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let src = midi.create();
    midi.setNoteOn(src, 1, 60, 100);
    let copy = midi.clone(src);
    midi.setNote(copy, 70);      // edit the clone only
    midiOut.send(src);           // source unchanged -> note 60
    midiOut.send(copy);          // clone carries the edit -> note 70
};
)";

static const char* LUA_MIDI_CLONE = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local src = midi.create()
    midi.setNoteOn(src, 1, 60, 100)
    local copy = midi.clone(src)
    midi.setNote(copy, 70)
    midiOut.send(src)
    midiOut.send(copy)
end
)";

TEST_CASE("midi.clone is an independent copy in both engines", "[MidiKit][CrossEngine]") {
	requireEquivalent(JS_MIDI_CLONE, LUA_MIDI_CLONE);
}


static const char* JS_MIDI_CLONE_INCOMING = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let copy = midi.clone(msg);   // deep copy of the incoming note-on
    midi.setChannel(copy, 5);     // reroute to channel 5
    midiOut.send(copy);
};
)";

static const char* LUA_MIDI_CLONE_INCOMING = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local copy = midi.clone(msg)
    midi.setChannel(copy, 5)
    midiOut.send(copy)
end
)";

TEST_CASE("midi.clone of the incoming message sends a modified copy", "[MidiKit][CrossEngine]") {
	requireEquivalent(JS_MIDI_CLONE_INCOMING, LUA_MIDI_CLONE_INCOMING);
}


// --- setNoteOff --------------------------------------------------------

static const char* JS_NOTE_OFF = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let out = midi.create();
    midi.setNoteOff(out, 7, 48);
    midiOut.send(out);
};
)";

static const char* LUA_NOTE_OFF = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local out = midi.create()
    midi.setNoteOff(out, 7, 48)
    midiOut.send(out)
end
)";

TEST_CASE("setNoteOff produces identical wire bytes", "[MidiKit][CrossEngine]") {
	requireEquivalent(JS_NOTE_OFF, LUA_NOTE_OFF);
}


// --- setNoteOff velocity (#D5) ------------------------------------------
//
// The optional 4th arg sets the release velocity (byte 3), read back with
// getValue — symmetric with note-on velocity. The 3-arg form keeps velocity
// 0 for backward compatibility; the velocity clamps to 0-127.

static const char* JS_NOTE_OFF_VEL = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let out = midi.create();
    midi.setNoteOff(out, 7, 48, 100);
    midiOut.send(out);
};
)";

static const char* LUA_NOTE_OFF_VEL = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local out = midi.create()
    midi.setNoteOff(out, 7, 48, 100)
    midiOut.send(out)
end
)";

TEST_CASE("setNoteOff with velocity produces identical wire bytes", "[MidiKit][CrossEngine]") {
	requireEquivalent(JS_NOTE_OFF_VEL, LUA_NOTE_OFF_VEL);
}

static const char* JS_NOTE_OFF_VEL_PROBE = R"(/**
 * @engine QuickJs@v1
 */
let a = midi.create();
midi.setNoteOff(a, 7, 48, 100);
rack.log("PROBE:" + number.toString(midi.getValue(a)));
midi.setNoteOff(a, 7, 48);
rack.log("PROBE:" + number.toString(midi.getValue(a)));
midi.setNoteOff(a, 7, 48, 500);
rack.log("PROBE:" + number.toString(midi.getValue(a)));
midi.setNoteOff(a, 7, 48, -5);
rack.log("PROBE:" + number.toString(midi.getValue(a)));
)";

static const char* LUA_NOTE_OFF_VEL_PROBE = R"(--[[
@engine minilua@v1
--]]
local a = midi.create()
midi.setNoteOff(a, 7, 48, 100)
rack.log("PROBE:" .. number.toString(midi.getValue(a)))
midi.setNoteOff(a, 7, 48)
rack.log("PROBE:" .. number.toString(midi.getValue(a)))
midi.setNoteOff(a, 7, 48, 500)
rack.log("PROBE:" .. number.toString(midi.getValue(a)))
midi.setNoteOff(a, 7, 48, -5)
rack.log("PROBE:" .. number.toString(midi.getValue(a)))
)";

TEST_CASE("setNoteOff velocity round-trips via getValue and clamps", "[MidiKit][CrossEngine]") {
	requireLoggedValues(JS_NOTE_OFF_VEL_PROBE, LUA_NOTE_OFF_VEL_PROBE, {"100", "0", "127", "0"});
}


// --- setCc14bit --------------------------------------------------------

static const char* JS_CC_14BIT = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let msb = midi.create();
    let lsb = midi.create();
    midi.setCc14bit(msb, lsb, 8, 1, 100.5);
    midiOut.send(msb);
    midiOut.send(lsb);
};
)";

static const char* LUA_CC_14BIT = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local msb = midi.create()
    local lsb = midi.create()
    midi.setCc14bit(msb, lsb, 8, 1, 100.5)
    midiOut.send(msb)
    midiOut.send(lsb)
end
)";

TEST_CASE("setCc14bit produces identical MSB/LSB wire messages", "[MidiKit][CrossEngine]") {
	requireEquivalent(JS_CC_14BIT, LUA_CC_14BIT);
}


// --- setCc14bit on a createCc14bit() pair (atomic 2-message flush) -------
//
// The two-handle form above sends two independent messages. A
// createCc14bit() pair is the atomic alternative: midiOut.send(cc14) flushes
// both underlying CC messages in order when passed the first handle of the
// pair (per SCRIPTING.md), so sending it is what exercises the pair end to
// end.

static const char* JS_CC_14BIT_PAIR = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let cc14 = midi.createCc14bit();
    midi.setCc14bit(cc14, 8, 1, 100.5);
    midiOut.send(cc14);
};
)";

static const char* LUA_CC_14BIT_PAIR = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local cc14 = midi.createCc14bit()
    midi.setCc14bit(cc14, 8, 1, 100.5)
    midiOut.send(cc14)
end
)";

TEST_CASE("createCc14bit pair produces identical MSB/LSB wire messages", "[MidiKit][CrossEngine]") {
	requireEquivalent(JS_CC_14BIT_PAIR, LUA_CC_14BIT_PAIR);
}

// Cross-engine equivalence only pins JS and Lua to each other — it can't
// catch a bug shared by both (the NRPN quad once flushed MSB-after-LSB in
// both engines). This asserts the actual wire bytes/order against the
// 14-bit CC convention: CC 1 (value MSB), then CC 33 (value LSB).
TEST_CASE("createCc14bit pair wire order is spec-compliant (MSB before LSB)", "[MidiKit]") {
	EngineResult r = run(JS_CC_14BIT_PAIR);
	REQUIRE(r.sent.size() == 2);
	// channel 8 -> status/channel byte 0xb7; value=100.5 -> MSB=100, LSB=64
	REQUIRE(r.sent[0].bytes == std::vector<uint8_t>{0xb7, 1, 100});
	REQUIRE(r.sent[1].bytes == std::vector<uint8_t>{0xb7, 33, 64});
}


// --- 14-bit CC pair send() order -----------------------------------------
//
// A 14-bit CC pair flushes as a unit when the group leader is sent. This
// verifies the send-order fix also applies across pairs: two pairs are
// created (p1, then p2) but sent in the opposite order (p2, then p1), and
// the wire must carry p2's whole pair before p1's whole pair — the pairs
// are ordered by send() call, not by handle-creation order, and never
// interleaved.

static const char* JS_CC_14BIT_SEND_ORDER = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let p1 = midi.createCc14bit();
    midi.setCc14bit(p1, 9, 1, 100.5);
    let p2 = midi.createCc14bit();
    midi.setCc14bit(p2, 9, 2, 3.5);
    midiOut.send(p2);   // created second, sent first
    midiOut.send(p1);   // created first, sent last
};
)";

static const char* LUA_CC_14BIT_SEND_ORDER = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local p1 = midi.createCc14bit()
    midi.setCc14bit(p1, 9, 1, 100.5)
    local p2 = midi.createCc14bit()
    midi.setCc14bit(p2, 9, 2, 3.5)
    midiOut.send(p2)
    midiOut.send(p1)
end
)";

TEST_CASE("14-bit CC pairs flush in send() order, not handle-creation order, in both engines", "[MidiKit][CrossEngine]") {
	EngineResult js = run(JS_CC_14BIT_SEND_ORDER);
	EngineResult lua = run(LUA_CC_14BIT_SEND_ORDER);

	// channel 9 -> status/channel byte 0xb8; p1 (cc=1, value=100.5) -> CC1=100, CC33=64
	std::vector<uint8_t> p1m0 = {0xb8, 1, 100};
	std::vector<uint8_t> p1m1 = {0xb8, 33, 64};
	// p2 (cc=2, value=3.5) -> CC2=3, CC34=64
	std::vector<uint8_t> p2m0 = {0xb8, 2, 3};
	std::vector<uint8_t> p2m1 = {0xb8, 34, 64};

	// Handle order would be p1's pair then p2's; send() order is p2 then p1.
	std::vector<std::vector<uint8_t>> expect = {p2m0, p2m1, p1m0, p1m1};

	REQUIRE(js.sent.size() == 4);
	for (size_t i = 0; i < expect.size(); i++) {
		REQUIRE(js.sent[i].bytes == expect[i]);
	}

	REQUIRE(lua.sent.size() == 4);
	for (size_t i = 0; i < expect.size(); i++) {
		REQUIRE(lua.sent[i].bytes == expect[i]);
	}
}


// --- setNRPN (4 chained CC messages) ------------------------------------
//
// midiOut.send(nrpnHandle) flushes all 4 underlying CC messages in order
// when passed the first handle of an NRPN quad (per SCRIPTING.md), so
// sending it is what actually exercises setNRPN's byte layout end to end.

static const char* JS_4MESSAGE_NRPN = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let nrpn = midi.createNRPN();
    midi.setNRPN(nrpn, 9, 1234, 5678);
    midiOut.send(nrpn);
};
)";

static const char* LUA_4MESSAGE_NRPN = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local nrpn = midi.createNRPN()
    midi.setNRPN(nrpn, 9, 1234, 5678)
    midiOut.send(nrpn)
end
)";

TEST_CASE("setNRPN produces identical 4-message wire sequence", "[MidiKit][CrossEngine]") {
	requireEquivalent(JS_4MESSAGE_NRPN, LUA_4MESSAGE_NRPN);
}

// Cross-engine equivalence above only pins JS and Lua to each other — it
// can't catch a bug shared by both (as happened: both engines flushed the
// quad as CC98/CC99/CC38/CC6, MSB-after-LSB for both pairs, which desyncs
// MidiProcessor::processCc's NRPN state machine and corrupts every value
// after the first). This asserts the actual wire bytes/order against the
// spec: CC99 (param MSB), CC98 (param LSB), CC6 (data MSB), CC38 (data LSB).
TEST_CASE("setNRPN wire order is spec-compliant (MSB before LSB)", "[MidiKit]") {
	EngineResult r = run(JS_4MESSAGE_NRPN);
	REQUIRE(r.sent.size() == 4);
	// channel 9 -> status/channel byte 0xb8; number=1234 -> msb=9,lsb=82; value=5678 -> msb=44,lsb=46
	REQUIRE(r.sent[0].bytes == std::vector<uint8_t>{0xb8, 99, 9});
	REQUIRE(r.sent[1].bytes == std::vector<uint8_t>{0xb8, 98, 82});
	REQUIRE(r.sent[2].bytes == std::vector<uint8_t>{0xb8, 6, 44});
	REQUIRE(r.sent[3].bytes == std::vector<uint8_t>{0xb8, 38, 46});
}


// --- NRPN send() order ---------------------------------------------------
//
// An NRPN is a quad of 4 CC messages that flush as a unit when the group
// leader is sent. This verifies that the send-order fix also applies across
// NRPN groups: two NRPNs are created (n1, then n2) but sent in the opposite
// order (n2, then n1), and the wire must carry n2's whole quad before n1's
// whole quad — i.e. the groups are ordered by send() call, not by
// handle-creation order.

static const char* JS_NRPN_SEND_ORDER = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let n1 = midi.createNRPN();
    midi.setNRPN(n1, 9, 1234, 5678);
    let n2 = midi.createNRPN();
    midi.setNRPN(n2, 9, 100, 200);
    midiOut.send(n2);   // created second, sent first
    midiOut.send(n1);   // created first, sent last
};
)";

static const char* LUA_NRPN_SEND_ORDER = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local n1 = midi.createNRPN()
    midi.setNRPN(n1, 9, 1234, 5678)
    local n2 = midi.createNRPN()
    midi.setNRPN(n2, 9, 100, 200)
    midiOut.send(n2)
    midiOut.send(n1)
end
)";

TEST_CASE("NRPN quads flush in send() order, not handle-creation order, in both engines", "[MidiKit][CrossEngine]") {
	EngineResult js = run(JS_NRPN_SEND_ORDER);
	EngineResult lua = run(LUA_NRPN_SEND_ORDER);

	// n2 (number=100, value=200): msb=0,lsb=100, data msb=1,lsb=72
	std::vector<uint8_t> n2p0 = {0xb8, 99, 0};
	std::vector<uint8_t> n2p1 = {0xb8, 98, 100};
	std::vector<uint8_t> n2p2 = {0xb8, 6, 1};
	std::vector<uint8_t> n2p3 = {0xb8, 38, 72};
	// n1 (number=1234, value=5678): msb=9,lsb=82, data msb=44,lsb=46
	std::vector<uint8_t> n1p0 = {0xb8, 99, 9};
	std::vector<uint8_t> n1p1 = {0xb8, 98, 82};
	std::vector<uint8_t> n1p2 = {0xb8, 6, 44};
	std::vector<uint8_t> n1p3 = {0xb8, 38, 46};

	// Handle order would be n1's quad then n2's; send() order is n2 then n1.
	std::vector<std::vector<uint8_t>> expect = {n2p0, n2p1, n2p2, n2p3, n1p0, n1p1, n1p2, n1p3};

	REQUIRE(js.sent.size() == 8);
	for (size_t i = 0; i < expect.size(); i++) {
		REQUIRE(js.sent[i].bytes == expect[i]);
	}

	REQUIRE(lua.sent.size() == 8);
	for (size_t i = 0; i < expect.size(); i++) {
		REQUIRE(lua.sent[i].bytes == expect[i]);
	}
}


// --- is* type predicates -------------------------------------------------
//
// One message per status byte, each checked against every is* predicate and
// the results logged as a single ordered bit string — this is the shape of
// the per-engine "API midi.is* type check" case, just funneled through the
// same PROBE_PREFIX log channel used for the number.* tests above instead of
// engine-internal js_eval/lua_getglobal readbacks.

static const char* JS_IS_TYPES = R"(/**
 * @engine QuickJs@v1
 */
let msgNoteOn = midi.create();
midi.setNoteOn(msgNoteOn, 1, 60, 100);
let msgCc = midi.create();
midi.setCc(msgCc, 1, 10, 64);
let msgSysEx = midi.create();
midi.setSysEx(msgSysEx, "43104c0000");

let bits = "" +
    (midi.isNoteOn(msgNoteOn) ? "1" : "0") +
    (midi.isNoteOff(msgNoteOn) ? "1" : "0") +
    (midi.isCc(msgNoteOn) ? "1" : "0") +
    (midi.isCc(msgCc) ? "1" : "0") +
    (midi.isSysEx(msgCc) ? "1" : "0") +
    (midi.isSysEx(msgSysEx) ? "1" : "0") +
    (midi.isClock(msgNoteOn) ? "1" : "0") +
    (midi.isStart(msgNoteOn) ? "1" : "0") +
    (midi.isStop(msgNoteOn) ? "1" : "0") +
    (midi.isContinue(msgNoteOn) ? "1" : "0");
rack.log("PROBE:" + bits);
)";

static const char* LUA_IS_TYPES = R"(--[[
@engine minilua@v1
--]]
local function b(v) if v then return "1" else return "0" end end

local msgNoteOn = midi.create()
midi.setNoteOn(msgNoteOn, 1, 60, 100)
local msgCc = midi.create()
midi.setCc(msgCc, 1, 10, 64)
local msgSysEx = midi.create()
midi.setSysEx(msgSysEx, "43104c0000")

local bits =
    b(midi.isNoteOn(msgNoteOn)) ..
    b(midi.isNoteOff(msgNoteOn)) ..
    b(midi.isCc(msgNoteOn)) ..
    b(midi.isCc(msgCc)) ..
    b(midi.isSysEx(msgCc)) ..
    b(midi.isSysEx(msgSysEx)) ..
    b(midi.isClock(msgNoteOn)) ..
    b(midi.isStart(msgNoteOn)) ..
    b(midi.isStop(msgNoteOn)) ..
    b(midi.isContinue(msgNoteOn))
rack.log("PROBE:" .. bits)
)";

TEST_CASE("midi.is* predicates agree on every message type", "[MidiKit][CrossEngine]") {
	requireLoggedValues(JS_IS_TYPES, LUA_IS_TYPES, {"1001010000"});
}


// --- midi.getChannel on a realtime/SysEx message (#A4) --------------------
//
// Status 0xf (clock, start/stop/continue, SysEx) has no channel. getChannel()
// used to return the low status nibble + 1 — a plausible-looking but
// meaningless number, since that nibble is a sub-type selector, not a
// channel — which made e.g. a clock tick misread as "channel 9". It now
// returns -1 for that family, and the real 1-16 channel otherwise.

static const char* JS_GET_CHANNEL_SENTINEL = R"(/**
 * @engine QuickJs@v1
 */
let note = midi.create();
midi.setNoteOn(note, 5, 60, 100);
rack.log("PROBE:" + number.toString(midi.getChannel(note)));

let clock = midi.create();
midi.setSysEx(clock, "");
rack.log("PROBE:" + number.toString(midi.getChannel(clock)));
)";

static const char* LUA_GET_CHANNEL_SENTINEL = R"(--[[
@engine minilua@v1
--]]
local note = midi.create()
midi.setNoteOn(note, 5, 60, 100)
rack.log("PROBE:" .. number.toString(midi.getChannel(note)))

local clock = midi.create()
midi.setSysEx(clock, "")
rack.log("PROBE:" .. number.toString(midi.getChannel(clock)))
)";

TEST_CASE("midi.getChannel returns -1 on realtime/SysEx, the real channel otherwise", "[MidiKit][CrossEngine]") {
	requireLoggedValues(JS_GET_CHANNEL_SENTINEL, LUA_GET_CHANNEL_SENTINEL, {"5", "-1"});
}


// --- input.enable ----------------------------------------------------------
//
// input.enable flips a flag on the module's own inputInfos, which is plain
// module state, not engine-internal — comparable directly without a log
// probe.

static const char* JS_INPUT_ENABLE = R"(/**
 * @engine QuickJs@v1
 */
input.enable(1);
input.enable(2);
)";

static const char* LUA_INPUT_ENABLE = R"(--[[
@engine minilua@v1
--]]
input.enable(1)
input.enable(2)
)";

TEST_CASE("input.enable sets identical module state in both engines", "[MidiKit][CrossEngine]") {
	auto checkEnabled = [](const std::string& script) {
		MidiKitModule* m = createModule();
		m->loadScript(script);
		using StoermelderPackOne::MidiScript::MidiScriptEnginePortInfo;
		bool enabled[4];
		for (int i = 0; i < 4; i++) {
			enabled[i] = reinterpret_cast<MidiScriptEnginePortInfo*>(m->inputInfos[i])->enabled;
		}
		Test::destroyModule(m);
		return std::vector<bool>(enabled, enabled + 4);
	};
	std::vector<bool> expected = {true, true, false, false};
	REQUIRE(checkEnabled(JS_INPUT_ENABLE) == expected);
	REQUIRE(checkEnabled(LUA_INPUT_ENABLE) == expected);
}


// --- input.getVoltage / input.isHigh / input.isLow --------------------------

static const char* JS_INPUT_GET_VOLTAGE = R"(/**
 * @engine QuickJs@v1
 */
input.enable(1);
rack.log("PROBE:" + number.toString(input.getVoltage(1)));
rack.log("PROBE:" + number.toString(input.getVoltage(1, 1)));
rack.log("PROBE:" + (input.isHigh(1) ? "high" : "low"));
rack.log("PROBE:" + (input.isLow(1) ? "low" : "high"));
)";

static const char* LUA_INPUT_GET_VOLTAGE = R"(--[[
@engine minilua@v1
--]]
input.enable(1)
rack.log("PROBE:" .. number.toString(input.getVoltage(1)))
rack.log("PROBE:" .. number.toString(input.getVoltage(1, 1)))
rack.log("PROBE:" .. (input.isHigh(1) and "high" or "low"))
rack.log("PROBE:" .. (input.isLow(1) and "low" or "high"))
)";

TEST_CASE("input.getVoltage/isHigh/isLow read identical default state", "[MidiKit][CrossEngine]") {
	// Default (unpatched) input reads 0V, which is "low" and not "high".
	requireLoggedValues(JS_INPUT_GET_VOLTAGE, LUA_INPUT_GET_VOLTAGE, {"0", "0", "low", "low"});
}


// --- trig.getTicks ----------------------------------------------------------
//
// getTicks depends on rising edges processed through Module::process(), so
// this drives the module directly rather than going through the probe-log
// helpers — the shape mirrors the per-engine "API trig.getTicks" case, just
// asserting the same sequence on both engines instead of one at a time.

static const char* JS_TRIG_GET_TICKS = R"(/**
 * @engine QuickJs@v1
 */
trig.enableIn(1);
midi.onMessage = function(port, msg) {
    let out = midi.create();
    midi.setCc(out, 1, 1, 0);
    midi.setValue(out, trig.getTicks(1));
    midiOut.send(out);
};
)";

static const char* LUA_TRIG_GET_TICKS = R"(--[[
@engine minilua@v1
--]]
trig.enableIn(1)
midi.onMessage = function(midiPort, msg)
    local out = midi.create()
    midi.setCc(out, 1, 1, 0)
    midi.setValue(out, trig.getTicks(1))
    midiOut.send(out)
end
)";

TEST_CASE("trig.getTicks counts identical rising edges in both engines", "[MidiKit][CrossEngine]") {
	auto ticksAfterTwoPulses = [](const std::string& script) {
		MidiKitModule* m = createModule();
		m->loadScript(script);

		Module::ProcessArgs args;
		args.sampleTime = 1.0f / 44100.0f;
		args.sampleRate = 44100.0f;

		m->inputs[MidiKitModule::INPUT_TRIG].channels = 1;
		for (int frame = 0; frame < 4; frame++) {
			// Alternates 0V/10V every frame: two rising edges over 4 frames.
			m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(frame % 2 == 0 ? 0.f : 10.f);
			args.frame = frame;
			m->process(args);
		}

		midi::Message in;
		in.setSize(3);
		in.setStatus(0x9);
		m->host.getActiveEngine()->processInMessage(0, in);
		m->host.getActiveEngine()->process();

		int port, ticks;
		midi::Message out;
		REQUIRE(processOutMessage(m, port, out, ticks));
		int result = out.getValue();
		Test::destroyModule(m);
		return result;
	};

	REQUIRE(ticksAfterTwoPulses(JS_TRIG_GET_TICKS) == ticksAfterTwoPulses(LUA_TRIG_GET_TICKS));
}


static const char* JS_TRIG_GET_TICKS_CHANNEL = R"(/**
 * @engine QuickJs@v1
 */
trig.enableIn(1, 1);
trig.enableIn(1, 2);
midi.onMessage = function(port, msg) {
    let a = midi.create();
    midi.setCc(a, 1, 1, 0);
    midi.setValue(a, trig.getTicks(1, 1));
    midiOut.send(a);
    let b = midi.create();
    midi.setCc(b, 1, 2, 0);
    midi.setValue(b, trig.getTicks(1, 2));
    midiOut.send(b);
};
)";

static const char* LUA_TRIG_GET_TICKS_CHANNEL = R"(--[[
@engine minilua@v1
--]]
trig.enableIn(1, 1)
trig.enableIn(1, 2)
midi.onMessage = function(midiPort, msg)
    local a = midi.create()
    midi.setCc(a, 1, 1, 0)
    midi.setValue(a, trig.getTicks(1, 1))
    midiOut.send(a)
    local b = midi.create()
    midi.setCc(b, 1, 2, 0)
    midi.setValue(b, trig.getTicks(1, 2))
    midiOut.send(b)
end
)";

TEST_CASE("trig.getTicks(1, channel) counts each channel independently, in both engines", "[MidiKit][CrossEngine]") {
	auto ticksPerChannel = [](const std::string& script) {
		MidiKitModule* m = createModule();
		m->loadScript(script);

		Module::ProcessArgs args;
		args.sampleTime = 1.0f / 44100.0f;
		args.sampleRate = 44100.0f;

		m->inputs[MidiKitModule::INPUT_TRIG].channels = 2;
		// Channel 1 gets two rising edges, channel 2 gets three.
		auto drive = [&](int frame, float v0, float v1) {
			m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(v0, 0);
			m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(v1, 1);
			args.frame = frame;
			m->process(args);
		};
		drive(0, 0.f, 0.f);   // prime both low
		drive(1, 10.f, 0.f);  // ch1 edge
		drive(2, 0.f, 0.f);
		drive(3, 10.f, 0.f);  // ch1 edge
		drive(4, 10.f, 10.f); // ch2 edge (ch1 stays high)
		drive(5, 0.f, 10.f);
		drive(6, 0.f, 0.f);
		drive(7, 0.f, 10.f);  // ch2 edge
		drive(8, 0.f, 0.f);
		drive(9, 0.f, 10.f);  // ch2 edge

		midi::Message in;
		in.setSize(3);
		in.setStatus(0x9);
		m->host.getActiveEngine()->processInMessage(0, in);
		m->host.getActiveEngine()->process();

		int port, ticks;
		midi::Message out;
		std::vector<int> values;
		while (processOutMessage(m, port, out, ticks)) {
			values.push_back(out.getValue());
		}
		Test::destroyModule(m);
		return values;
	};

	REQUIRE(ticksPerChannel(JS_TRIG_GET_TICKS_CHANNEL) == std::vector<int>{2, 3});
	REQUIRE(ticksPerChannel(LUA_TRIG_GET_TICKS_CHANNEL) == std::vector<int>{2, 3});
}


// --- trig.setGate / setHigh / setLow / setTrigger ---------------------------
//
// These write to the module's own trigger-output state, which is plain
// module state comparable directly across engines.

static const char* JS_TRIG_SET_FUNCTIONS = R"(/**
 * @engine QuickJs@v1
 */
trig.setGate(1, 100);
trig.setHigh(1);
trig.setLow(1);
trig.setTrigger(1);
)";

static const char* LUA_TRIG_SET_FUNCTIONS = R"(--[[
@engine minilua@v1
--]]
trig.setGate(1, 100)
trig.setHigh(1)
trig.setLow(1)
trig.setTrigger(1)
)";

TEST_CASE("trig.setTrigger produces identical output-trigger state", "[MidiKit][CrossEngine]") {
	auto checkTriggerActive = [](const std::string& script) {
		MidiKitModule* m = createModule();
		m->loadScript(script);

		// process() only writes the trigger output voltage while a cable is
		// connected (see the isConnected() gate in MidiKitModule::process),
		// so simulate one — otherwise the pulse generator's 10V pulse is
		// never written to the port and the voltage reads 0.
		m->outputs[MidiKitModule::OUTPUT_TRIG].channels = 1;

		Module::ProcessArgs args;
		args.sampleTime = 1.0f / 44100.0f;
		args.sampleRate = 44100.0f;
		args.frame = 0;
		m->process(args);

		float voltage = m->outputs[MidiKitModule::OUTPUT_TRIG].getVoltage(0);
		bool active = m->triggersOut.triggerActive[0][0];
		Test::destroyModule(m);
		return std::make_pair(voltage, active);
	};

	auto js = checkTriggerActive(JS_TRIG_SET_FUNCTIONS);
	auto lua = checkTriggerActive(LUA_TRIG_SET_FUNCTIONS);
	REQUIRE(js.first == Catch::Approx(10.0f));
	REQUIRE(js.second == true);
	REQUIRE(js == lua);
}

// Pins the ms contract of trig.setGate: the docs specify durationMs, so a
// 100 ms gate must fall after ~4410 samples at 44.1 kHz. Dedicated scripts
// (setGate only — setHigh would clear triggerActive and pin the output high).
static const char* JS_TRIG_SET_GATE_MS = R"(/**
 * @engine QuickJs@v1
 */
trig.setGate(1, 100);
)";

static const char* LUA_TRIG_SET_GATE_MS = R"(--[[
@engine minilua@v1
--]]
trig.setGate(1, 100)
)";

TEST_CASE("trig.setGate duration is milliseconds: gate falls after ~100 ms", "[MidiKit][CrossEngine]") {
	auto checkGateLength = [](const std::string& script) {
		MidiKitModule* m = createModule();
		m->loadScript(script);

		// process() only writes the trigger output voltage while a cable is
		// connected (see the isConnected() gate in MidiKitModule::process).
		m->outputs[MidiKitModule::OUTPUT_TRIG].channels = 1;

		Module::ProcessArgs args;
		args.sampleTime = 1.0f / 44100.0f;
		args.sampleRate = 44100.0f;
		args.frame = 0;

		// Count the samples the gate is high (10V).
		int highSamples = 0;
		for (int i = 0; i < 5000; i++) {
			m->process(args);
			if (m->outputs[MidiKitModule::OUTPUT_TRIG].getVoltage(0) > 5.f)
				highSamples++;
		}

		Test::destroyModule(m);
		return highSamples;
	};

	auto js = checkGateLength(JS_TRIG_SET_GATE_MS);
	auto lua = checkGateLength(LUA_TRIG_SET_GATE_MS);
	// 100 ms @ 44.1 kHz = 4410 samples. The window guards float rounding at the
	// exact boundary while still failing the old seconds interpretation (which
	// would still be high at sample 5000).
	REQUIRE(js > 4300);
	REQUIRE(js < 4500);
	REQUIRE(js == lua);
}


// --- param.enable ------------------------------------------------------------

static const char* JS_PARAM_ENABLE = R"(/**
 * @engine QuickJs@v1
 */
param.enable(1);
param.enable(3);
)";

static const char* LUA_PARAM_ENABLE = R"(--[[
@engine minilua@v1
--]]
param.enable(1)
param.enable(3)
)";

TEST_CASE("param.enable sets identical module state in both engines", "[MidiKit][CrossEngine]") {
	auto checkEnabled = [](const std::string& script) {
		MidiKitModule* m = createModule();
		m->loadScript(script);
		using StoermelderPackOne::MidiScript::MidiScriptEngineParamQuantity;
		bool enabled[4];
		for (int i = 0; i < 4; i++) {
			enabled[i] = reinterpret_cast<MidiScriptEngineParamQuantity*>(m->paramQuantities[i])->enabled;
		}
		Test::destroyModule(m);
		return std::vector<bool>(enabled, enabled + 4);
	};
	std::vector<bool> expected = {true, false, true, false};
	REQUIRE(checkEnabled(JS_PARAM_ENABLE) == expected);
	REQUIRE(checkEnabled(LUA_PARAM_ENABLE) == expected);
}


// --- param.getValue ----------------------------------------------------------

static const char* JS_PARAM_GET_VALUE = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    param.enable(1);
    let out = midi.create();
    midi.setCc(out, 1, 1, 0);
    midi.setValue(out, Math.floor(param.getValue(1) * 127));
    midiOut.send(out);
};
)";

static const char* LUA_PARAM_GET_VALUE = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    param.enable(1)
    local out = midi.create()
    midi.setCc(out, 1, 1, 0)
    midi.setValue(out, math.floor(param.getValue(1) * 127))
    midiOut.send(out)
end
)";

TEST_CASE("param.getValue reads identical value in both engines", "[MidiKit][CrossEngine]") {
	auto valueAt = [](const std::string& script, float paramValue) {
		MidiKitModule* m = createModule();
		m->loadScript(script);
		m->params[MidiKitModule::PARAM + 0].setValue(paramValue);

		midi::Message in;
		in.setSize(3);
		in.setStatus(0x9);
		m->host.getActiveEngine()->processInMessage(0, in);
		m->host.getActiveEngine()->process();

		int port, ticks;
		midi::Message out;
		REQUIRE(processOutMessage(m, port, out, ticks));
		int result = out.getValue();
		Test::destroyModule(m);
		return result;
	};

	REQUIRE(valueAt(JS_PARAM_GET_VALUE, 0.5f) == valueAt(LUA_PARAM_GET_VALUE, 0.5f));
	REQUIRE(valueAt(JS_PARAM_GET_VALUE, 1.0f) == valueAt(LUA_PARAM_GET_VALUE, 1.0f));
}


// --- midiOut.selectPort ------------------------------------------------

static const char* JS_SELECT_PORT = R"(/**
 * @engine QuickJs@v1
 */
let msg = midi.create();
midi.setNoteOn(msg, 1, 60, 100);

midi.onMessage = function(port, msg) {
    midiOut.selectPort(1);
    midiOut.send(msg);
};
)";

static const char* LUA_SELECT_PORT = R"(--[[
@engine minilua@v1
--]]
msg = midi.create()
midi.setNoteOn(msg, 1, 60, 100)

midi.onMessage = function(midiPort, msg)
    midiOut.selectPort(1)
    midiOut.send(msg)
end
)";

TEST_CASE("midiOut.selectPort produces identical output port in both engines", "[MidiKit][CrossEngine]") {
	requireEquivalent(JS_SELECT_PORT, LUA_SELECT_PORT);
}


static const char* JS_SELECT_PORT_STICKY = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    midiOut.selectPort(1);
    let msg1 = midi.create();
    midi.setNoteOn(msg1, 1, 60, 100);
    let msg2 = midi.create();
    midi.setNoteOn(msg2, 1, 61, 100);
    midiOut.send(msg1);
    midiOut.send(msg2);
};
)";

static const char* LUA_SELECT_PORT_STICKY = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    midiOut.selectPort(1)
    local msg1 = midi.create()
    midi.setNoteOn(msg1, 1, 60, 100)
    local msg2 = midi.create()
    midi.setNoteOn(msg2, 1, 61, 100)
    midiOut.send(msg1)
    midiOut.send(msg2)
end
)";

TEST_CASE("midiOut.selectPort stays selected across calls identically", "[MidiKit][CrossEngine]") {
	requireEquivalent(JS_SELECT_PORT_STICKY, LUA_SELECT_PORT_STICKY);
}


static const char* JS_SELECT_PORT_INVALID = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    midiOut.selectPort(2);
};
)";

static const char* LUA_SELECT_PORT_INVALID = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    midiOut.selectPort(2)
end
)";

TEST_CASE("midiOut.selectPort rejects an out-of-range port identically", "[MidiKit][CrossEngine]") {
	requireEquivalentLog(JS_SELECT_PORT_INVALID, LUA_SELECT_PORT_INVALID, "onMessage error", true);
}


// --- midiOut.sendAfterMs ------------------------------------------------

static const char* JS_SEND_AFTER_MS = R"(/**
 * @engine QuickJs@v1
 */
let msg = midi.create();
midi.setNoteOn(msg, 1, 60, 100);

midi.onMessage = function(port, msg) {
    midiOut.sendAfterMs(msg, 100);
};
)";

static const char* LUA_SEND_AFTER_MS = R"(--[[
@engine minilua@v1
--]]
msg = midi.create()
midi.setNoteOn(msg, 1, 60, 100)

midi.onMessage = function(midiPort, msg)
    midiOut.sendAfterMs(msg, 100)
end
)";

TEST_CASE("midiOut.sendAfterMs schedules an identical future-frame message", "[MidiKit][CrossEngine]") {
	// frame is a scheduling detail specific to each engine's own sample-time
	// bookkeeping, so it's checked for "> 0" per engine rather than compared
	// for equality between them — requireEquivalent already pins the wire
	// bytes and port, which is what sendAfterMs otherwise shares with
	// midiOut.send.
	auto frameAt = [](const std::string& script) {
		MidiKitModule* m = createModule();
		m->loadScript(script);
		drainLog(m);

		midi::Message in = noteOn(1, 60, 100);
		m->host.getActiveEngine()->processInMessage(0, in);
		m->host.getActiveEngine()->process();

		int port, ticks;
		midi::Message out;
		REQUIRE(processOutMessage(m, port, out, ticks));
		int frame = out.frame;
		Test::destroyModule(m);
		return frame;
	};

	REQUIRE(frameAt(JS_SEND_AFTER_MS) > 0);
	REQUIRE(frameAt(LUA_SEND_AFTER_MS) > 0);
	requireEquivalent(JS_SEND_AFTER_MS, LUA_SEND_AFTER_MS);
}


// --- midiOut.sendAfterTrigger with selected port / explicit trigPort ------

static const char* JS_SEND_AFTER_TRIGGER_SELECTED_PORT = R"(/**
 * @engine QuickJs@v1
 */
let msg = midi.create();
midi.setNoteOn(msg, 1, 60, 100);

midi.onMessage = function(port, msg) {
    midiOut.selectPort(1);
    midiOut.sendAfterTrigger(msg, 10);
};
)";

static const char* LUA_SEND_AFTER_TRIGGER_SELECTED_PORT = R"(--[[
@engine minilua@v1
--]]
msg = midi.create()
midi.setNoteOn(msg, 1, 60, 100)

midi.onMessage = function(midiPort, msg)
    midiOut.selectPort(1)
    midiOut.sendAfterTrigger(msg, 10)
end
)";

TEST_CASE("sendAfterTrigger uses the selected port identically", "[MidiKit][CrossEngine]") {
	requireEquivalent(JS_SEND_AFTER_TRIGGER_SELECTED_PORT, LUA_SEND_AFTER_TRIGGER_SELECTED_PORT);
}


static const char* JS_SEND_AFTER_TRIGGER_TRIGPORT = R"(/**
 * @engine QuickJs@v1
 */
let msg = midi.create();
midi.setNoteOn(msg, 1, 60, 100);

midi.onMessage = function(port, msg) {
    midiOut.selectPort(1);
    midiOut.sendAfterTrigger(msg, 10, 1);
};
)";

static const char* LUA_SEND_AFTER_TRIGGER_TRIGPORT = R"(--[[
@engine minilua@v1
--]]
msg = midi.create()
midi.setNoteOn(msg, 1, 60, 100)

midi.onMessage = function(midiPort, msg)
    midiOut.selectPort(1)
    midiOut.sendAfterTrigger(msg, 10, 1)
end
)";

TEST_CASE("sendAfterTrigger with explicit trigPort (3 args) is identical", "[MidiKit][CrossEngine]") {
	requireEquivalent(JS_SEND_AFTER_TRIGGER_TRIGPORT, LUA_SEND_AFTER_TRIGGER_TRIGPORT);
}


static const char* JS_SEND_AFTER_TRIGGER_CHANNEL = R"(/**
 * @engine QuickJs@v1
 */
let msg = midi.create();
midi.setNoteOn(msg, 1, 60, 100);

midi.onMessage = function(port, msg) {
    midiOut.selectPort(1);
    midiOut.sendAfterTrigger(msg, 10, 1, 2);
};
)";

static const char* LUA_SEND_AFTER_TRIGGER_CHANNEL = R"(--[[
@engine minilua@v1
--]]
msg = midi.create()
midi.setNoteOn(msg, 1, 60, 100)

midi.onMessage = function(midiPort, msg)
    midiOut.selectPort(1)
    midiOut.sendAfterTrigger(msg, 10, 1, 2)
end
)";

TEST_CASE("sendAfterTrigger with explicit channel (4 args) is identical", "[MidiKit][CrossEngine]") {
	requireEquivalent(JS_SEND_AFTER_TRIGGER_CHANNEL, LUA_SEND_AFTER_TRIGGER_CHANNEL);
}


// --- midi.create() / midi.createNRPN() outside midi.onMessage() -----------
//
// The message store is reset on every callback, so a handle created at top
// level is silently invalidated before it can be used. That reset is
// documented and intended; these tests pin the warning that makes it
// visible, and that the two engines use the same wording for it (unlike
// parse-error text, which #13's write-up notes differs deliberately).

static const char* OUTSIDE_CALLBACK_WARNING = "called outside a callback";

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

TEST_CASE("midi.create outside midi.onMessage warns identically", "[MidiKit][CrossEngine]") {
	requireEquivalentLog(JS_TOPLEVEL_CREATE, LUA_TOPLEVEL_CREATE, OUTSIDE_CALLBACK_WARNING, true);
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

TEST_CASE("midi.createNRPN outside midi.onMessage warns identically", "[MidiKit][CrossEngine]") {
	requireEquivalentLog(JS_TOPLEVEL_CREATE_NRPN, LUA_TOPLEVEL_CREATE_NRPN, OUTSIDE_CALLBACK_WARNING, true);
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

TEST_CASE("midi.create inside midi.onMessage does not warn in either engine", "[MidiKit][CrossEngine]") {
	requireEquivalentLog(JS_CALLBACK_CREATE, LUA_CALLBACK_CREATE, OUTSIDE_CALLBACK_WARNING, false);
}


// --- onLoad ----------------------------------------------------------------

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


// --- top-level message handle survives a load with no onLoad (#D4) --------
//
// onLoad must only reset the message store when the script actually
// overrides the default no-op — otherwise a handle a script builds at top
// level (a documented, intentional pattern) would be discarded before the
// script ever gets to use it.

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

TEST_CASE("Top-level message handle survives a load with no onLoad in both engines (#D4)", "[MidiKit][CrossEngine]") {
	requireLoggedValues(JS_TOPLEVEL_SYSEX, LUA_TOPLEVEL_SYSEX, {"yes"});
}


// --- onUnload ----------------------------------------------------------

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
	// then closeState()) makes a second call a no-op: activeEngine is already
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
	// Covers the queue consolidation: the outgoing engine's onUnload() message
	// used to be queued on THAT engine's own out-queue, which nothing drained
	// once activeEngine moved on. With a module-owned queue nothing is keyed on
	// which engine is active, so the message is still observable here.
	//
	// This does NOT cover the switch being a blocking closeState() rather than
	// an async loadScript("") — under SyncTaskWorker both run onUnload() inline,
	// so the message lands either way. See the async-worker test below.
	MidiKitModule* m = createModule();
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

	Test::destroyModule(m);
}


// ── async-worker teardown ───────────────────────────────────────────────────
//
// These use a real background worker rather than SyncTaskWorker. Under
// SyncTaskWorker every dispatch runs inline, so a fire-and-forget loadScript()
// and a blocking closeState() are indistinguishable — a test written against it
// passes whether or not teardown actually waits. The whole point of the switch
// and teardown paths using closeState() is that they DO wait, which only a real
// worker can show.

TEST_CASE("Switching engines closes the outgoing engine before returning", "[MidiKit][CrossEngine][Async]") {
	// loadScript() used to close the outgoing engine with an async
	// loadScript(""), so the switch returned while onUnload() had not run yet.
	// It is now a blocking closeState(): once loadScript() returns, the outgoing
	// engine's onUnload() has finished and its output is already queued.
	//
	// No barrier() before the assertions — that is the point. If the outgoing
	// close were async again, the log and the queue would both still be empty
	// here and this fails.
	auto worker = asyncWorker();
	MidiKitModule* m = createModule(worker);

	m->loadScript(JS_ON_UNLOAD);
	barrier(worker);                 // the LOAD is still async; wait for it
	drainLog(m);
	REQUIRE(m->host.isQuickJsEngine());

	m->loadScript(LUA_ON_UNLOAD);    // switch: closes QuickJs synchronously

	std::string log = drainLog(m);
	REQUIRE(log.find("onUnload ran") != std::string::npos);

	int port, ticks;
	midi::Message out;
	REQUIRE(processOutMessage(m, port, out, ticks));
	REQUIRE(out.getStatus() == 0x8);   // note off
	REQUIRE(out.getNote() == 60);

	barrier(worker);                 // let the pending Lua load finish
	Test::destroyModule(m);
}


TEST_CASE("onRemove() waits for onUnload before draining", "[MidiKit][CrossEngine][Async]") {
	// Teardown's ordering contract: closeState() blocks, so by the time
	// flushOutput() runs the worker has finished producing. If the close were
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

		// Drained by flushOutput(), not left queued.
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
	// blocking closeState() on the active engine. Same ordering contract as the
	// switch path: once onReset() returns, onUnload() has run and its output is
	// queued rather than still in flight.
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

		int port, ticks;
		midi::Message out;
		REQUIRE(processOutMessage(m, port, out, ticks));
		REQUIRE(out.getStatus() == 0x8);   // note off

		Test::destroyModule(m);
	};
	check(JS_ON_UNLOAD);
	check(LUA_ON_UNLOAD);
}


// ── process() drains the module queue ───────────────────────────────────────

// Runs process() over one full divider period (division 8), so the drain block
// inside `if (processDivider.process())` is reached exactly once.
static void processOneDividerPeriod(MidiKitModule* m, int64_t startFrame = 0) {
	for (int64_t f = 0; f < 8; f++) {
		m->process(Test::makeProcessArgs(startFrame + f));
	}
}

TEST_CASE("process() drains the out-queue after the script is cleared", "[MidiKit][CrossEngine]") {
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
		REQUIRE_FALSE(m->midiOutQueue.empty());   // onUnload()'s message is queued

		processOneDividerPeriod(m);

		// process() moved it out of the module queue even with no active engine.
		REQUIRE(m->midiOutQueue.empty());

		Test::destroyModule(m);
	};
	check(JS_ON_UNLOAD);
	check(LUA_ON_UNLOAD);
}


TEST_CASE("process() drains a tick-scheduled message into midiOutput", "[MidiKit]") {
	// End-to-end for the drain: a message the engine queued with a non-zero tick
	// must reach midiOutput's tick queue, not merely leave the module queue.
	// midi::Output::sendMessage() no-ops without a subscribed device, so
	// midiOutput's scheduling queues are the observable endpoint.
	MidiKitModule* m = createModule();
	midi::Message msg = noteOn(1, 60, 100);

	REQUIRE(m->sendMidi(0, &msg, 1, 0, 5));   // tick 5: lands in tickQueue
	REQUIRE(m->midiOutput.tickQueue[0].size() == 0);

	processOneDividerPeriod(m);

	REQUIRE(m->midiOutQueue.empty());
	REQUIRE(m->midiOutput.tickQueue[0].size() == 1);
	REQUIRE(m->midiOutput.tickQueue[0].top().tick == 5);

	Test::destroyModule(m);
}


TEST_CASE("process() drains the queue in FIFO order across an engine switch", "[MidiKit][CrossEngine]") {
	// The per-engine queues could never interleave; the shared one can. Messages
	// queued by the outgoing engine must still precede those from the incoming
	// engine — a switch must not reorder output. Distinguishable notes stand in
	// for the two producers.
	MidiKitModule* m = createModule();

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

	Test::destroyModule(m);
}


TEST_CASE("An NRPN group is queued whole and in order", "[MidiKit]") {
	// The atomicity contract has two halves: dropped whole when short on room
	// (below), and — here — queued as four consecutive messages in the order
	// given, with no interleaving from a message queued after it.
	MidiKitModule* m = createModule();

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

	Test::destroyModule(m);
}


TEST_CASE("onRemove() flushes teardown output immediately, bypassing scheduling", "[MidiKit]") {
	// flushOutput() sets frame = -1 and calls midiOutput.sendMessage() directly
	// rather than midiOutput.send(): the frame and tick queues are drained only
	// by process(), which will never run again. A tick-scheduled message left to
	// send() would land in tickQueue and never be emitted.
	MidiKitModule* m = createModule();
	midi::Message msg = noteOn(1, 60, 100);

	REQUIRE(m->sendMidi(0, &msg, 1, 0, 5));   // would be tick-scheduled via send()

	Module::RemoveEvent eRemove;
	m->onRemove(eRemove);

	REQUIRE(m->midiOutQueue.empty());
	// Sent immediately instead of being parked in a queue nothing will drain.
	REQUIRE(m->midiOutput.tickQueue[0].size() == 0);
	REQUIRE(m->midiOutput.frameQueue.size() == 0);

	delete m;
}


TEST_CASE("MIDI output overflow drops without corrupting the queue", "[MidiKit]") {
	// dsp::RingBuffer::push() has no overflow check: on a full buffer it
	// overwrites unread entries and leaves size() > capacity, and
	// empty()/full() go incoherent from there. sendMidi() adds the check the
	// container lacks — this pins that the queue's invariants survive being
	// pushed past capacity.
	MidiKitModule* m = createModule();
	midi::Message msg = noteOn(1, 60, 100);

	size_t capacity = m->midiOutQueue.capacity();
	for (size_t i = 0; i < capacity; i++) {
		REQUIRE(m->sendMidi(0, &msg, 1, 0, 0));
	}
	REQUIRE(m->midiOutQueue.full());

	// One more push has no room: dropped, not overwritten.
	REQUIRE_FALSE(m->sendMidi(0, &msg, 1, 0, 0));
	REQUIRE(m->midiOutQueue.full());
	REQUIRE(m->midiOutQueue.size() == capacity);
	REQUIRE_FALSE(m->midiOutQueue.empty());

	Test::destroyModule(m);
}


TEST_CASE("MIDI output overflow is reported once per episode, not once per drop", "[MidiKit]") {
	MidiKitModule* m = createModule();
	midi::Message msg = noteOn(1, 60, 100);

	size_t capacity = m->midiOutQueue.capacity();
	for (size_t i = 0; i < capacity; i++) {
		REQUIRE(m->sendMidi(0, &msg, 1, 0, 0));
	}
	// Several drops in the same episode — only one log line should result once
	// process() next runs and consumes the rising edge of midiOutOverflow.
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

	Test::destroyModule(m);
}


TEST_CASE("MIDI output overflow is reported again after the queue recovers", "[MidiKit]") {
	// The flag is edge-triggered via exchange(false), so reporting once per
	// episode must not mean once per module lifetime: a later, separate
	// saturation has to log again. Pins that process() CLEARS the flag rather
	// than latching it.
	MidiKitModule* m = createModule();
	midi::Message msg = noteOn(1, 60, 100);

	auto fillAndOverflow = [&]() {
		while (m->midiOutQueue.capacity() > 0) {
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
	REQUIRE(m->midiOutQueue.empty());

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
	// The all-or-nothing contract in sendMidi(): an NRPN is 4 messages sharing
	// one parameter change, and a partial group is a malformed parameter
	// change, worse than dropping it outright.
	MidiKitModule* m = createModule();
	midi::Message msg = noteOn(1, 60, 100);

	// Leave exactly 3 free slots — one short of the 4-message group.
	size_t capacity = m->midiOutQueue.capacity();
	for (size_t i = 0; i < capacity - 3; i++) {
		REQUIRE(m->sendMidi(0, &msg, 1, 0, 0));
	}
	REQUIRE(m->midiOutQueue.capacity() == 3);

	midi::Message group[4] = {msg, msg, msg, msg};
	REQUIRE_FALSE(m->sendMidi(0, group, 4, 0, 0));
	// Rejected as a whole: the 3 free slots are still free, not partially
	// consumed by the first 3 messages of the group.
	REQUIRE(m->midiOutQueue.capacity() == 3);

	Test::destroyModule(m);
}


TEST_CASE("onUnload runs again when a second script replaces the first, in both engines", "[MidiKit][CrossEngine]") {
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


// --- rack.onSave() vs rack.onUnload() -------------------------------------
//
// rack.onSave() is the config-bearing hook; rack.onUnload() is teardown-only
// and any value it returns is discarded. Both scripts below define both
// hooks so a test can tell, from the log alone, which one actually ran.

static const char* JS_ON_SAVE_AND_UNLOAD = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(midiPort, msg) {};
rack.onUnload = function() {
    rack.log("onUnload ran");
    return { bogus: true };
};
rack.onSave = function() {
    rack.log("onSave ran");
    return { real: 42 };
};
)";

static const char* LUA_ON_SAVE_AND_UNLOAD = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg) end
rack.onUnload = function()
    rack.log("onUnload ran")
    return { bogus = true }
end
rack.onSave = function()
    rack.log("onSave ran")
    return { real = 42 }
end
)";

TEST_CASE("captureConfig() calls onSave, not onUnload, and does not run teardown, in both engines", "[MidiKit][CrossEngine]") {
	auto check = [](const std::string& script) {
		MidiKitModule* m = createModule();
		m->loadScript(script);
		drainLog(m);

		// A save (captureConfig()) must run onSave(), not onUnload() — this is
		// the regression this hook split targets: onUnload used to be the
		// config-bearing hook, so a save would spuriously log "onUnload ran"
		// and (for scripts with real teardown side effects) fire them on
		// every save.
		std::string config = captureConfig(m->host.getActiveEngine());
		std::string log = drainLog(m);
		REQUIRE(log.find("onSave ran") != std::string::npos);
		REQUIRE(log.find("onUnload ran") == std::string::npos);
		REQUIRE(configInt(config, "real") == 42);

		Test::destroyModule(m);
	};
	check(JS_ON_SAVE_AND_UNLOAD);
	check(LUA_ON_SAVE_AND_UNLOAD);
}

TEST_CASE("onUnload's return value is ignored on real teardown, in both engines", "[MidiKit][CrossEngine]") {
	auto check = [](const std::string& script) {
		MidiKitModule* m = createModule();
		m->loadScript(script);
		drainLog(m);

		// clearScript() tears the script down for real (the onUnload() path),
		// which used to also be where config was captured.
		m->clearScript();
		std::string log = drainLog(m);
		REQUIRE(log.find("onUnload ran") != std::string::npos);

		Test::destroyModule(m);
	};
	check(JS_ON_SAVE_AND_UNLOAD);
	check(LUA_ON_SAVE_AND_UNLOAD);
}


// --- script config persistence -------------------------------------------
//
// rack.onLoad(persistedConfig) restores a config; rack.onSave() returns the
// current config. The engine JSON-stringifies onSave()'s return value and
// hands it back to onLoad() on the next load — the save/reload round-trip
// the module's dataToJson()/dataFromJson() drive. This asserts the engine
// contract directly: captureConfig() reflects context-menu edits, and a
// reload with that config makes onLoad() see the same values.

static const char* JS_CONFIG = R"(/**
 * @engine QuickJs@v1
 */
let config = { divisor: 6, emitTrigger: true };
rack.onLoad = function(persisted) {
    if (persisted) config = Object.assign({}, config, persisted);
    rack.log("onLoad divisor=" + config.divisor + " emitTrigger=" + config.emitTrigger);
};
rack.onSave = function() {
    return config;
};
rack.registerContextMenu({
    type: "boolean",
    label: "Emit trigger",
    onGetValue: function() {
		return config.emitTrigger;
	},
    onChange: function(checked) {
		config.emitTrigger = checked;
	}
});
midi.onMessage = function(midiPort, msg) {};
)";

static const char* LUA_CONFIG = R"(--[[
@engine minilua@v1
--]]
config = { divisor = 6, emitTrigger = true }
rack.onLoad = function(persisted)
    if persisted then
        config.divisor = persisted.divisor or config.divisor
        config.emitTrigger = persisted.emitTrigger
    end
    rack.log("onLoad divisor=" .. config.divisor .. " emitTrigger=" .. tostring(config.emitTrigger))
end
rack.onSave = function()
    return config
end
rack.registerContextMenu({
    type = "boolean",
    label = "Emit trigger",
    onGetValue = function()
		return config.emitTrigger
	end,
    onChange = function(checked)
        config.emitTrigger = checked
    end
})
midi.onMessage = function(midiPort, msg) end
)";

TEST_CASE("Script config survives capture and reload in both engines", "[MidiKit][CrossEngine]") {
	auto check = [](const std::string& script) {
		MidiKitModule* m = createModule();
		m->loadScript(script);
		drainLog(m);

		// Initial config, as returned by onSave().
		std::string config = captureConfig(m->host.getActiveEngine());
		REQUIRE(configInt(config, "divisor") == 6);
		REQUIRE(configBool(config, "emitTrigger") == true);

		// The user flips a setting via the script's context menu.
		std::vector<ScriptMenuItem> specs;
		m->host.getActiveEngine()->getContextMenus([&specs](const std::vector<ScriptMenuItem>& s) { specs = s; });
		REQUIRE(specs.size() == 1);
		m->host.getActiveEngine()->invokeContextMenuCallback(specs[0].callbackId, 0);
		drainLog(m);

		// The modified config is what a save would persist.
		config = captureConfig(m->host.getActiveEngine());
		REQUIRE(configBool(config, "emitTrigger") == false);

		// Reload with the persisted config: onLoad() must restore it.
		m->loadScript(script, config);
		std::string reloadLog = drainLog(m);
		// The log line's number formatting is engine-specific (JS prints
		// "6", Lua's `..` stringifies as "6.0"), so assert the stable parts
		// and verify the restored values numerically rather than comparing
		// log text verbatim — same philosophy as requireEquivalent() above.
		REQUIRE(reloadLog.find("onLoad divisor=") != std::string::npos);
		REQUIRE(reloadLog.find("emitTrigger=false") != std::string::npos);

		// The script's config after the reload is the persisted, flipped one.
		std::string restored = captureConfig(m->host.getActiveEngine());
		REQUIRE(configInt(restored, "divisor") == 6);
		REQUIRE(configBool(restored, "emitTrigger") == false);

		Test::destroyModule(m);
	};
	check(JS_CONFIG);
	check(LUA_CONFIG);
}


TEST_CASE("A script with only onUnload (no onSave) persists nothing, in both engines", "[MidiKit][CrossEngine]") {
	// There is no legacy fallback from onSave() to onUnload()'s return value:
	// a script written before rack.onSave() existed, which only returns its
	// config from onUnload(), simply persists nothing until migrated. Reuses
	// JS_ON_SAVE_AND_UNLOAD/LUA_ON_SAVE_AND_UNLOAD's onUnload (which does
	// return a real table) with onSave stripped out.
	//
	// captureConfig() succeeds with an empty result rather than failing: "this
	// script has no config" is a definite answer, so the caller clears its
	// stored config instead of keeping a stale one. Answered from the cached
	// hook ref without a worker round-trip.
	auto check = [](const std::string& script) {
		MidiKitModule* m = createModule();
		m->loadScript(script);
		drainLog(m);

		std::string config = captureConfig(m->host.getActiveEngine());
		REQUIRE(config.empty());

		Test::destroyModule(m);
	};

	static const char* JS_ONLY_UNLOAD = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(midiPort, msg) {};
rack.onUnload = function() {
    return { bogus: true };
};
)";
	static const char* LUA_ONLY_UNLOAD = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg) end
rack.onUnload = function()
    return { bogus = true }
end
)";

	check(JS_ONLY_UNLOAD);
	check(LUA_ONLY_UNLOAD);
}


TEST_CASE("captureConfig on an engine with no script loaded reports nothing to persist", "[MidiKit][CrossEngine]") {
	// No script is a definite "nothing to persist", not a failure: there is no
	// config to preserve, so the caller should clear whatever it stored rather
	// than keep it. false is reserved for a failed dispatch.
	MidiKitModule* m = createModule();
	REQUIRE(captureConfig(&m->host.seLua).empty());
	REQUIRE(captureConfig(&m->host.seQuickJs).empty());
	Test::destroyModule(m);
}


static const char* JS_ON_TRIGGER = R"(/**
 * @engine QuickJs@v1
 */
trig.enableIn(1);
trig.onTrigger = function(trigPort) {
    rack.log("onTrigger " + number.toString(trigPort));
    let msg = midi.create();
    midi.setCc(msg, 1, 10, trigPort);
    midiOut.send(msg);
};
)";

static const char* LUA_ON_TRIGGER = R"(--[[
@engine minilua@v1
--]]
trig.enableIn(1)
function trig.onTrigger(trigPort)
    rack.log("onTrigger " .. trigPort)
    local msg = midi.create()
    midi.setCc(msg, 1, 10, trigPort)
    midiOut.send(msg)
end
)";

TEST_CASE("onTrigger fires on a trigger input tick and sends an identical message in both engines", "[MidiKit][CrossEngine]") {
	auto checkOnTrigger = [](const std::string& script) {
		MidiKitModule* m = createModule();
		m->loadScript(script);
		drainLog(m);

		m->host.getActiveEngine()->processInTick(0, 0);
		m->host.getActiveEngine()->process();

		std::string log = drainLog(m);
		REQUIRE(log.find("onTrigger 1") != std::string::npos);

		int port, ticks;
		midi::Message out;
		REQUIRE(processOutMessage(m, port, out, ticks));
		auto sent = toSent(port, ticks, out);
		Test::destroyModule(m);
		return sent;
	};

	auto js = checkOnTrigger(JS_ON_TRIGGER);
	auto lua = checkOnTrigger(LUA_ON_TRIGGER);
	REQUIRE(js.port == lua.port);
	REQUIRE(js.bytes == lua.bytes);
}


static const char* JS_ON_TRIGGER_CHANNEL = R"(/**
 * @engine QuickJs@v1
 */
trig.enableIn(1, 1);
trig.enableIn(1, 2);
trig.onTrigger = function(trigPort, channel) {
    rack.log("onTrigger " + number.toString(trigPort) + " " + number.toString(channel));
};
)";

static const char* LUA_ON_TRIGGER_CHANNEL = R"(--[[
@engine minilua@v1
--]]
trig.enableIn(1, 1)
trig.enableIn(1, 2)
function trig.onTrigger(trigPort, channel)
    rack.log("onTrigger " .. trigPort .. " " .. channel)
end
)";

TEST_CASE("onTrigger receives the firing channel, in both engines", "[MidiKit][CrossEngine]") {
	auto logChannels = [](const std::string& script) {
		MidiKitModule* m = createModule();
		m->loadScript(script);
		drainLog(m);

		// Channels are 1-based in the callback: index 0 -> "1", index 1 -> "2".
		m->host.getActiveEngine()->processInTick(0, 0);
		m->host.getActiveEngine()->process();
		m->host.getActiveEngine()->processInTick(0, 1);
		m->host.getActiveEngine()->process();

		std::string log = drainLog(m);
		Test::destroyModule(m);
		return log;
	};

	auto js = logChannels(JS_ON_TRIGGER_CHANNEL);
	auto lua = logChannels(LUA_ON_TRIGGER_CHANNEL);
	REQUIRE(js.find("onTrigger 1 1") != std::string::npos);
	REQUIRE(js.find("onTrigger 1 2") != std::string::npos);
	REQUIRE(lua.find("onTrigger 1 1") != std::string::npos);
	REQUIRE(lua.find("onTrigger 1 2") != std::string::npos);
}


TEST_CASE("Script without onTrigger silently ignores trigger ticks, in both engines", "[MidiKit][CrossEngine]") {
	auto checkNoOnTrigger = [](const std::string& script) {
		MidiKitModule* m = createModule();
		m->loadScript(script);
		drainLog(m);

		m->host.getActiveEngine()->processInTick(0, 0);
		m->host.getActiveEngine()->process();

		std::string log = drainLog(m);
		int port, ticks;
		midi::Message out;
		bool sentAnything = processOutMessage(m, port, out, ticks);
		Test::destroyModule(m);
		return std::make_pair(log, sentAnything);
	};

	auto js = checkNoOnTrigger(JS_NO_ON_LOAD);
	auto lua = checkNoOnTrigger(LUA_NO_ON_LOAD);
	REQUIRE(js.second == false);
	REQUIRE(lua.second == false);
}


static const char* JS_ON_TRIGGER_NOT_ENABLED = R"(/**
 * @engine QuickJs@v1
 */
trig.onTrigger = function(trigPort, channel) {
    rack.log("onTrigger fired");
};
)";

static const char* LUA_ON_TRIGGER_NOT_ENABLED = R"(--[[
@engine minilua@v1
--]]
trig.onTrigger = function(trigPort, channel)
    rack.log("onTrigger fired")
end
)";

TEST_CASE("trig.onTrigger is not called until trig.enableIn() is used, in both engines", "[MidiKit][CrossEngine]") {
	// trig.onTrigger is unused until the channel is enabled with trig.enableIn().
	auto checkNotEnabled = [](const std::string& script) {
		MidiKitModule* m = createModule();
		m->loadScript(script);
		drainLog(m);

		m->inputs[MidiKitModule::INPUT_TRIG].channels = 1;
		// Without trig.enableIn(), a rising edge is not processed at all.
		m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f);
		m->process(Test::makeProcessArgs(0));
		m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f);
		m->process(Test::makeProcessArgs(1));
		m->host.getActiveEngine()->process();

		std::string log = drainLog(m);
		Test::destroyModule(m);
		return log;
	};

	REQUIRE(checkNotEnabled(JS_ON_TRIGGER_NOT_ENABLED).find("onTrigger") == std::string::npos);
	REQUIRE(checkNotEnabled(LUA_ON_TRIGGER_NOT_ENABLED).find("onTrigger") == std::string::npos);
}


static const char* JS_ON_TRIGGER_ENABLE_CH1_ONLY = R"(/**
 * @engine QuickJs@v1
 */
trig.enableIn(1, 1);
trig.onTrigger = function(trigPort, channel) {
    rack.log("onTrigger " + number.toString(channel));
};
)";

static const char* LUA_ON_TRIGGER_ENABLE_CH1_ONLY = R"(--[[
@engine minilua@v1
--]]
trig.enableIn(1, 1)
trig.onTrigger = function(trigPort, channel)
    rack.log("onTrigger " .. channel)
end
)";

TEST_CASE("trig.enableIn gates trig.onTrigger per channel, in both engines", "[MidiKit][CrossEngine]") {
	// Only channel 1 is enabled: a rising edge on channel 1 fires the callback,
	// a rising edge on channel 2 (never enabled) is not processed at all.
	auto checkPerChannel = [](const std::string& script) {
		MidiKitModule* m = createModule();
		m->loadScript(script);
		drainLog(m);

		m->inputs[MidiKitModule::INPUT_TRIG].channels = 2;
		// Prime both SchmittTriggers LOW first (a fresh trigger starts
		// uninitialized; the first low call locks it so a later rise is real).
		m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f, 0);
		m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f, 1);
		m->process(Test::makeProcessArgs(0));

		// Rising edge on channel 1 (enabled) fires the callback.
		m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f, 0);
		m->process(Test::makeProcessArgs(1));
		m->host.getActiveEngine()->process();
		std::string log1 = drainLog(m);

		// Rising edge on channel 2 (never enabled) is ignored.
		m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f, 1);
		m->process(Test::makeProcessArgs(2));
		m->host.getActiveEngine()->process();
		std::string log2 = drainLog(m);

		Test::destroyModule(m);
		return std::make_pair(log1, log2);
	};

	auto js = checkPerChannel(JS_ON_TRIGGER_ENABLE_CH1_ONLY);
	auto lua = checkPerChannel(LUA_ON_TRIGGER_ENABLE_CH1_ONLY);
	REQUIRE(js.first.find("onTrigger 1") != std::string::npos);
	REQUIRE(js.second.find("onTrigger") == std::string::npos);
	REQUIRE(lua.first.find("onTrigger 1") != std::string::npos);
	REQUIRE(lua.second.find("onTrigger") == std::string::npos);
}


// --- send() order, not handle-creation order ----------------------------
//
// Regression test for the flush-order bug: the engine used to push the
// out-queue in msgStore index (handle-creation) order, so a script that
// created several messages and then sent them in a different order had them
// reordered on the wire. The receiver must observe send() order. This
// creates A, B, C (handle order) but sends C, A, B, and asserts the wire
// order is C, A, B in both engines.

static const char* JS_SEND_ORDER = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let a = midi.create();
    midi.setNoteOn(a, 1, 60, 100);
    let b = midi.create();
    midi.setNoteOn(b, 1, 62, 100);
    let c = midi.create();
    midi.setNoteOn(c, 1, 64, 100);
    midiOut.send(c);   // handle 2 sent first
    midiOut.send(a);   // handle 0 sent second
    midiOut.send(b);   // handle 1 sent last
};
)";

static const char* LUA_SEND_ORDER = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local a = midi.create()
    midi.setNoteOn(a, 1, 60, 100)
    local b = midi.create()
    midi.setNoteOn(b, 1, 62, 100)
    local c = midi.create()
    midi.setNoteOn(c, 1, 64, 100)
    midiOut.send(c)
    midiOut.send(a)
    midiOut.send(b)
end
)";

TEST_CASE("out-queue flushes in send() order, not handle-creation order, in both engines", "[MidiKit][CrossEngine]") {
	EngineResult js = run(JS_SEND_ORDER);
	EngineResult lua = run(LUA_SEND_ORDER);

	// Handle order would be 60, 62, 64; send() order is 64, 60, 62. The
	// script's channel argument is 1-based, so channel 1 = internal channel 0
	// = status nibble 0x9 | 0 = 0x90.
	std::vector<uint8_t> expectC = {0x90, 64, 100};
	std::vector<uint8_t> expectA = {0x90, 60, 100};
	std::vector<uint8_t> expectB = {0x90, 62, 100};

	REQUIRE(js.sent.size() == 3);
	REQUIRE(js.sent[0].bytes == expectC);
	REQUIRE(js.sent[1].bytes == expectA);
	REQUIRE(js.sent[2].bytes == expectB);

	REQUIRE(lua.sent.size() == 3);
	REQUIRE(lua.sent[0].bytes == expectC);
	REQUIRE(lua.sent[1].bytes == expectA);
	REQUIRE(lua.sent[2].bytes == expectB);
}


// --- rack.registerContextMenu() --------------------------------------------
//
// Script-registered context-menu items (see SCRIPTING.md). Both engines
// expose the identical rack.registerContextMenu() API; the observable result
// of a script is the extracted ContextMenuSpec list (what the widget builds
// its menu from) plus the log produced when a click fires the onChange
// callback. Each behaviour is pinned as a JS_* / LUA_* script pair and
// asserted identical, exactly like the midi.* cases above.

// Loads the script, drains the load-time log, and returns the registered
// ContextMenuSpecs. When clickId >= 0, also invokes that callback (the
// programmatic equivalent of a menu click) and re-reads the specs so
// presentation-state updates (checked/selected) are observable.
struct MenuResult {
	bool loaded = false;
	std::vector<ScriptMenuItem> specs;
	std::string loadLog;
	std::string log;
};

static MenuResult runMenu(const std::string& script, int clickId = -1, int clickValue = 0) {
	MidiKitModule* m = createModule();
	m->loadScript(script);

	MenuResult r;
	r.loaded = (m->host.isQuickJsEngine()) ? (m->host.seQuickJs.ctx != nullptr)
	                                              : (m->host.seLua.L != nullptr);
	r.loadLog = drainLog(m);
	if (r.loaded) {
		// getContextMenus is asynchronous: the worker evaluates onGetValue and
		// then invokes the callback with the evaluated specs. The tests use a
		// SyncTaskWorker, which runs the worker task inline on the calling
		// thread, so the callback has already fired by the time getContextMenus
		// returns and r.specs is filled synchronously.
		auto queryMenus = [&]() {
			m->host.getActiveEngine()->getContextMenus([&r](const std::vector<ScriptMenuItem>& specs) {
				r.specs = specs;
			});
		};
		queryMenus();
		if (clickId >= 0) {
			m->host.getActiveEngine()->invokeContextMenuCallback(clickId, clickValue);
			r.log = drainLog(m);
			queryMenus();
		}
	}
	Test::destroyModule(m);
	return r;
}

// The two engines must expose identical menu models: same count, same order,
// and identical presentation fields. callbackId is engine-internal (both
// assign 1, 2, 3… in registration order), so only its validity is checked,
// not its exact value.
static void requireSameMenus(const std::vector<ScriptMenuItem>& a, const std::vector<ScriptMenuItem>& b) {
	REQUIRE(a.size() == b.size());
	for (size_t i = 0; i < a.size(); i++) {
		REQUIRE(a[i].type == b[i].type);
		REQUIRE(a[i].label == b[i].label);
		// checked/selected share one union member; only read the active one.
		if (a[i].type == ScriptMenuItem::Type::Boolean)
			REQUIRE(a[i].checked == b[i].checked);
		else
			REQUIRE(a[i].selected == b[i].selected);
		REQUIRE(a[i].options == b[i].options);
		REQUIRE(a[i].callbackId >= 1);
	}
}

static const char* JS_REGISTER_BOOL = R"(/**
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

static const char* LUA_REGISTER_BOOL = R"(--[[
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

TEST_CASE("registerContextMenu boolean menu is identical", "[MidiKit][CrossEngine]") {
	MenuResult js = runMenu(JS_REGISTER_BOOL);
	MenuResult lua = runMenu(LUA_REGISTER_BOOL);
	REQUIRE(js.loaded);
	REQUIRE(lua.loaded);
	requireSameMenus(js.specs, lua.specs);

	REQUIRE(js.specs.size() == 1);
	REQUIRE(js.specs[0].type == ScriptMenuItem::Type::Boolean);
	REQUIRE(js.specs[0].label == "Velocity to CC");
	REQUIRE(js.specs[0].checked == false);

	// A click fires onChange(true) and flips the presentation state.
	MenuResult jsOn = runMenu(JS_REGISTER_BOOL, js.specs[0].callbackId, 1);
	MenuResult luaOn = runMenu(LUA_REGISTER_BOOL, lua.specs[0].callbackId, 1);
	REQUIRE(jsOn.log.find("onChange: true") != std::string::npos);
	REQUIRE(luaOn.log.find("onChange: true") != std::string::npos);
	REQUIRE(jsOn.specs[0].checked == true);
	REQUIRE(luaOn.specs[0].checked == true);

	// And back to false.
	MenuResult jsOff = runMenu(JS_REGISTER_BOOL, js.specs[0].callbackId, 0);
	MenuResult luaOff = runMenu(LUA_REGISTER_BOOL, lua.specs[0].callbackId, 0);
	REQUIRE(jsOff.log.find("onChange: false") != std::string::npos);
	REQUIRE(luaOff.log.find("onChange: false") != std::string::npos);
}

static const char* JS_REGISTER_OPTIONS = R"(/**
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

static const char* LUA_REGISTER_OPTIONS = R"(--[[
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

TEST_CASE("registerContextMenu options menu is identical", "[MidiKit][CrossEngine]") {
	MenuResult js = runMenu(JS_REGISTER_OPTIONS);
	MenuResult lua = runMenu(LUA_REGISTER_OPTIONS);
	REQUIRE(js.loaded);
	REQUIRE(lua.loaded);
	requireSameMenus(js.specs, lua.specs);

	REQUIRE(js.specs.size() == 1);
	REQUIRE(js.specs[0].type == ScriptMenuItem::Type::Options);
	REQUIRE(js.specs[0].label == "Out mode");
	REQUIRE(js.specs[0].options.size() == 3);
	REQUIRE(js.specs[0].options[0] == "Internal");
	REQUIRE(js.specs[0].options[1] == "External");
	REQUIRE(js.specs[0].options[2] == "Both");
	REQUIRE(js.specs[0].selected == 1);

	// Clicking index 2 passes index + label to onChange and updates selection.
	MenuResult jsClick = runMenu(JS_REGISTER_OPTIONS, js.specs[0].callbackId, 2);
	MenuResult luaClick = runMenu(LUA_REGISTER_OPTIONS, lua.specs[0].callbackId, 2);
	REQUIRE(jsClick.log.find("onChange: 2 Both") != std::string::npos);
	REQUIRE(luaClick.log.find("onChange: 2 Both") != std::string::npos);
	REQUIRE(jsClick.specs[0].selected == 2);
	REQUIRE(luaClick.specs[0].selected == 2);
}

static const char* JS_REGISTER_TWO = R"(/**
 * @engine QuickJs@v1
 */
rack.registerContextMenu({ type: "boolean", label: "First", onChange: function() {} });
rack.registerContextMenu({ type: "options", label: "Second", options: ["x", "y"], onChange: function() {} });
)";

static const char* LUA_REGISTER_TWO = R"(--[[
@engine minilua@v1
--]]
rack.registerContextMenu({ type = "boolean", label = "First", onChange = function() end })
rack.registerContextMenu({ type = "options", label = "Second", options = {"x", "y"}, onChange = function() end })
)";

TEST_CASE("Multiple registerContextMenu calls keep registration order", "[MidiKit][CrossEngine]") {
	MenuResult js = runMenu(JS_REGISTER_TWO);
	MenuResult lua = runMenu(LUA_REGISTER_TWO);
	REQUIRE(js.loaded);
	REQUIRE(lua.loaded);
	requireSameMenus(js.specs, lua.specs);

	REQUIRE(js.specs.size() == 2);
	REQUIRE(js.specs[0].label == "First");
	REQUIRE(js.specs[1].label == "Second");
	REQUIRE(js.specs[0].type == ScriptMenuItem::Type::Boolean);
	REQUIRE(js.specs[1].type == ScriptMenuItem::Type::Options);
	// callbackIds are assigned monotonically in registration order.
	REQUIRE(js.specs[0].callbackId < js.specs[1].callbackId);
	REQUIRE(lua.specs[0].callbackId < lua.specs[1].callbackId);
}

static const char* JS_REGISTER_THROW = R"(/**
 * @engine QuickJs@v1
 */
rack.registerContextMenu({
	type: "boolean",
	label: "Bad",
	onChange: function(checked) { throw new Error("boom"); }
});
rack.registerContextMenu({
	type: "boolean",
	label: "Good",
	onChange: function(checked) { rack.log("good"); }
});
)";

static const char* LUA_REGISTER_THROW = R"(--[[
@engine minilua@v1
--]]
rack.registerContextMenu({
	type = "boolean",
	label = "Bad",
	onChange = function(checked) error("boom") end
})
rack.registerContextMenu({
	type = "boolean",
	label = "Good",
	onChange = function(checked) rack.log("good") end
})
)";

TEST_CASE("Throwing context-menu callback is logged and the module keeps working", "[MidiKit][CrossEngine]") {
	MenuResult js = runMenu(JS_REGISTER_THROW);
	MenuResult lua = runMenu(LUA_REGISTER_THROW);
	REQUIRE(js.loaded);
	REQUIRE(lua.loaded);
	requireSameMenus(js.specs, lua.specs);

	REQUIRE(js.specs.size() == 2);

	// A throwing onChange is reported through the log in both engines.
	MenuResult jsBad = runMenu(JS_REGISTER_THROW, js.specs[0].callbackId, 1);
	MenuResult luaBad = runMenu(LUA_REGISTER_THROW, lua.specs[0].callbackId, 1);
	REQUIRE(jsBad.log.find("Context menu callback error") != std::string::npos);
	REQUIRE(luaBad.log.find("Context menu callback error") != std::string::npos);

	// The other registered item still fires normally.
	MenuResult jsGood = runMenu(JS_REGISTER_THROW, js.specs[1].callbackId, 1);
	MenuResult luaGood = runMenu(LUA_REGISTER_THROW, lua.specs[1].callbackId, 1);
	REQUIRE(jsGood.log.find("good") != std::string::npos);
	REQUIRE(luaGood.log.find("good") != std::string::npos);

	// A callbackId that was never registered is a silent no-op in both.
	MenuResult jsNoop = runMenu(JS_REGISTER_THROW, 9999, 1);
	MenuResult luaNoop = runMenu(LUA_REGISTER_THROW, 9999, 1);
	REQUIRE(jsNoop.log.empty());
	REQUIRE(luaNoop.log.empty());
}

static const char* JS_REGISTER_BAD_NO_ONCHANGE = R"(/**
 * @engine QuickJs@v1
 */
rack.registerContextMenu({ type: "boolean", label: "X" });
)";

static const char* LUA_REGISTER_BAD_NO_ONCHANGE = R"(--[[
@engine minilua@v1
--]]
rack.registerContextMenu({ type = "boolean", label = "X" })
)";

static const char* JS_REGISTER_BAD_TYPE = R"(/**
 * @engine QuickJs@v1
 */
rack.registerContextMenu({ type: "nope", label: "X", onChange: function() {} });
)";

static const char* LUA_REGISTER_BAD_TYPE = R"(--[[
@engine minilua@v1
--]]
rack.registerContextMenu({ type = "nope", label = "X", onChange = function() end })
)";

static const char* JS_REGISTER_BAD_OPTIONS = R"(/**
 * @engine QuickJs@v1
 */
rack.registerContextMenu({ type: "options", label: "X", options: ["ok", 42], onChange: function() {} });
)";

static const char* LUA_REGISTER_BAD_OPTIONS = R"(--[[
@engine minilua@v1
--]]
rack.registerContextMenu({ type = "options", label = "X", options = {"ok", 42}, onChange = function() end })
)";

TEST_CASE("Malformed registerContextMenu fails the load identically", "[MidiKit][CrossEngine]") {
	// Missing onChange: both engines reject the registration and the load.
	MenuResult js = runMenu(JS_REGISTER_BAD_NO_ONCHANGE);
	MenuResult lua = runMenu(LUA_REGISTER_BAD_NO_ONCHANGE);
	REQUIRE_FALSE(js.loaded);
	REQUIRE_FALSE(lua.loaded);

	// Unknown type: both fail with the same diagnostic wording.
	js = runMenu(JS_REGISTER_BAD_TYPE);
	lua = runMenu(LUA_REGISTER_BAD_TYPE);
	REQUIRE_FALSE(js.loaded);
	REQUIRE_FALSE(lua.loaded);
	REQUIRE(js.loadLog.find("registerContextMenu: type must be") != std::string::npos);
	REQUIRE(lua.loadLog.find("registerContextMenu: type must be") != std::string::npos);

	// Non-string element in options (the Lua lua_isstring-coercion divergence
	// was caught here — see repo notes): both engines reject it.
	js = runMenu(JS_REGISTER_BAD_OPTIONS);
	lua = runMenu(LUA_REGISTER_BAD_OPTIONS);
	REQUIRE_FALSE(js.loaded);
	REQUIRE_FALSE(lua.loaded);
}

static const char* JS_ONGETVALUE_NO_RETURN_BOOL = R"(/**
 * @engine QuickJs@v1
 */
rack.registerContextMenu({
	type: "boolean",
	label: "No return",
	onGetValue: function() {},
	onChange: function() {}
});
)";

static const char* LUA_ONGETVALUE_NO_RETURN_BOOL = R"(--[[
@engine minilua@v1
--]]
rack.registerContextMenu({
	type = "boolean",
	label = "No return",
	onGetValue = function() end,
	onChange = function() end
})
)";

static const char* JS_ONGETVALUE_NO_RETURN_OPTIONS = R"(/**
 * @engine QuickJs@v1
 */
rack.registerContextMenu({
	type: "options",
	label: "No return",
	options: ["A", "B", "C"],
	onGetValue: function() {},
	onChange: function() {}
});
)";

static const char* LUA_ONGETVALUE_NO_RETURN_OPTIONS = R"(--[[
@engine minilua@v1
--]]
rack.registerContextMenu({
	type = "options",
	label = "No return",
	options = {"A", "B", "C"},
	onGetValue = function() end,
	onChange = function() end
})
)";

TEST_CASE("onGetValue returning nothing defaults to false/0", "[MidiKit][CrossEngine]") {
	// Boolean: a missing return yields undefined (JS) / nil (Lua), which both
	// engines coerce to false.
	MenuResult js = runMenu(JS_ONGETVALUE_NO_RETURN_BOOL);
	MenuResult lua = runMenu(LUA_ONGETVALUE_NO_RETURN_BOOL);
	REQUIRE(js.loaded);
	REQUIRE(lua.loaded);
	requireSameMenus(js.specs, lua.specs);
	REQUIRE(js.specs.size() == 1);
	REQUIRE(js.specs[0].type == ScriptMenuItem::Type::Boolean);
	REQUIRE(js.specs[0].checked == false);
	REQUIRE(lua.specs[0].checked == false);

	// Options: a missing return yields undefined (JS) / nil (Lua), which both
	// engines coerce to 0 (the first option).
	MenuResult jsOpt = runMenu(JS_ONGETVALUE_NO_RETURN_OPTIONS);
	MenuResult luaOpt = runMenu(LUA_ONGETVALUE_NO_RETURN_OPTIONS);
	REQUIRE(jsOpt.loaded);
	REQUIRE(luaOpt.loaded);
	requireSameMenus(jsOpt.specs, luaOpt.specs);
	REQUIRE(jsOpt.specs.size() == 1);
	REQUIRE(jsOpt.specs[0].type == ScriptMenuItem::Type::Options);
	REQUIRE(jsOpt.specs[0].selected == 0);
	REQUIRE(luaOpt.specs[0].selected == 0);
}


// --- 32-handle store cap (review D1 consequence / A3 partial flush) ------
//
// midi.create()/midi.clone()/midi.createNRPN() fail once the 32-handle
// per-callback store is full, aborting the rest of the callback. The error
// wording is identical in both engines (unified: "midi.create: message store
// full" etc.). Per A3, messages already sent before the error are still
// flushed — a multi-message sequence can be emitted partially — while
// anything created after the error is dropped.

static const char* JS_STORE_FULL = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(midiPort, msg) {
    // Two messages sent before the overflow — these must still be flushed.
    let m1 = midi.create();
    midi.setNoteOn(m1, 1, 60, 100);
    midiOut.send(m1);
    let m2 = midi.create();
    midi.setCc(m2, 1, 20, 100);
    midiOut.send(m2);

    // Handle 0 is the incoming message (msgCount starts at 1) and m1/m2
    // above each consume a slot, so this loop crosses the 32-handle cap and
    // midi.create() throws mid-callback. The exact overflow point doesn't
    // matter — the point is that it throws here.
    for (let i = 0; i < 33; i++) {
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

    for i = 1, 33 do
        midi.create()
    end

    local m3 = midi.create()
    midi.setNoteOn(m3, 1, 61, 100)
    midiOut.send(m3)
end
)";

TEST_CASE("midi.create past the 32-handle cap errors and flushes only pre-error sends", "[MidiKit][CrossEngine]") {
	EngineResult js = run(JS_STORE_FULL);
	EngineResult lua = run(LUA_STORE_FULL);

	// Identical error wording in both engines (unified wording).
	CATCH_INFO("JS log:\n" << js.log);
	CATCH_INFO("Lua log:\n" << lua.log);
	REQUIRE(js.log.find("midi.create: message store full") != std::string::npos);
	REQUIRE(lua.log.find("midi.create: message store full") != std::string::npos);

	// Partial flush (A3): both pre-error messages go out, identically, with
	// the exact bytes the scripts requested. size == 2 also proves the
	// message created after the error never went out.
	REQUIRE(js.sent.size() == 2);
	REQUIRE(lua.sent.size() == 2);
	REQUIRE(js.sent[0].bytes == lua.sent[0].bytes);
	REQUIRE(js.sent[1].bytes == lua.sent[1].bytes);
	REQUIRE(js.sent[0].bytes == std::vector<uint8_t>({0x90, 0x3c, 0x64})); // note-on ch1 note 60 vel 100
	REQUIRE(js.sent[1].bytes == std::vector<uint8_t>({0xb0, 0x14, 0x64})); // cc ch1 cc 20 val 100
}


// --- createNRPN/createCc14bit store-boundary (audit #4) ------------------
//
// Slot 0 of the 32-slot store is the incoming message, so msgCount starts at
// 1 inside onMessage. createNRPN() needs 4 consecutive slots and
// createCc14bit() 2; the last valid starting positions are 28 and 30. The
// QuickJS bounds checks used >= instead of >, wrongly rejecting those last
// valid positions (Lua was already correct). These pin the boundary: at the
// last valid slot the calls succeed and emit spec-compliant bytes; one slot
// past, they error "store full".

static const char* JS_NRPN_AT_BOUNDARY = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    // 27 creates -> msgCount 28; createNRPN() at slot 28 (slots 28-31) fits.
    for (let i = 0; i < 27; i++) {
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
    for i = 1, 27 do
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
    // 29 creates -> msgCount 30; createCc14bit() at slot 30 (slots 30-31) fits.
    for (let i = 0; i < 29; i++) {
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
    for i = 1, 29 do
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
    for (let i = 0; i < 28; i++) {
        midi.create();
    }
    midi.createNRPN();   // slot 29 needs slots 29-32; only 29-31 exist
};
)";

static const char* LUA_NRPN_PAST_BOUNDARY = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    for i = 1, 28 do
        midi.create()
    end
    midi.createNRPN()
end
)";

static const char* JS_CC14_PAST_BOUNDARY = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    for (let i = 0; i < 30; i++) {
        midi.create();
    }
    midi.createCc14bit();   // slot 31 needs slots 31-32; only 31 exists
};
)";

static const char* LUA_CC14_PAST_BOUNDARY = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    for i = 1, 30 do
        midi.create()
    end
    midi.createCc14bit()
end
)";

TEST_CASE("createNRPN at the last valid store slot succeeds in both engines", "[MidiKit][CrossEngine]") {
	EngineResult js = run(JS_NRPN_AT_BOUNDARY);
	EngineResult lua = run(LUA_NRPN_AT_BOUNDARY);
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
	EngineResult js = run(JS_CC14_AT_BOUNDARY);
	EngineResult lua = run(LUA_CC14_AT_BOUNDARY);
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
	EngineResult js = run(JS_NRPN_PAST_BOUNDARY);
	EngineResult lua = run(LUA_NRPN_PAST_BOUNDARY);
	REQUIRE(js.log.find("midi.createNRPN: message store full") != std::string::npos);
	REQUIRE(lua.log.find("midi.createNRPN: message store full") != std::string::npos);
	REQUIRE(js.sent.empty());
	REQUIRE(lua.sent.empty());
}

TEST_CASE("createCc14bit one slot past the boundary errors in both engines", "[MidiKit][CrossEngine]") {
	EngineResult js = run(JS_CC14_PAST_BOUNDARY);
	EngineResult lua = run(LUA_CC14_PAST_BOUNDARY);
	REQUIRE(js.log.find("midi.createCc14bit: message store full") != std::string::npos);
	REQUIRE(lua.log.find("midi.createCc14bit: message store full") != std::string::npos);
	REQUIRE(js.sent.empty());
	REQUIRE(lua.sent.empty());
}


// --- runtime API mutation: forbidden by contract, must not crash ---------
//
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
	MidiKitModule* m = createModule();
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

	Test::destroyModule(m);
}