#include "MidiKit.test.hpp"

using StoermelderPackOne::MidiScript::ContextMenuSpec;

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

static midi::Message noteOn(int ch, int note, int vel) {
	midi::Message msg;
	msg.setSize(3);
	msg.setStatus(0x9);
	msg.setChannel(ch);
	msg.setNote(note);
	msg.setValue(vel);
	return msg;
}

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
	REQUIRE(m->activeEngine != nullptr);

	midi::Message inCopy = in;
	m->activeEngine->processInMessage(0, inCopy);
	m->activeEngine->process();

	int port, ticks;
	midi::Message out;
	while (m->activeEngine->processOutMessage(port, out, ticks)) {
		r.sent.push_back(toSent(port, ticks, out));
	}
	r.log = drainLog(m);

	Test::destroyModule(m);
	return r;
}

// Default-input overload: most cases don't care what the incoming message
// is, only what the script does once onMidiMessage fires, so a plain NoteOn
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
// onMidiMessage runs entirely at load time (see the outside-callback-warning
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
// gives requireEquivalent. The script runs at load time (no onMidiMessage
// needed), so this bypasses run()'s incoming-NoteOn feed entirely.
//
// loadScript() itself writes framework chatter to the same log ("Script
// loaded", "No onMidiMessage(...) defined", ...), which would otherwise leak
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
// ever saw the value — so instead the script logs inside onMidiMessage and
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
 * @engine QuickJs
 */
rack.log("PROBE:" + number.toString(number.crossfade(0, 10, 0.5)));
rack.log("PROBE:" + number.toString(number.crossfade(100, 200, 0.25)));
rack.log("PROBE:" + number.toString(number.crossfade(-5, 5, 0.75)));
)";

static const char* LUA_NUMBER_CROSSFADE = R"(--[[
@engine Lua
--]]
rack.log("PROBE:" .. number.toString(number.crossfade(0, 10, 0.5)))
rack.log("PROBE:" .. number.toString(number.crossfade(100, 200, 0.25)))
rack.log("PROBE:" .. number.toString(number.crossfade(-5, 5, 0.75)))
)";

TEST_CASE("number.crossfade is identical", "[MidiKit][CrossEngine]") {
	requireLoggedValues(JS_NUMBER_CROSSFADE, LUA_NUMBER_CROSSFADE, {"5", "125", "2.5"});
}


static const char* JS_NUMBER_RESCALE = R"(/**
 * @engine QuickJs
 */
rack.log("PROBE:" + number.toString(number.rescale(5, 0, 10, 0, 100)));
)";

static const char* LUA_NUMBER_RESCALE = R"(--[[
@engine Lua
--]]
rack.log("PROBE:" .. number.toString(number.rescale(5, 0, 10, 0, 100)))
)";

TEST_CASE("number.rescale is identical", "[MidiKit][CrossEngine]") {
	requireLoggedValues(JS_NUMBER_RESCALE, LUA_NUMBER_RESCALE, {"50"});
}


static const char* JS_NUMBER_TOSTRING = R"(/**
 * @engine QuickJs
 */
rack.log("PROBE:" + number.toString(42));
rack.log("PROBE:" + number.toString(3.14));
rack.log("PROBE:" + number.toString(-100));
rack.log("PROBE:" + number.toString(1 / 3));
rack.log("PROBE:" + number.toString(0));
)";

static const char* LUA_NUMBER_TOSTRING = R"(--[[
@engine Lua
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
 * @engine QuickJs
 */
rack.log("PROBE:" + number.toString(rack.random()));
)";

static const char* LUA_RACK_RANDOM = R"(--[[
@engine Lua
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
 * @engine QuickJs
 */
rack.log("PROBE:" + number.toString(rack.getFrame()));
)";

static const char* LUA_RACK_GET_FRAME = R"(--[[
@engine Lua
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
 * @engine QuickJs
 */
rack.onMidiMessage = function(port, msg) {
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
@engine Lua
--]]
rack.onMidiMessage = function(midiPort, msg)
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
 * @engine QuickJs
 */
rack.onMidiMessage = function(port, msg) {
    rack.log("Member channels: ", 2, "-", 16);
    rack.log("note on ch", 3, " note=", 60, " -> ", 64);
    rack.log("ok=", true, " n=", 1 / 3, " nil=", null);
};
)";

static const char* LUA_RACK_LOG_MULTI = R"(--[[
@engine Lua
--]]
rack.onMidiMessage = function(midiPort, msg)
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
 * @engine QuickJs
 */
rack.onMidiMessage = function(port, msg) {
    let out = midi.create();
    midi.setNoteOn(out, 1, 60, 100);
    midiOut.send(out);
};
)";

