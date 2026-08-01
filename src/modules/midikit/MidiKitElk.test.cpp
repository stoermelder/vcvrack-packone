#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "MidiKit.cpp"

using namespace StoermelderPackOne::MidiKit;
using StoermelderPackOne::MidiScript::MidiScriptEngine;

SYNC_MODEL(modelMidiKit, "MidiKit");
Test::TestContext<> testContext;

// Bypass the dylib factory — create directly so the injected SyncTaskWorker
// is used instead of the module's default async TaskWorker.
static MidiKitModule* createModule() {
	MidiKitModule* m = new MidiKitModule(std::make_shared<StoermelderPackOne::SyncTaskWorker>());
	m->id = rand();
	Module::SampleRateChangeEvent e{44100.f, 1.f / 44100.f};
	m->onSampleRateChange(e);
	return m;
}

// Note: the Elk header parser uses ([^@]*) to capture tag values, so each
// value runs up to the next @ (or the end of the header) and picks up the
// separating whitespace.  loadScript() trims that trailing whitespace, so a
// script with @engine as its only tag loads too — see the ELK_ONLY_ENGINE
// test below.


static const char* ELK_EMPTY = R"(/**
 * @engine Elk
 * @description test
 */
)";

TEST_CASE("Elk-tagged script loads and creates JS context", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_EMPTY);

	REQUIRE(m->se.js != nullptr);
	REQUIRE(m->activeEngine == static_cast<MidiScriptEngine*>(&m->se));

	Test::destroyModule(m);
}


static const char* ELK_ONLY_ENGINE = R"(/**
 * @engine Elk
 */
)";

TEST_CASE("Elk script loads with @engine as the only header tag", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_ONLY_ENGINE);

	REQUIRE(m->se.js != nullptr);
	REQUIRE(m->activeEngine == static_cast<MidiScriptEngine*>(&m->se));

	Test::destroyModule(m);
}


static const char* ELK_MAX = R"(/**
 * @engine Elk
 * @description test
 */
let x = number.max(3, 7);
)";

TEST_CASE("Script body runs synchronously on load", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_MAX);
	REQUIRE(m->se.js != nullptr);

	// number.max(3, 7) evaluated at load time — JS global x should be 7
	jsval_t v = js_eval(m->se.js, "x;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(7.0));

	Test::destroyModule(m);
}


static const char* LUA_HEADER = R"(--[[
@engine Lua
--]]
)";

TEST_CASE("Lua-tagged script is rejected by Elk engine", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->se.loadScript(LUA_HEADER);

	REQUIRE(m->se.js == nullptr);

	Test::destroyModule(m);
}


static const char* ELK_BAD_SYNTAX = R"(/**
 * @engine Elk
 * @description test
 */
let ??? = ;
)";

TEST_CASE("JS syntax error is handled gracefully", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->se.loadScript(ELK_BAD_SYNTAX);

	REQUIRE(m->se.js == nullptr);

	Test::destroyModule(m);
}


static std::string drainLog(MidiKitModule* m) {
	std::string all;
	while (!m->midiLogMessages.empty()) {
		auto t = m->midiLogMessages.shift();
		all += std::get<2>(t) + "\n";
	}
	return all;
}


// Parse-error reporting with a source position
//
// Elk raises errors as a bare string with no position, so a failed load used to
// log just "ERROR: parse error". js_mkerr() now records the offset of the token
// it failed on, and the engine turns that into a line, a column and the source
// line itself.

// The `function f() {}` declaration form is a parse error in Elk — it is a JS
// subset that only accepts `let f = function() {}`. This is the single most
// likely mistake a script author makes, and the one that most needs a line.
static const char* ELK_BAD_ON_LINE_7 = R"(/**
 * @engine Elk
 * @description test
 */
let a = 1;
let b = 2;
function broken(x) { return x; }
let c = 3;
)";

TEST_CASE("Parse error reports the line it failed on", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_BAD_ON_LINE_7);
	// A failed load tears the state down
	REQUIRE(m->se.js == nullptr);

	std::string log = drainLog(m);
	// `function` is on line 7, at column 10 counting from the `broken` token
	REQUIRE(log.find("line 7:") != std::string::npos);
	// The offending source line is echoed so the author needn't count lines
	REQUIRE(log.find("function broken(x) { return x; }") != std::string::npos);
	// The underlying elk message is still present
	REQUIRE(log.find("parse error") != std::string::npos);

	Test::destroyModule(m);
}


