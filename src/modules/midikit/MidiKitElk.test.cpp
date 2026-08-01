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


static const char* ELK_ON_UNLOAD = R"(/**
 * @engine Elk
 * @description test
 */
onMidiMessage = function(midiPort, msg) {};
onUnload = function() {
	log("onUnload ran");
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