static const char* LUA_NOTE_ON = R"(--[[
@engine Lua
--]]
rack.onMidiMessage = function(midiPort, msg)
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
 * @engine QuickJs
 * @description CC number +1 passthrough
 */
rack.onMidiMessage = function(port, msg) {
    if (midi.isCc(msg)) {
        midi.setNote(msg, midi.getNote(msg) + 1);
        midiOut.send(msg);
    }
};
)";

static const char* LUA_CC_REROUTE = R"(--[[
@engine Lua
@description CC number +1 passthrough
--]]
rack.onMidiMessage = function(port, msg)
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
 * @engine QuickJs
 */
rack.onMidiMessage = function(port, msg) {
    let out = midi.create();
    midi.setCc(out, 2, 74, 127);
    midiOut.send(out);
};
)";

static const char* LUA_CC = R"(--[[
@engine Lua
--]]
rack.onMidiMessage = function(midiPort, msg)
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
 * @engine QuickJs
 */
rack.onMidiMessage = function(port, msg) {
    let out = midi.create();
    midi.setCc(out, 1, 10, 500);
    midiOut.send(out);
};
)";

static const char* LUA_CC_CLAMP = R"(--[[
@engine Lua
--]]
rack.onMidiMessage = function(midiPort, msg)
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
 * @engine QuickJs
 */
rack.onMidiMessage = function(port, msg) {
    let out = midi.create();
    midi.setSysEx(out, "43104c0000");
    midiOut.send(out);
};
)";

static const char* LUA_SYSEX = R"(--[[
@engine Lua
--]]
rack.onMidiMessage = function(midiPort, msg)
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
 * @engine QuickJs
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
@engine Lua
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
 * @engine QuickJs
 */
rack.onMidiMessage = function(port, msg) {
    let out = midi.create();
    midi.setNoteOn(out, 1, 60, 100);
    midiOut.sendAfterTrigger(out, 10);
};
)";

static const char* LUA_SEND_AFTER_TRIGGER = R"(--[[
@engine Lua
--]]
rack.onMidiMessage = function(midiPort, msg)
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
 * @engine QuickJs
 */
rack.onMidiMessage = function(port, msg) {
    let out = midi.create();
    midi.setNoteOn(out, 1, 60, 100);
    midiOut.send(out);
};
)";

static const char* LUA_HEADER_ONLY = R"(--[[
@engine Lua
--]]
rack.onMidiMessage = function(midiPort, msg)
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
 * @engine QuickJs
 */
rack.onMidiMessage = function(port, msg) {
    let out = midi.create();
    midi.setRaw(out, "f11a");
    midiOut.send(out);
};
)";

static const char* LUA_RAW = R"(--[[
@engine Lua
--]]
rack.onMidiMessage = function(midiPort, msg)
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
 * @engine QuickJs
 */
rack.onMidiMessage = function(port, msg) {
    let out = midi.create();
    midi.setPitchWheel(out, 2, 12345);
    midiOut.send(out);
};
)";

static const char* LUA_PITCH_WHEEL = R"(--[[
@engine Lua
--]]
rack.onMidiMessage = function(midiPort, msg)
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
 * @engine QuickJs
 */
rack.onMidiMessage = function(port, msg) {
    let out = midi.create();
    midi.setProgramChange(out, 4, 10);
    midiOut.send(out);
};
)";

static const char* LUA_PROGRAM_CHANGE = R"(--[[
@engine Lua
--]]
rack.onMidiMessage = function(midiPort, msg)
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
 * @engine QuickJs
 */
let low = midi.create();
midi.setProgramChange(low, 4, 0);
rack.log("PROBE:" + number.toString(midi.getProgramChange(low)));

let high = midi.create();
midi.setProgramChange(high, 4, 127);
rack.log("PROBE:" + number.toString(midi.getProgramChange(high)));
)";

static const char* LUA_GET_PROGRAM_CHANGE = R"(--[[
@engine Lua
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
 * @engine QuickJs
 */
rack.onMidiMessage = function(port, msg) {
    let out = midi.create();
    midi.setChanPressure(out, 5, 80);
    midiOut.send(out);
};
)";

static const char* LUA_CHAN_PRESSURE = R"(--[[
@engine Lua
--]]
rack.onMidiMessage = function(midiPort, msg)
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
 * @engine QuickJs
 */
rack.onMidiMessage = function(port, msg) {
    let out = midi.create();
    midi.setKeyPressure(out, 6, 64, 90);
    midiOut.send(out);
};
)";

static const char* LUA_KEY_PRESSURE = R"(--[[
@engine Lua
--]]
rack.onMidiMessage = function(midiPort, msg)
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
 * @engine QuickJs
 */
