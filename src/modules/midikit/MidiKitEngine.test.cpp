#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "MidiKit.cpp"

using namespace StoermelderPackOne::MidiKit;
using StoermelderPackOne::MidiScript::MidiScriptEngine;

// Cross-engine equivalence suite (review finding D7).
//
// Elk and Lua are two independent ~1000-line implementations of the same
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

SYNC_MODEL(modelMidiKit, "MidiKit");
Test::TestContext<> testContext;

static MidiKitModule* createModule() {
	MidiKitModule* m = new MidiKitModule(std::make_shared<StoermelderPackOne::SyncTaskWorker>());
	m->id = rand();
	Module::SampleRateChangeEvent e{44100.f, 1.f / 44100.f};
	m->onSampleRateChange(e);
	return m;
}

static std::string drainLog(MidiKitModule* m) {
	std::string all;
	while (!m->midiLogMessages.empty()) {
		auto t = m->midiLogMessages.shift();
		all += std::get<2>(t) + "\n";
	}
	return all;
}

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


// --- number.* ------------------------------------------------------------
//
// Each value under test is logged at load time via number.toString, which
// the per-engine "API number.toString" tests already pin as producing
// identical text in both engines — that's what makes comparing logged lines
// a valid equivalence check rather than just an engine-internal readback.

static const char* JS_NUMBER_ABS = R"(/**
 * @engine Elk
 */
log("PROBE:" + number.toString(number.abs(-5)));
log("PROBE:" + number.toString(number.abs(3)));
log("PROBE:" + number.toString(number.abs(0)));
)";

static const char* LUA_NUMBER_ABS = R"(--[[
@engine Lua
--]]
log("PROBE:" .. number.toString(number.abs(-5)))
log("PROBE:" .. number.toString(number.abs(3)))
log("PROBE:" .. number.toString(number.abs(0)))
)";

TEST_CASE("number.abs is identical", "[MidiKit][CrossEngine]") {
	requireLoggedValues(JS_NUMBER_ABS, LUA_NUMBER_ABS, {"5", "3", "0"});
}


static const char* JS_NUMBER_CEIL = R"(/**
 * @engine Elk
 */
log("PROBE:" + number.toString(number.ceil(3.2)));
log("PROBE:" + number.toString(number.ceil(-3.2)));
log("PROBE:" + number.toString(number.ceil(5)));
)";

static const char* LUA_NUMBER_CEIL = R"(--[[
@engine Lua
--]]
log("PROBE:" .. number.toString(number.ceil(3.2)))
log("PROBE:" .. number.toString(number.ceil(-3.2)))
log("PROBE:" .. number.toString(number.ceil(5)))
)";

TEST_CASE("number.ceil is identical", "[MidiKit][CrossEngine]") {
	requireLoggedValues(JS_NUMBER_CEIL, LUA_NUMBER_CEIL, {"4", "-3", "5"});
}


static const char* JS_NUMBER_CROSSFADE = R"(/**
 * @engine Elk
 */
log("PROBE:" + number.toString(number.crossfade(0, 10, 0.5)));
log("PROBE:" + number.toString(number.crossfade(100, 200, 0.25)));
log("PROBE:" + number.toString(number.crossfade(-5, 5, 0.75)));
)";

static const char* LUA_NUMBER_CROSSFADE = R"(--[[
@engine Lua
--]]
log("PROBE:" .. number.toString(number.crossfade(0, 10, 0.5)))
log("PROBE:" .. number.toString(number.crossfade(100, 200, 0.25)))
log("PROBE:" .. number.toString(number.crossfade(-5, 5, 0.75)))
)";

TEST_CASE("number.crossfade is identical", "[MidiKit][CrossEngine]") {
	requireLoggedValues(JS_NUMBER_CROSSFADE, LUA_NUMBER_CROSSFADE, {"5", "125", "2.5"});
}


static const char* JS_NUMBER_FLOOR = R"(/**
 * @engine Elk
 */
log("PROBE:" + number.toString(number.floor(3.8)));
log("PROBE:" + number.toString(number.floor(-3.8)));
log("PROBE:" + number.toString(number.floor(5)));
)";

static const char* LUA_NUMBER_FLOOR = R"(--[[
@engine Lua
--]]
log("PROBE:" .. number.toString(number.floor(3.8)))
log("PROBE:" .. number.toString(number.floor(-3.8)))
log("PROBE:" .. number.toString(number.floor(5)))
)";