// Same defect one line earlier — pins that the number tracks the error rather
// than being a constant that happens to match.
static const char* ELK_BAD_ON_LINE_6 = R"(/**
 * @engine Elk
 * @description test
 */
let a = 1;
function broken(x) { return x; }
let b = 2;
)";

TEST_CASE("Parse error line number tracks the error position", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_BAD_ON_LINE_6);
	REQUIRE(m->se.js == nullptr);

	std::string log = drainLog(m);
	REQUIRE(log.find("line 6:") != std::string::npos);
	REQUIRE(log.find("line 7:") == std::string::npos);

	Test::destroyModule(m);
}


// A script that loads cleanly must not emit any position noise.
TEST_CASE("Successful load reports no error position", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_MAX);
	REQUIRE(m->se.js != nullptr);

	std::string log = drainLog(m);
	REQUIRE(log.find("line ") == std::string::npos);
	REQUIRE(log.find("Script loaded") != std::string::npos);

	Test::destroyModule(m);
}


// Boolean === / !== between two booleans works (compared as 0/1), so a
// boolean flag can be tested directly with `flag === true` / `flag !== false`.
// Previously this raised "type mismatch" at runtime — or a bare "parse error"
// when the comparison sat inside an if condition. Mixed boolean/number and
// boolean/string comparisons are still type errors.
static const char* ELK_BOOL_EQ = R"(/**
 * @engine Elk
 * @description test
 */
let eqSame = true === true;
let neDiff = true !== false;
let flag = true;
let ifCmp = 0;
if (flag === true) ifCmp = 1;
)";

TEST_CASE("Boolean ===/!== comparison works in scripts", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_BOOL_EQ);
	REQUIRE(m->se.js != nullptr);

	jsval_t v;

	v = js_eval(m->se.js, "eqSame;", ~0U);
	REQUIRE(js_type(v) == JS_TRUE);

	v = js_eval(m->se.js, "neDiff;", ~0U);
	REQUIRE(js_type(v) == JS_TRUE);

	// The previously-broken case: a boolean comparison inside an if condition
	v = js_eval(m->se.js, "ifCmp;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(1.0));

	// Mixed boolean/number and boolean/string comparisons are still type errors
	v = js_eval(m->se.js, "true === 'true';", ~0U);
	REQUIRE(js_type(v) == JS_ERR);

	Test::destroyModule(m);
}


static const char* ELK_ON_UNLOAD = R"(/**
 * @engine Elk
 * @description test
 */
onMidiMessage = function(midiPort, msg) {};
onUnload = function() {
	rack.log("onUnload ran");
	let msg = midi.create();
	midi.setNoteOff(msg, 1, 60);
	midiOut.send(msg);
};
)";


TEST_CASE("onUnload runs on module destruction without crashing", "[MidiKit][Elk]") {
	// Regression guard: writeLog/writeOverlay/input.*/trig.*/param.* are pure
	// virtual in MidiScriptEngineElk, overridden only by the derived class in
	// MidiKit.cpp. Running onUnload from ~MidiScriptEngineElk() itself would
	// call through a vtable that no longer has those overrides — undefined
	// behaviour that crashes as "pure virtual function called". MidiKitModule
	// has its own destructor that calls closeState() first, while still fully
	// alive, specifically to avoid that. This test does not (and cannot)
	// assert a log/message result — it can only prove destroyModule() doesn't
	// crash, which is what it's for.
	MidiKitModule* m = createModule();
	m->loadScript(ELK_ON_UNLOAD);
	REQUIRE(m->se.js != nullptr);

	Test::destroyModule(m);
}


// ─── Memory / garbage collection ────────────────────────────────────────────

// RAM-usage tests for the Elk engine. Each onMidiMessage callback allocates
// scratch garbage (strings, objects, arrays) that nothing retains, so the fixed
// 64KB arena must stay bounded across a large number of callbacks. Under real
// use the engine's own automatic GC is what keeps it bounded, and the no-growth
// test below deliberately does NOT collect or touch the GC threshold — it only
// runs callbacks and checks the arena did not march toward exhaustion. A leak —
// a script that keeps references to per-callback allocations — would grow brk
// monotonically (reachable entities are never collected) until the arena fills
// and allocations start failing with "oom".
//
// The retain test below is a sensitivity control for the no-growth test: it
// proves the measurement would actually see a leak. It sets the GC threshold to
// zero (js_setgct) so Elk's own automatic GC runs at every top-level statement
// boundary, which lets it read the retained live set instead of the transient
// garbage stacked on top of it. That is deliberate isolation of retention, not
// a substitute for the automatic-GC behaviour pinned by the no-growth test.