rack.onMidiMessage = function(port, msg) {
    let outHigh = midi.create();
    midi.setKeyPressure(outHigh, 6, 64, 200);
    midiOut.send(outHigh);
    let outLow = midi.create();
    midi.setKeyPressure(outLow, 6, 64, -1);
    midiOut.send(outLow);
};
)";

static const char* LUA_KEY_PRESSURE_CLAMP = R"(--[[
@engine Lua
--]]
rack.onMidiMessage = function(midiPort, msg)
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
 * @engine QuickJs
 */
rack.onMidiMessage = function(port, msg) {
    let outHigh = midi.create();
    midi.setNoteOn(outHigh, 1, 60, 200);
    midiOut.send(outHigh);
    let outLow = midi.create();
    midi.setNoteOn(outLow, 1, 60, -1);
    midiOut.send(outLow);
};
)";

static const char* LUA_NOTE_ON_CLAMP = R"(--[[
@engine Lua
--]]
rack.onMidiMessage = function(midiPort, msg)
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
 * @engine QuickJs
 */
rack.onMidiMessage = function(port, msg) {
    let src = midi.create();
    midi.setNoteOn(src, 1, 60, 100);
    let copy = midi.clone(src);
    midi.setNote(copy, 70);      // edit the clone only
    midiOut.send(src);           // source unchanged -> note 60
    midiOut.send(copy);          // clone carries the edit -> note 70
};
)";

static const char* LUA_MIDI_CLONE = R"(--[[
@engine Lua
--]]
rack.onMidiMessage = function(midiPort, msg)
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
 * @engine QuickJs
 */
rack.onMidiMessage = function(port, msg) {
    let copy = midi.clone(msg);   // deep copy of the incoming note-on
    midi.setChannel(copy, 5);     // reroute to channel 5
    midiOut.send(copy);
};
)";

static const char* LUA_MIDI_CLONE_INCOMING = R"(--[[
@engine Lua
--]]
rack.onMidiMessage = function(midiPort, msg)
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
 * @engine QuickJs
 */
rack.onMidiMessage = function(port, msg) {
    let out = midi.create();
    midi.setNoteOff(out, 7, 48);
    midiOut.send(out);
};
)";

static const char* LUA_NOTE_OFF = R"(--[[
@engine Lua
--]]
rack.onMidiMessage = function(midiPort, msg)
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
 * @engine QuickJs
 */
rack.onMidiMessage = function(port, msg) {
    let out = midi.create();
    midi.setNoteOff(out, 7, 48, 100);
    midiOut.send(out);
};
)";

static const char* LUA_NOTE_OFF_VEL = R"(--[[
@engine Lua
--]]
rack.onMidiMessage = function(midiPort, msg)
    local out = midi.create()
    midi.setNoteOff(out, 7, 48, 100)
    midiOut.send(out)
end
)";

TEST_CASE("setNoteOff with velocity produces identical wire bytes", "[MidiKit][CrossEngine]") {
	requireEquivalent(JS_NOTE_OFF_VEL, LUA_NOTE_OFF_VEL);
}

static const char* JS_NOTE_OFF_VEL_PROBE = R"(/**
 * @engine QuickJs
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
@engine Lua
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
 * @engine QuickJs
 */
rack.onMidiMessage = function(port, msg) {
    let msb = midi.create();
    let lsb = midi.create();
    midi.setCc14bit(msb, lsb, 8, 1, 100.5);
    midiOut.send(msb);
    midiOut.send(lsb);
};
)";

static const char* LUA_CC_14BIT = R"(--[[
@engine Lua
--]]
rack.onMidiMessage = function(midiPort, msg)
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


// --- setNRPN (4 chained CC messages) ------------------------------------
//
// midiOut.send(nrpnHandle) flushes all 4 underlying CC messages in order
// when passed the first handle of an NRPN quad (per SCRIPTING.md), so
// sending it is what actually exercises setNRPN's byte layout end to end.

static const char* JS_NRPN = R"(/**
 * @engine QuickJs
 */
rack.onMidiMessage = function(port, msg) {
    let nrpn = midi.createNRPN();
    midi.setNRPN(nrpn, 9, 1234, 5678);
    midiOut.send(nrpn);
};
)";

static const char* LUA_NRPN = R"(--[[
@engine Lua
--]]
rack.onMidiMessage = function(midiPort, msg)
    local nrpn = midi.createNRPN()
    midi.setNRPN(nrpn, 9, 1234, 5678)
    midiOut.send(nrpn)
end
)";