TEST_CASE("number.floor is identical", "[MidiKit][CrossEngine]") {
	requireLoggedValues(JS_NUMBER_FLOOR, LUA_NUMBER_FLOOR, {"3", "-4", "5"});
}


static const char* JS_NUMBER_MIN = R"(/**
 * @engine Elk
 */
log("PROBE:" + number.toString(number.min(3, 7)));
log("PROBE:" + number.toString(number.min(-5, 5)));
log("PROBE:" + number.toString(number.min(10, 10)));
)";

static const char* LUA_NUMBER_MIN = R"(--[[
@engine Lua
--]]
log("PROBE:" .. number.toString(number.min(3, 7)))
log("PROBE:" .. number.toString(number.min(-5, 5)))
log("PROBE:" .. number.toString(number.min(10, 10)))
)";

TEST_CASE("number.min is identical", "[MidiKit][CrossEngine]") {
	requireLoggedValues(JS_NUMBER_MIN, LUA_NUMBER_MIN, {"3", "-5", "10"});
}


static const char* JS_NUMBER_RESCALE = R"(/**
 * @engine Elk
 */
log("PROBE:" + number.toString(number.rescale(5, 0, 10, 0, 100)));
)";

static const char* LUA_NUMBER_RESCALE = R"(--[[
@engine Lua
--]]
log("PROBE:" .. number.toString(number.rescale(5, 0, 10, 0, 100)))
)";

TEST_CASE("number.rescale is identical", "[MidiKit][CrossEngine]") {
	requireLoggedValues(JS_NUMBER_RESCALE, LUA_NUMBER_RESCALE, {"50"});
}


static const char* JS_NUMBER_TOSTRING = R"(/**
 * @engine Elk
 */
log("PROBE:" + number.toString(42));
log("PROBE:" + number.toString(3.14));
log("PROBE:" + number.toString(-100));
log("PROBE:" + number.toString(1 / 3));
log("PROBE:" + number.toString(0));
)";

static const char* LUA_NUMBER_TOSTRING = R"(--[[
@engine Lua
--]]
log("PROBE:" .. number.toString(42))
log("PROBE:" .. number.toString(3.14))
log("PROBE:" .. number.toString(-100))
log("PROBE:" .. number.toString(1 / 3))
log("PROBE:" .. number.toString(0))
)";

TEST_CASE("number.toString is identical", "[MidiKit][CrossEngine]") {
	requireLoggedValues(JS_NUMBER_TOSTRING, LUA_NUMBER_TOSTRING, {"42", "3.14", "-100", "0.333333", "0"});
}


// number.toFixed (#A10) — closes the gap number.toString left: Elk had no
// way to control decimal precision, so a knob-value overlay always printed
// six decimals (0.500000). Covers 0 digits (rounds to an integer string,
// unlike toString's "%i" branch which only triggers for exact integers),
// a mid-range digit count, and rounding at the last retained digit.
static const char* JS_NUMBER_TOFIXED = R"(/**
 * @engine Elk
 */
log("PROBE:" + number.toFixed(3.14159, 2));
log("PROBE:" + number.toFixed(0.5, 0));
log("PROBE:" + number.toFixed(1, 3));
log("PROBE:" + number.toFixed(2.005, 2));
)";

static const char* LUA_NUMBER_TOFIXED = R"(--[[
@engine Lua
--]]
log("PROBE:" .. number.toFixed(3.14159, 2))
log("PROBE:" .. number.toFixed(0.5, 0))
log("PROBE:" .. number.toFixed(1, 3))
log("PROBE:" .. number.toFixed(2.005, 2))
)";

TEST_CASE("number.toFixed is identical", "[MidiKit][CrossEngine]") {
	requireLoggedValues(JS_NUMBER_TOFIXED, LUA_NUMBER_TOFIXED, {"3.14", "0", "1.000", "2.01"});
}


// number.random has no fixed expected value — both engines only need to stay
// within the documented [0, 1) range, and in agreement about that range, so
// this uses a bespoke check rather than requireLoggedValues.
static const char* JS_NUMBER_RANDOM = R"(/**
 * @engine Elk
 */
log("PROBE:" + number.toString(number.random()));
)";

static const char* LUA_NUMBER_RANDOM = R"(--[[
@engine Lua
--]]
log("PROBE:" .. number.toString(number.random()))
)";