static const char* ELK_GC_SCRATCH = R"(/**
 * @engine Elk
 * @description test
 */
onMidiMessage = function(midiPort, msg) {
	let n = number.toString(midi.getNote(msg));
	let s = n + "_" + n;
	let o = { a: 1, b: "b", c: s };
	let arr = [s, o, "tail"];
	number.toString(arr.length);
};
)";

TEST_CASE("Elk garbage-generating callbacks do not grow RAM usage", "[MidiKit][Elk][GC]") {
	MidiKitModule* m = createModule();
	m->loadScript(ELK_GC_SCRATCH);
	REQUIRE(m->se.js != nullptr);

	const int warmup = 200;
	const int run = 5000;

	midi::Message msg;
	msg.setSize(3);
	msg.setStatus(0x9);
	msg.setChannel(1);
	msg.setNote(60);
	msg.setValue(100);

	for (int i = 0; i < warmup; i++) {
		m->se.processInMessage(0, msg);
		m->se.process();
	}

	size_t used0, total;
	REQUIRE(m->se.getMemoryUsage(used0, total));
	// The arena is fixed, so the total budget is known up front.
	REQUIRE(total == sizeof(m->se.jsMem));

	for (int i = 0; i < run; i++) {
		m->se.processInMessage(0, msg);
		m->se.process();
	}

	size_t used1, total1;
	REQUIRE(m->se.getMemoryUsage(used1, total1));

	// The callbacks must have actually run (no load/callback errors), so the
	// allocations really happened rather than the test passing vacuously.
	std::string log = drainLog(m);
	REQUIRE(log.find("rror") == std::string::npos);

	// Only the engine's automatic GC is in play here — nothing in this test
	// collects or changes the threshold. Elk collects at top-level statement
	// boundaries once brk crosses half the arena, so a non-retaining script's
	// usage oscillates well below the limit. A leak would march brk toward the
	// full 64KB arena (and start erroring with "oom"), so after 5000 callbacks
	// usage must still be comfortably inside the arena and no more than half
	// the arena above the warm-up level.
	REQUIRE(used1 < total1);
	REQUIRE(used1 <= used0 + total1 / 2);

	Test::destroyModule(m);
}


// Sensitivity control for the test above: a script that DOES retain its
// per-callback allocation (grows a global array) must show up as clear
// growth. Without this the no-growth test could pass vacuously if js_usage
// stopped reflecting allocations at all. The GC threshold is lowered to zero so
// the retained set can be read cleanly — deliberate isolation of retention,
// separate from the automatic-GC behaviour the no-growth test pins.
static const char* ELK_GC_RETAIN = R"(/**
 * @engine Elk
 * @description test
 */
let leaked = [];
onMidiMessage = function(midiPort, msg) {
	leaked[leaked.length] = number.toString(midi.getNote(msg)) + "_";
};
)";

TEST_CASE("Elk retaining callbacks do grow RAM usage", "[MidiKit][Elk][GC]") {
	MidiKitModule* m = createModule();
	m->loadScript(ELK_GC_RETAIN);
	REQUIRE(m->se.js != nullptr);
	js_setgct(m->se.js, 0);

	const int warmup = 20;
	const int run = 200;

	midi::Message msg;
	msg.setSize(3);
	msg.setStatus(0x9);
	msg.setChannel(1);
	msg.setNote(60);
	msg.setValue(100);

	for (int i = 0; i < warmup; i++) {
		m->se.processInMessage(0, msg);
		m->se.process();
	}

	js_eval(m->se.js, "1;", ~0U);
	size_t used0, total;
	REQUIRE(m->se.getMemoryUsage(used0, total));

	for (int i = 0; i < run; i++) {
		m->se.processInMessage(0, msg);
		m->se.process();
	}

	js_eval(m->se.js, "1;", ~0U);
	size_t used1, total1;
	REQUIRE(m->se.getMemoryUsage(used1, total1));

	std::string log = drainLog(m);
	REQUIRE(log.find("rror") == std::string::npos);

	// 200 retained strings + their array slots must be clearly visible, while
	// still staying well under the 64KB arena.
	REQUIRE(used1 > used0 + 2048);

	Test::destroyModule(m);
}