TEST_CASE("setNRPN produces identical 4-message wire sequence", "[MidiKit][CrossEngine]") {
	requireEquivalent(JS_NRPN, LUA_NRPN);
}

// Cross-engine equivalence above only pins JS and Lua to each other — it
// can't catch a bug shared by both (as happened: both engines flushed the
// quad as CC98/CC99/CC38/CC6, MSB-after-LSB for both pairs, which desyncs
// MidiProcessor::processCc's NRPN state machine and corrupts every value
// after the first). This asserts the actual wire bytes/order against the
// spec: CC99 (param MSB), CC98 (param LSB), CC6 (data MSB), CC38 (data LSB).
TEST_CASE("setNRPN wire order is spec-compliant (MSB before LSB)", "[MidiKit]") {
	EngineResult r = run(JS_NRPN);
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
 * @engine QuickJs
 */
rack.onMidiMessage = function(port, msg) {
    let n1 = midi.createNRPN();
    midi.setNRPN(n1, 9, 1234, 5678);
    let n2 = midi.createNRPN();
    midi.setNRPN(n2, 9, 100, 200);
    midiOut.send(n2);   // created second, sent first
    midiOut.send(n1);   // created first, sent last
};
)";

static const char* LUA_NRPN_SEND_ORDER = R"(--[[
@engine Lua
--]]
rack.onMidiMessage = function(midiPort, msg)
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
 * @engine QuickJs
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
@engine Lua
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
 * @engine QuickJs
 */
let note = midi.create();
midi.setNoteOn(note, 5, 60, 100);
rack.log("PROBE:" + number.toString(midi.getChannel(note)));

let clock = midi.create();
midi.setSysEx(clock, "");
rack.log("PROBE:" + number.toString(midi.getChannel(clock)));
)";

static const char* LUA_GET_CHANNEL_SENTINEL = R"(--[[
@engine Lua
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
 * @engine QuickJs
 */
input.enable(1);
input.enable(2);
)";

static const char* LUA_INPUT_ENABLE = R"(--[[
@engine Lua
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
 * @engine QuickJs
 */
input.enable(1);
rack.log("PROBE:" + number.toString(input.getVoltage(1)));
rack.log("PROBE:" + number.toString(input.getVoltage(1, 1)));
rack.log("PROBE:" + (input.isHigh(1) ? "high" : "low"));
rack.log("PROBE:" + (input.isLow(1) ? "low" : "high"));
)";

static const char* LUA_INPUT_GET_VOLTAGE = R"(--[[
@engine Lua
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
 * @engine QuickJs
 */
rack.onMidiMessage = function(port, msg) {
    let out = midi.create();
    midi.setCc(out, 1, 1, 0);
    midi.setValue(out, trig.getTicks(1));
    midiOut.send(out);
};
)";

static const char* LUA_TRIG_GET_TICKS = R"(--[[
@engine Lua
--]]
rack.onMidiMessage = function(midiPort, msg)
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
		m->activeEngine->processInMessage(0, in);
		m->activeEngine->process();

		int port, ticks;
		midi::Message out;
		REQUIRE(m->activeEngine->processOutMessage(port, out, ticks));
		int result = out.getValue();
		Test::destroyModule(m);
		return result;
	};

	REQUIRE(ticksAfterTwoPulses(JS_TRIG_GET_TICKS) == ticksAfterTwoPulses(LUA_TRIG_GET_TICKS));
}


// --- trig.setGate / setHigh / setLow / setTrigger ---------------------------
//
// These write to the module's own trigger-output state, which is plain
// module state comparable directly across engines.

static const char* JS_TRIG_SET_FUNCTIONS = R"(/**
 * @engine QuickJs
 */
trig.setGate(1, 100);
trig.setHigh(1);
trig.setLow(1);
trig.setTrigger(1);
)";

static const char* LUA_TRIG_SET_FUNCTIONS = R"(--[[
@engine Lua
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

		Module::ProcessArgs args;
		args.sampleTime = 1.0f / 44100.0f;
		args.sampleRate = 44100.0f;
		args.frame = 0;
		m->process(args);

		float voltage = m->outputs[MidiKitModule::OUTPUT_TRIG].getVoltage(0);
		bool active = m->outputTriggerActive[0];
		Test::destroyModule(m);
		return std::make_pair(voltage, active);
	};

	auto js = checkTriggerActive(JS_TRIG_SET_FUNCTIONS);
	auto lua = checkTriggerActive(LUA_TRIG_SET_FUNCTIONS);
	REQUIRE(js.first == Catch::Approx(10.0f));
	REQUIRE(js.second == true);
	REQUIRE(js == lua);
}


// --- param.enable ------------------------------------------------------------