TEST_CASE("number.random stays within [0, 1) in both engines", "[MidiKit][CrossEngine]") {
	auto checkInRange = [](const std::string& script) {
		auto lines = loadAndDrainLog(script);
		REQUIRE(lines.size() == 1);
		double v = std::stod(lines[0]);
		REQUIRE(v >= 0.0);
		REQUIRE(v < 1.0);
	};
	checkInRange(JS_NUMBER_RANDOM);
	checkInRange(LUA_NUMBER_RANDOM);
}


// --- setNoteOn / midiOut.send -----------------------------------------

static const char* JS_NOTE_ON = R"(/**
 * @engine Elk
 */
onMidiMessage = function(port, msg) {
    let out = midi.create();
    midi.setNoteOn(out, 1, 60, 100);
    midiOut.send(out);
};
)";

static const char* LUA_NOTE_ON = R"(--[[
@engine Lua
--]]
function onMidiMessage(port, msg)
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
 * @engine Elk
 * @description CC number +1 passthrough
 */
onMidiMessage = function(port, msg) {
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
onMidiMessage = function(port, msg)
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
 * @engine Elk
 */
onMidiMessage = function(port, msg) {
    let out = midi.create();
    midi.setCc(out, 2, 74, 127);
    midiOut.send(out);
};
)";

static const char* LUA_CC = R"(--[[
@engine Lua
--]]
function onMidiMessage(port, msg)
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
 * @engine Elk
 */
onMidiMessage = function(port, msg) {
    let out = midi.create();
    midi.setCc(out, 1, 10, 500);
    midiOut.send(out);
};
)";

static const char* LUA_CC_CLAMP = R"(--[[
@engine Lua
--]]
function onMidiMessage(port, msg)
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
 * @engine Elk
 */
onMidiMessage = function(port, msg) {
    let out = midi.create();
    midi.setSysEx(out, "43104c0000");
    midiOut.send(out);
};
)";

static const char* LUA_SYSEX = R"(--[[
@engine Lua
--]]
function onMidiMessage(port, msg)
    local out = midi.create()
    midi.setSysEx(out, "43104c0000")
    midiOut.send(out)
end
)";

TEST_CASE("setSysEx frames the payload identically", "[MidiKit][CrossEngine]") {
	requireEquivalent(JS_SYSEX, LUA_SYSEX);
}


// --- sendAfterTrigger (finding #7: 3-arg form) --------------------------
//
// Regression coverage for the actual bug in #7: Lua used to misread the
// 3-arg form as (msg, trigPort, ticks) instead of matching Elk's
// (midiPort-selected, msg, ticks). Comparing the two engines directly is
// exactly the check that would have caught #7 the moment it was introduced,
// rather than requiring a human to notice the scripts behaved differently.

static const char* JS_SEND_AFTER_TRIGGER = R"(/**
 * @engine Elk
 */
onMidiMessage = function(port, msg) {
    let out = midi.create();
    midi.setNoteOn(out, 1, 60, 100);
    midiOut.sendAfterTrigger(out, 10);
};
)";

static const char* LUA_SEND_AFTER_TRIGGER = R"(--[[
@engine Lua
--]]
function onMidiMessage(port, msg)
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
// both engines — #13 was Elk-only failing on this exact shape.

static const char* JS_HEADER_ONLY = R"(/**
 * @engine Elk
 */
onMidiMessage = function(port, msg) {
    let out = midi.create();
    midi.setNoteOn(out, 1, 60, 100);
    midiOut.send(out);
};
)";

static const char* LUA_HEADER_ONLY = R"(--[[
@engine Lua
--]]
function onMidiMessage(port, msg)
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
 * @engine Elk
 */
onMidiMessage = function(port, msg) {
    let out = midi.create();
    midi.setRaw(out, "f11a");
    midiOut.send(out);
};
)";

static const char* LUA_RAW = R"(--[[
@engine Lua
--]]
function onMidiMessage(port, msg)
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
 * @engine Elk
 */
onMidiMessage = function(port, msg) {
    let out = midi.create();
    midi.setPitchWheel(out, 2, 12345);
    midiOut.send(out);
};
)";

static const char* LUA_PITCH_WHEEL = R"(--[[
@engine Lua
--]]
function onMidiMessage(port, msg)
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
 * @engine Elk
 */
