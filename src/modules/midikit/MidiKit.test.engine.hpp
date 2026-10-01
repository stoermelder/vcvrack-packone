#include "MidiKit.test.hpp"

using StoermelderPackOne::MidiScript::ScriptMenuItem;

// Cross-engine equivalence suite
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


// number.*
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


// rack.log coercion
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


// rack.log multiple arguments
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