static const char* JS_PARAM_ENABLE = R"(/**
 * @engine QuickJs
 */
param.enable(1);
param.enable(3);
)";

static const char* LUA_PARAM_ENABLE = R"(--[[
@engine Lua
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
 * @engine QuickJs
 */
rack.onMidiMessage = function(port, msg) {
    param.enable(1);
    let out = midi.create();
    midi.setCc(out, 1, 1, 0);
    midi.setValue(out, Math.floor(param.getValue(1) * 127));
    midiOut.send(out);
};
)";

static const char* LUA_PARAM_GET_VALUE = R"(--[[
@engine Lua
--]]
rack.onMidiMessage = function(midiPort, msg)
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
		m->activeEngine->processInMessage(0, in);
		m->activeEngine->process();

		int port, ticks;
		midi::Message out;
		REQUIRE(m->activeEngine->processOutMessage(port, out, ticks));
		int result = out.getValue();
		Test::destroyModule(m);
		return result;
	};

	REQUIRE(valueAt(JS_PARAM_GET_VALUE, 0.5f) == valueAt(LUA_PARAM_GET_VALUE, 0.5f));
	REQUIRE(valueAt(JS_PARAM_GET_VALUE, 1.0f) == valueAt(LUA_PARAM_GET_VALUE, 1.0f));
}


// --- midiOut.selectPort ------------------------------------------------

static const char* JS_SELECT_PORT = R"(/**
 * @engine QuickJs
 */
let msg = midi.create();
midi.setNoteOn(msg, 1, 60, 100);

rack.onMidiMessage = function(port, msg) {
    midiOut.selectPort(1);
    midiOut.send(msg);
};
)";

static const char* LUA_SELECT_PORT = R"(--[[
@engine Lua
--]]
msg = midi.create()
midi.setNoteOn(msg, 1, 60, 100)

rack.onMidiMessage = function(midiPort, msg)
    midiOut.selectPort(1)
    midiOut.send(msg)
end
)";

TEST_CASE("midiOut.selectPort produces identical output port in both engines", "[MidiKit][CrossEngine]") {
	requireEquivalent(JS_SELECT_PORT, LUA_SELECT_PORT);
}


static const char* JS_SELECT_PORT_STICKY = R"(/**
 * @engine QuickJs
 */
rack.onMidiMessage = function(port, msg) {
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
@engine Lua
--]]
rack.onMidiMessage = function(midiPort, msg)
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
 * @engine QuickJs
 */
rack.onMidiMessage = function(port, msg) {
    midiOut.selectPort(2);
};
)";

static const char* LUA_SELECT_PORT_INVALID = R"(--[[
@engine Lua
--]]
rack.onMidiMessage = function(midiPort, msg)
    midiOut.selectPort(2)
end
)";

TEST_CASE("midiOut.selectPort rejects an out-of-range port identically", "[MidiKit][CrossEngine]") {
	requireEquivalentLog(JS_SELECT_PORT_INVALID, LUA_SELECT_PORT_INVALID, "onMidiMessage error", true);
}


// --- midiOut.sendAfterMs ------------------------------------------------

static const char* JS_SEND_AFTER_MS = R"(/**
 * @engine QuickJs
 */
let msg = midi.create();
midi.setNoteOn(msg, 1, 60, 100);

rack.onMidiMessage = function(port, msg) {
    midiOut.sendAfterMs(msg, 100);
};
)";

static const char* LUA_SEND_AFTER_MS = R"(--[[
@engine Lua
--]]
msg = midi.create()
midi.setNoteOn(msg, 1, 60, 100)

rack.onMidiMessage = function(midiPort, msg)
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
		m->activeEngine->processInMessage(0, in);
		m->activeEngine->process();

		int port, ticks;
		midi::Message out;
		REQUIRE(m->activeEngine->processOutMessage(port, out, ticks));
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
 * @engine QuickJs
 */
let msg = midi.create();
midi.setNoteOn(msg, 1, 60, 100);

rack.onMidiMessage = function(port, msg) {
    midiOut.selectPort(1);
    midiOut.sendAfterTrigger(msg, 10);
};
)";

static const char* LUA_SEND_AFTER_TRIGGER_SELECTED_PORT = R"(--[[
@engine Lua
--]]
msg = midi.create()
midi.setNoteOn(msg, 1, 60, 100)

rack.onMidiMessage = function(midiPort, msg)
    midiOut.selectPort(1)
    midiOut.sendAfterTrigger(msg, 10)
end
)";

TEST_CASE("sendAfterTrigger uses the selected port identically", "[MidiKit][CrossEngine]") {
	requireEquivalent(JS_SEND_AFTER_TRIGGER_SELECTED_PORT, LUA_SEND_AFTER_TRIGGER_SELECTED_PORT);
}