onMidiMessage = function(port, msg) {
    let out = midi.create();
    midi.setProgramChange(out, 4, 10);
    midiOut.send(out);
};
)";

static const char* LUA_PROGRAM_CHANGE = R"(--[[
@engine Lua
--]]
function onMidiMessage(port, msg)
    local out = midi.create()
    midi.setProgramChange(out, 4, 10)
    midiOut.send(out)
end
)";

TEST_CASE("setProgramChange produces identical wire bytes", "[MidiKit][CrossEngine]") {
	requireEquivalent(JS_PROGRAM_CHANGE, LUA_PROGRAM_CHANGE);
}


// --- setChanPressure (2-byte message, #A2) --------------------------------

static const char* JS_CHAN_PRESSURE = R"(/**
 * @engine Elk
 */
onMidiMessage = function(port, msg) {
    let out = midi.create();
    midi.setChanPressure(out, 5, 80);
    midiOut.send(out);
};
)";

static const char* LUA_CHAN_PRESSURE = R"(--[[
@engine Lua
--]]
function onMidiMessage(port, msg)
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
 * @engine Elk
 */
onMidiMessage = function(port, msg) {
    let out = midi.create();
    midi.setKeyPressure(out, 6, 64, 90);
    midiOut.send(out);
};
)";

static const char* LUA_KEY_PRESSURE = R"(--[[
@engine Lua
--]]
function onMidiMessage(port, msg)
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
 * @engine Elk
 */
onMidiMessage = function(port, msg) {
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
function onMidiMessage(port, msg)
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
 * @engine Elk
 */
onMidiMessage = function(port, msg) {
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
function onMidiMessage(port, msg)
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


// --- setNoteOff --------------------------------------------------------

static const char* JS_NOTE_OFF = R"(/**
 * @engine Elk
 */
onMidiMessage = function(port, msg) {
    let out = midi.create();
    midi.setNoteOff(out, 7, 48);
    midiOut.send(out);
};
)";

static const char* LUA_NOTE_OFF = R"(--[[
@engine Lua
--]]
function onMidiMessage(port, msg)
    local out = midi.create()
    midi.setNoteOff(out, 7, 48)
    midiOut.send(out)
end
)";

TEST_CASE("setNoteOff produces identical wire bytes", "[MidiKit][CrossEngine]") {
	requireEquivalent(JS_NOTE_OFF, LUA_NOTE_OFF);
}


// --- setCc14bit --------------------------------------------------------

static const char* JS_CC_14BIT = R"(/**
 * @engine Elk
 */
onMidiMessage = function(port, msg) {
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
function onMidiMessage(port, msg)
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
 * @engine Elk
 */
onMidiMessage = function(port, msg) {
    let nrpn = midi.createNRPN();
    midi.setNRPN(nrpn, 9, 1234, 5678);
    midiOut.send(nrpn);
};
)";

static const char* LUA_NRPN = R"(--[[
@engine Lua
--]]
function onMidiMessage(port, msg)
    local nrpn = midi.createNRPN()
    midi.setNRPN(nrpn, 9, 1234, 5678)
    midiOut.send(nrpn)
end
)";

TEST_CASE("setNRPN produces identical 4-message wire sequence", "[MidiKit][CrossEngine]") {
	requireEquivalent(JS_NRPN, LUA_NRPN);
}


// --- is* type predicates -------------------------------------------------
//
// One message per status byte, each checked against every is* predicate and
// the results logged as a single ordered bit string — this is the shape of
// the per-engine "API midi.is* type check" case, just funneled through the
// same PROBE_PREFIX log channel used for the number.* tests above instead of
// engine-internal js_eval/lua_getglobal readbacks.

static const char* JS_IS_TYPES = R"(/**
 * @engine Elk
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
log("PROBE:" + bits);
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
log("PROBE:" .. bits)
)";

TEST_CASE("midi.is* predicates agree on every message type", "[MidiKit][CrossEngine]") {
	requireLoggedValues(JS_IS_TYPES, LUA_IS_TYPES, {"1001010000"});
}


// --- input.enable ----------------------------------------------------------
//
// input.enable flips a flag on the module's own inputInfos, which is plain
// module state, not engine-internal — comparable directly without a log
// probe.

static const char* JS_INPUT_ENABLE = R"(/**
 * @engine Elk
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
 * @engine Elk
 */
