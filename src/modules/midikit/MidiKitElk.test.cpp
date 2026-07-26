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

// Note: the Elk header parser uses ([^@]*) to capture tag values, which
// means each tag value is terminated by the next @.  A script where
// @engine is the ONLY tag would capture "Elk " (with trailing space) and
// fail the "Elk" equality check.  Adding a second tag (e.g. @description)
// ensures the capture stops cleanly at the next @.


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


static const char* ELK_RESCALE = R"(/**
 * @engine Elk
 * @description test
 */
let r = number.rescale(5, 0, 10, 0, 100);
)";

TEST_CASE("number.rescale API works from script body", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_RESCALE);
	REQUIRE(m->se.js != nullptr);

	jsval_t v = js_eval(m->se.js, "r;", ~0U);
	REQUIRE(js_type(v) == JS_NUM);
	REQUIRE(js_getnum(v) == Catch::Approx(50.0).margin(0.01));

	Test::destroyModule(m);
}


// Echo script: forwards every message verbatim.
// Used to verify that processMidi is called without needing a second js_eval
// readback (the Elk arena is too small for two consecutive js_eval calls after
// a full API registration + script load).
static const char* ELK_ECHO = R"(/**
 * @engine Elk
 * @description echo passthrough
 */
let processMidi = function(port, msg) {
    midiOut.send(msg);
};
)";

TEST_CASE("processMidi callback is invoked with incoming message", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_ECHO);
	REQUIRE(m->se.js != nullptr);

	midi::Message msg;
	msg.setSize(3);
	msg.setStatus(0x9);  // NoteOn
	msg.setChannel(0);
	msg.setNote(60);
	msg.setValue(100);

	m->se.processInMessage(0, msg);
	m->se.process();

	// Callback forwarded the message → output queue has exactly one message
	int outPort;
	midi::Message outMsg;
	int ticks;
	REQUIRE(m->se.processOutMessage(outPort, outMsg, ticks));
	REQUIRE(outMsg.getStatus() == 0x9);
	REQUIRE(outMsg.getNote() == 60);

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


// Increments the CC number of every incoming CC message by 1 and forwards it.
static const char* ELK_CC_REROUTE = R"(/**
 * @engine Elk
 * @description CC number +1 passthrough
 */
let processMidi = function(port, msg) {
    if (midi.isCc(msg)) {
        midi.setNote(msg, midi.getNote(msg) + 1);
        midiOut.send(msg);
    }
};
)";

TEST_CASE("Simple CC reroute script", "[MidiKit][Elk]") {
	MidiKitModule* m = createModule();

	m->loadScript(ELK_CC_REROUTE);
	REQUIRE(m->se.js != nullptr);

	midi::Message msg;
	msg.setSize(3);
	msg.setStatus(0xb);   // CC
	msg.setChannel(0);    // channel 1 (0-based internally)
	msg.setNote(10);      // CC number 10
	msg.setValue(64);     // CC value

	m->se.processInMessage(0, msg);
	m->se.process();

	int outPort;
	midi::Message outMsg;
	int ticks;
	REQUIRE(m->se.processOutMessage(outPort, outMsg, ticks));

	REQUIRE(outMsg.getStatus() == 0xb);  // still CC
	REQUIRE(outMsg.getChannel() == 0);   // same channel
	REQUIRE(outMsg.getNote() == 11);     // CC number incremented
	REQUIRE(outMsg.getValue() == 64);    // value unchanged

	Test::destroyModule(m);
}