static const char* JS_SEND_AFTER_TRIGGER_TRIGPORT = R"(/**
 * @engine QuickJs
 */
let msg = midi.create();
midi.setNoteOn(msg, 1, 60, 100);

rack.onMidiMessage = function(port, msg) {
    midiOut.selectPort(1);
    midiOut.sendAfterTrigger(msg, 1, 10);
};
)";

static const char* LUA_SEND_AFTER_TRIGGER_TRIGPORT = R"(--[[
@engine Lua
--]]
msg = midi.create()
midi.setNoteOn(msg, 1, 60, 100)

rack.onMidiMessage = function(midiPort, msg)
    midiOut.selectPort(1)
    midiOut.sendAfterTrigger(msg, 1, 10)
end
)";

TEST_CASE("sendAfterTrigger with explicit trigPort (3 args) is identical", "[MidiKit][CrossEngine]") {
	requireEquivalent(JS_SEND_AFTER_TRIGGER_TRIGPORT, LUA_SEND_AFTER_TRIGGER_TRIGPORT);
}


// --- midi.create() / midi.createNRPN() outside onMidiMessage() -----------
//
// The message store is reset on every callback, so a handle created at top
// level is silently invalidated before it can be used. That reset is
// documented and intended; these tests pin the warning that makes it
// visible, and that the two engines use the same wording for it (unlike
// parse-error text, which #13's write-up notes differs deliberately).

static const char* OUTSIDE_CALLBACK_WARNING = "called outside a callback";

static const char* JS_TOPLEVEL_CREATE = R"(/**
 * @engine QuickJs
 */
let g = midi.create();
)";

static const char* LUA_TOPLEVEL_CREATE = R"(--[[
@engine Lua
--]]
g = midi.create()
)";

TEST_CASE("midi.create outside onMidiMessage warns identically", "[MidiKit][CrossEngine]") {
	requireEquivalentLog(JS_TOPLEVEL_CREATE, LUA_TOPLEVEL_CREATE, OUTSIDE_CALLBACK_WARNING, true);
}


static const char* JS_TOPLEVEL_CREATE_NRPN = R"(/**
 * @engine QuickJs
 */
let g = midi.createNRPN();
)";

static const char* LUA_TOPLEVEL_CREATE_NRPN = R"(--[[
@engine Lua
--]]
g = midi.createNRPN()
)";

TEST_CASE("midi.createNRPN outside onMidiMessage warns identically", "[MidiKit][CrossEngine]") {
	requireEquivalentLog(JS_TOPLEVEL_CREATE_NRPN, LUA_TOPLEVEL_CREATE_NRPN, OUTSIDE_CALLBACK_WARNING, true);
}


static const char* JS_CALLBACK_CREATE = R"(/**
 * @engine QuickJs
 */
rack.onMidiMessage = function(port, msg) {
    let m = midi.create();
    midi.setCc(m, 1, 20, 100);
    midiOut.send(m);
};
)";

static const char* LUA_CALLBACK_CREATE = R"(--[[
@engine Lua
--]]
rack.onMidiMessage = function(midiPort, msg)
    local m = midi.create()
    midi.setCc(m, 1, 20, 100)
    midiOut.send(m)
end
)";

TEST_CASE("midi.create inside onMidiMessage does not warn in either engine", "[MidiKit][CrossEngine]") {
	requireEquivalentLog(JS_CALLBACK_CREATE, LUA_CALLBACK_CREATE, OUTSIDE_CALLBACK_WARNING, false);
}


// --- onLoad ----------------------------------------------------------------

static const char* JS_ON_LOAD = R"(/**
 * @engine QuickJs
 */
rack.onMidiMessage = function(midiPort, msg) {};
rack.onLoad = function() {
    rack.log("onLoad ran");
    let msg = midi.create();
    midi.setNoteOn(msg, 1, 60, 100);
    midiOut.send(msg);
};
)";

static const char* LUA_ON_LOAD = R"(--[[
@engine Lua
--]]
rack.onMidiMessage = function(midiPort, msg) end
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
		REQUIRE(m->activeEngine->processOutMessage(port, out, ticks));
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
 * @engine QuickJs
 */
let x = Math.max(3, 7);
)";

static const char* LUA_NO_ON_LOAD = R"(--[[
@engine Lua
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
 * @engine QuickJs
 */
let msg = midi.create();
midi.setSysEx(msg, "43104c0000");
rack.log("PROBE:" + (midi.isSysEx(msg) ? "yes" : "no"));
)";