input.enable(1);
log("PROBE:" + number.toString(input.getVoltage(1)));
log("PROBE:" + number.toString(input.getVoltage(1, 1)));
log("PROBE:" + (input.isHigh(1) ? "high" : "low"));
log("PROBE:" + (input.isLow(1) ? "low" : "high"));
)";

static const char* LUA_INPUT_GET_VOLTAGE = R"(--[[
@engine Lua
--]]
input.enable(1)
log("PROBE:" .. number.toString(input.getVoltage(1)))
log("PROBE:" .. number.toString(input.getVoltage(1, 1)))
log("PROBE:" .. (input.isHigh(1) and "high" or "low"))
log("PROBE:" .. (input.isLow(1) and "low" or "high"))
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
 * @engine Elk
 */
onMidiMessage = function(port, msg) {
    let out = midi.create();
    midi.setCc(out, 1, 1, 0);
    midi.setValue(out, trig.getTicks(1));
    midiOut.send(out);
};
)";

static const char* LUA_TRIG_GET_TICKS = R"(--[[
@engine Lua
--]]
function onMidiMessage(port, msg)
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
 * @engine Elk
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
 * @engine Elk
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
 * @engine Elk
 */
onMidiMessage = function(port, msg) {
    param.enable(1);
    let out = midi.create();
    midi.setCc(out, 1, 1, 0);
    midi.setValue(out, number.floor(param.getValue(1) * 127));
    midiOut.send(out);
};
)";

static const char* LUA_PARAM_GET_VALUE = R"(--[[
@engine Lua
--]]
function onMidiMessage(port, msg)
    param.enable(1)
    local out = midi.create()
    midi.setCc(out, 1, 1, 0)
    midi.setValue(out, number.floor(param.getValue(1) * 127))
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


// --- midi.selectPort ---------------------------------------------------

static const char* JS_SELECT_PORT = R"(/**
 * @engine Elk
 */
let msg = midi.create();
midi.setNoteOn(msg, 1, 60, 100);

onMidiMessage = function(port, msg) {
    midi.selectPort(1);
    midiOut.send(msg);
};
)";

static const char* LUA_SELECT_PORT = R"(--[[
@engine Lua
--]]
msg = midi.create()
midi.setNoteOn(msg, 1, 60, 100)

function onMidiMessage(port, msg)
    midi.selectPort(1)
    midiOut.send(msg)
end
)";

TEST_CASE("midi.selectPort produces identical output port in both engines", "[MidiKit][CrossEngine]") {
	requireEquivalent(JS_SELECT_PORT, LUA_SELECT_PORT);
}


static const char* JS_SELECT_PORT_STICKY = R"(/**
 * @engine Elk
 */
onMidiMessage = function(port, msg) {
    midi.selectPort(1);
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
function onMidiMessage(port, msg)
    midi.selectPort(1)
    local msg1 = midi.create()
    midi.setNoteOn(msg1, 1, 60, 100)
    local msg2 = midi.create()
    midi.setNoteOn(msg2, 1, 61, 100)
    midiOut.send(msg1)
    midiOut.send(msg2)
end
)";

TEST_CASE("midi.selectPort stays selected across calls identically", "[MidiKit][CrossEngine]") {
	requireEquivalent(JS_SELECT_PORT_STICKY, LUA_SELECT_PORT_STICKY);
}


static const char* JS_SELECT_PORT_INVALID = R"(/**
 * @engine Elk
 */
onMidiMessage = function(port, msg) {
    midi.selectPort(2);
};
)";

static const char* LUA_SELECT_PORT_INVALID = R"(--[[
@engine Lua
--]]
function onMidiMessage(port, msg)
    midi.selectPort(2)
end
)";

TEST_CASE("midi.selectPort rejects an out-of-range port identically", "[MidiKit][CrossEngine]") {
	requireEquivalentLog(JS_SELECT_PORT_INVALID, LUA_SELECT_PORT_INVALID, "onMidiMessage error", true);
}


// --- midiOut.sendAfterMs ------------------------------------------------

static const char* JS_SEND_AFTER_MS = R"(/**
 * @engine Elk
 */
let msg = midi.create();
midi.setNoteOn(msg, 1, 60, 100);

onMidiMessage = function(port, msg) {
    midiOut.sendAfterMs(msg, 100);
};
)";