static const char* LUA_TOPLEVEL_SYSEX = R"(--[[
@engine Lua
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
 * @engine QuickJs
 */
rack.onMidiMessage = function(midiPort, msg) {};
rack.onUnload = function() {
    rack.log("onUnload ran");
    let msg = midi.create();
    midi.setNoteOff(msg, 1, 60);
    midiOut.send(msg);
};
)";

static const char* LUA_ON_UNLOAD = R"(--[[
@engine Lua
--]]
rack.onMidiMessage = function(midiPort, msg) end
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

		// clearScript() reloads an empty script, which always selects QuickJs
		// (loadScript defaults to QuickJs when "@engine Lua" isn't found) — so
		// activeEngine no longer points at the engine onUnload actually ran
		// on by the time clearScript() returns. Capture it first.
		MidiScriptEngine* engine = m->activeEngine;
		m->clearScript();

		std::string log = drainLog(m);
		REQUIRE(log.find("onUnload ran") != std::string::npos);

		int port, ticks;
		midi::Message out;
		REQUIRE(engine->processOutMessage(port, out, ticks));
		auto sent = toSent(port, ticks, out);
		Test::destroyModule(m);
		return sent;
	};

	auto js = checkOnUnload(JS_ON_UNLOAD);
	auto lua = checkOnUnload(LUA_ON_UNLOAD);
	REQUIRE(js.port == lua.port);
	REQUIRE(js.bytes == lua.bytes);
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


static const char* JS_ON_TRIGGER = R"(/**
 * @engine QuickJs
 */
rack.onTrigger = function(trigPort) {
    rack.log("onTrigger " + number.toString(trigPort));
    let msg = midi.create();
    midi.setCc(msg, 1, 10, trigPort);
    midiOut.send(msg);
};
)";

static const char* LUA_ON_TRIGGER = R"(--[[
@engine Lua
--]]
function rack.onTrigger(trigPort)
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

		m->activeEngine->processInTick(0);
		m->activeEngine->process();

		std::string log = drainLog(m);
		REQUIRE(log.find("onTrigger 1") != std::string::npos);

		int port, ticks;
		midi::Message out;
		REQUIRE(m->activeEngine->processOutMessage(port, out, ticks));
		auto sent = toSent(port, ticks, out);
		Test::destroyModule(m);
		return sent;
	};

	auto js = checkOnTrigger(JS_ON_TRIGGER);
	auto lua = checkOnTrigger(LUA_ON_TRIGGER);
	REQUIRE(js.port == lua.port);
	REQUIRE(js.bytes == lua.bytes);
}


TEST_CASE("Script without onTrigger silently ignores trigger ticks, in both engines", "[MidiKit][CrossEngine]") {
	auto checkNoOnTrigger = [](const std::string& script) {
		MidiKitModule* m = createModule();
		m->loadScript(script);
		drainLog(m);

		m->activeEngine->processInTick(0);
		m->activeEngine->process();

		std::string log = drainLog(m);
		int port, ticks;
		midi::Message out;
		bool sentAnything = m->activeEngine->processOutMessage(port, out, ticks);
		Test::destroyModule(m);
		return std::make_pair(log, sentAnything);
	};

	auto js = checkNoOnTrigger(JS_NO_ON_LOAD);
	auto lua = checkNoOnTrigger(LUA_NO_ON_LOAD);
	REQUIRE(js.second == false);
	REQUIRE(lua.second == false);
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
 * @engine QuickJs
 */
rack.onMidiMessage = function(port, msg) {
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
@engine Lua
--]]
rack.onMidiMessage = function(midiPort, msg)
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
	std::vector<ContextMenuSpec> specs;
	std::string loadLog;
	std::string log;
};

static MenuResult runMenu(const std::string& script, int clickId = -1, int clickValue = 0) {
	MidiKitModule* m = createModule();
	m->loadScript(script);

	MenuResult r;
	r.loaded = (m->activeEngine == &m->seQuickJs) ? (m->seQuickJs.ctx != nullptr)
	                                              : (m->seLua.L != nullptr);
	r.loadLog = drainLog(m);
	if (r.loaded) {
		m->activeEngine->getContextMenus(r.specs);
		if (clickId >= 0) {
			m->activeEngine->invokeContextMenuCallback(clickId, clickValue);
			r.log = drainLog(m);
			m->activeEngine->getContextMenus(r.specs);
		}
	}
	Test::destroyModule(m);
	return r;
}

// The two engines must expose identical menu models: same count, same order,
// and identical presentation fields. callbackId is engine-internal (both
// assign 1, 2, 3… in registration order), so only its validity is checked,
// not its exact value.
static void requireSameMenus(const std::vector<ContextMenuSpec>& a, const std::vector<ContextMenuSpec>& b) {
	REQUIRE(a.size() == b.size());
	for (size_t i = 0; i < a.size(); i++) {
		REQUIRE(a[i].type == b[i].type);
		REQUIRE(a[i].label == b[i].label);
		REQUIRE(a[i].checked == b[i].checked);
		REQUIRE(a[i].selected == b[i].selected);
		REQUIRE(a[i].options == b[i].options);
		REQUIRE(a[i].callbackId >= 1);
	}
}

static const char* JS_REGISTER_BOOL = R"(/**
 * @engine QuickJs
 */
rack.registerContextMenu({
	type: "boolean",
	label: "Velocity to CC",
	checked: false,
	onChange: function(checked) {
		rack.log("onChange: " + (checked ? "true" : "false"));
	}
});
)";

static const char* LUA_REGISTER_BOOL = R"(--[[
@engine Lua
--]]
rack.registerContextMenu({
	type = "boolean",
	label = "Velocity to CC",
	checked = false,
	onChange = function(checked)
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
	REQUIRE(js.specs[0].type == ContextMenuSpec::Type::Boolean);
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
 * @engine QuickJs
 */
rack.registerContextMenu({
	type: "options",
	label: "Out mode",
	options: ["Internal", "External", "Both"],
	selected: 1,
	onChange: function(selectedIndex, selectedLabel) {
		rack.log("onChange: " + selectedIndex + " " + selectedLabel);
	}
});
)";

static const char* LUA_REGISTER_OPTIONS = R"(--[[
@engine Lua
--]]
rack.registerContextMenu({
	type = "options",
	label = "Out mode",
	options = {"Internal", "External", "Both"},
	selected = 1,
	onChange = function(selectedIndex, selectedLabel)
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
	REQUIRE(js.specs[0].type == ContextMenuSpec::Type::Options);
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
 * @engine QuickJs
 */
rack.registerContextMenu({ type: "boolean", label: "First", checked: false, onChange: function() {} });
rack.registerContextMenu({ type: "options", label: "Second", options: ["x", "y"], selected: 0, onChange: function() {} });
)";

static const char* LUA_REGISTER_TWO = R"(--[[
@engine Lua
--]]
rack.registerContextMenu({ type = "boolean", label = "First", checked = false, onChange = function() end })
rack.registerContextMenu({ type = "options", label = "Second", options = {"x", "y"}, selected = 0, onChange = function() end })
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
	REQUIRE(js.specs[0].type == ContextMenuSpec::Type::Boolean);
	REQUIRE(js.specs[1].type == ContextMenuSpec::Type::Options);
	// callbackIds are assigned monotonically in registration order.
	REQUIRE(js.specs[0].callbackId < js.specs[1].callbackId);
	REQUIRE(lua.specs[0].callbackId < lua.specs[1].callbackId);
}

static const char* JS_REGISTER_THROW = R"(/**
 * @engine QuickJs
 */
rack.registerContextMenu({
	type: "boolean",
	label: "Bad",
	checked: false,
	onChange: function(checked) { throw new Error("boom"); }
});
rack.registerContextMenu({
	type: "boolean",
	label: "Good",
	checked: false,
	onChange: function(checked) { rack.log("good"); }
});
)";

static const char* LUA_REGISTER_THROW = R"(--[[
@engine Lua
--]]
rack.registerContextMenu({
	type = "boolean",
	label = "Bad",
	checked = false,
	onChange = function(checked) error("boom") end
})
rack.registerContextMenu({
	type = "boolean",
	label = "Good",
	checked = false,
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
 * @engine QuickJs
 */
rack.registerContextMenu({ type: "boolean", label: "X", checked: false });
)";

static const char* LUA_REGISTER_BAD_NO_ONCHANGE = R"(--[[
@engine Lua
--]]
rack.registerContextMenu({ type = "boolean", label = "X", checked = false })
)";

static const char* JS_REGISTER_BAD_TYPE = R"(/**
 * @engine QuickJs
 */
rack.registerContextMenu({ type: "nope", label: "X", onChange: function() {} });
)";

static const char* LUA_REGISTER_BAD_TYPE = R"(--[[
@engine Lua
--]]
rack.registerContextMenu({ type = "nope", label = "X", onChange = function() end })
)";

static const char* JS_REGISTER_BAD_OPTIONS = R"(/**
 * @engine QuickJs
 */
rack.registerContextMenu({ type: "options", label: "X", options: ["ok", 42], onChange: function() {} });
)";

static const char* LUA_REGISTER_BAD_OPTIONS = R"(--[[
@engine Lua
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