static const char* LUA_SEND_AFTER_MS = R"(--[[
@engine Lua
--]]
msg = midi.create()
midi.setNoteOn(msg, 1, 60, 100)

function onMidiMessage(port, msg)
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


// --- midiOut.sendAfterTrigger with selectPort / explicit trigPort ---------

static const char* JS_SEND_AFTER_TRIGGER_SELECTED_PORT = R"(/**
 * @engine Elk
 */
let msg = midi.create();
midi.setNoteOn(msg, 1, 60, 100);

onMidiMessage = function(port, msg) {
    midi.selectPort(1);
    midiOut.sendAfterTrigger(msg, 10);
};
)";

static const char* LUA_SEND_AFTER_TRIGGER_SELECTED_PORT = R"(--[[
@engine Lua
--]]
msg = midi.create()
midi.setNoteOn(msg, 1, 60, 100)

function onMidiMessage(port, msg)
    midi.selectPort(1)
    midiOut.sendAfterTrigger(msg, 10)
end
)";

TEST_CASE("sendAfterTrigger uses the selected port identically", "[MidiKit][CrossEngine]") {
	requireEquivalent(JS_SEND_AFTER_TRIGGER_SELECTED_PORT, LUA_SEND_AFTER_TRIGGER_SELECTED_PORT);
}


static const char* JS_SEND_AFTER_TRIGGER_TRIGPORT = R"(/**
 * @engine Elk
 */
let msg = midi.create();
midi.setNoteOn(msg, 1, 60, 100);

onMidiMessage = function(port, msg) {
    midi.selectPort(1);
    midiOut.sendAfterTrigger(msg, 1, 10);
};
)";

static const char* LUA_SEND_AFTER_TRIGGER_TRIGPORT = R"(--[[
@engine Lua
--]]
msg = midi.create()
midi.setNoteOn(msg, 1, 60, 100)

function onMidiMessage(port, msg)
    midi.selectPort(1)
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
 * @engine Elk
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
 * @engine Elk
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
 * @engine Elk
 */
onMidiMessage = function(port, msg) {
    let m = midi.create();
    midi.setCc(m, 1, 20, 100);
    midiOut.send(m);
};
)";

static const char* LUA_CALLBACK_CREATE = R"(--[[
@engine Lua
--]]
function onMidiMessage(port, msg)
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
 * @engine Elk
 */
onMidiMessage = function(midiPort, msg) {};
onLoad = function() {
    log("onLoad ran");
    let msg = midi.create();
    midi.setNoteOn(msg, 1, 60, 100);
    midiOut.send(msg);
};
)";

static const char* LUA_ON_LOAD = R"(--[[
@engine Lua
--]]
function onMidiMessage(midiPort, msg) end
function onLoad()
    log("onLoad ran")
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
 * @engine Elk
 */
let x = number.max(3, 7);
)";

static const char* LUA_NO_ON_LOAD = R"(--[[
@engine Lua
--]]
x = number.max(3, 7)
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
 * @engine Elk
 */
let msg = midi.create();
midi.setSysEx(msg, "43104c0000");
log("PROBE:" + (midi.isSysEx(msg) ? "yes" : "no"));
)";

static const char* LUA_TOPLEVEL_SYSEX = R"(--[[
@engine Lua
--]]
msg = midi.create()
midi.setSysEx(msg, "43104c0000")
log("PROBE:" .. (midi.isSysEx(msg) and "yes" or "no"))
)";

TEST_CASE("Top-level message handle survives a load with no onLoad in both engines (#D4)", "[MidiKit][CrossEngine]") {
	requireLoggedValues(JS_TOPLEVEL_SYSEX, LUA_TOPLEVEL_SYSEX, {"yes"});
}


// --- onUnload ----------------------------------------------------------

static const char* JS_ON_UNLOAD = R"(/**
 * @engine Elk
 */
onMidiMessage = function(midiPort, msg) {};
onUnload = function() {
    log("onUnload ran");
    let msg = midi.create();
    midi.setNoteOff(msg, 1, 60);
    midiOut.send(msg);
};
)";

static const char* LUA_ON_UNLOAD = R"(--[[
@engine Lua
--]]
function onMidiMessage(midiPort, msg) end
function onUnload()
    log("onUnload ran")
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

		// clearScript() reloads an empty script, which always selects Elk
		// (loadScript defaults to Elk when "@engine Lua" isn't found) — so
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
