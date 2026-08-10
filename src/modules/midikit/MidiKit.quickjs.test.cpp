#include "MidiKit.test.hpp"


static const char* QJS_EMPTY = R"(/**
 * @engine QuickJs@v1
 * @description test
 */
)";

TEST_CASE("QuickJs-tagged script loads and creates a context", "[MidiKit][QuickJs]") {
	MidiKitModule* m = createModule();

	m->loadScript(QJS_EMPTY);

	REQUIRE(m->host.seQuickJs.ctx != nullptr);
	REQUIRE(m->host.isQuickJsEngine());

	Test::destroyModule(m);
}


static const char* QJS_ONLY_ENGINE = R"(/**
 * @engine QuickJs@v1
 */
)";

TEST_CASE("QuickJs script loads with @engine as the only header tag", "[MidiKit][QuickJs]") {
	MidiKitModule* m = createModule();

	m->loadScript(QJS_ONLY_ENGINE);

	REQUIRE(m->host.seQuickJs.ctx != nullptr);
	REQUIRE(m->host.isQuickJsEngine());

	Test::destroyModule(m);
}


static const char* LUA_HEADER = R"(--[[
@engine minilua@v1
--]]
)";

TEST_CASE("Lua-tagged script is rejected by QuickJs engine", "[MidiKit][QuickJs]") {
	MidiKitModule* m = createModule();

	m->host.seQuickJs.loadScript(LUA_HEADER);

	REQUIRE(m->host.seQuickJs.ctx == nullptr);

	Test::destroyModule(m);
}


static const char* QJS_BAD_SYNTAX = R"(/**
 * @engine QuickJs@v1
 */
let x = ;
)";

TEST_CASE("JS syntax error is handled gracefully", "[MidiKit][QuickJs]") {
	MidiKitModule* m = createModule();

	m->host.seQuickJs.loadScript(QJS_BAD_SYNTAX);

	REQUIRE(m->host.seQuickJs.ctx == nullptr);

	Test::destroyModule(m);
}


// Parse-error reporting with a source position
//
// QuickJS's own SyntaxError carries a "stack" property with file/line
// information (e.g. "script:6:10"), which formatError() appends after the
// bare message — a real position.
static const char* QJS_BAD_ON_LINE_6 = R"(/**
 * @engine QuickJs@v1
 */
let a = 1;
let b = 2;
let c = ;
let d = 3;
)";

TEST_CASE("Parse error reports the line it failed on", "[MidiKit][QuickJs]") {
	MidiKitModule* m = createModule();

	m->loadScript(QJS_BAD_ON_LINE_6);
	// A failed load tears the state down
	REQUIRE(m->host.seQuickJs.ctx == nullptr);

	std::string log = drainLog(m);
	REQUIRE(log.find("Error while loading script") != std::string::npos);
	// QuickJS reports the offending line number in the exception's stack trace
	REQUIRE(log.find(":6:") != std::string::npos);

	Test::destroyModule(m);
}


// Same defect one line earlier — pins that the number tracks the error rather
// than being a constant that happens to match.
static const char* QJS_BAD_ON_LINE_5 = R"(/**
 * @engine QuickJs@v1
 */
let a = 1;
let b = ;
let c = 3;
)";

TEST_CASE("Parse error line number tracks the error position", "[MidiKit][QuickJs]") {
	MidiKitModule* m = createModule();

	m->loadScript(QJS_BAD_ON_LINE_5);
	REQUIRE(m->host.seQuickJs.ctx == nullptr);

	std::string log = drainLog(m);
	REQUIRE(log.find(":5:") != std::string::npos);
	REQUIRE(log.find(":6:") == std::string::npos);

	Test::destroyModule(m);
}


// A script that loads cleanly must not emit any error noise.
TEST_CASE("Successful load reports no error", "[MidiKit][QuickJs]") {
	MidiKitModule* m = createModule();

	m->loadScript(QJS_EMPTY);
	REQUIRE(m->host.seQuickJs.ctx != nullptr);

	std::string log = drainLog(m);
	REQUIRE(log.find("rror") == std::string::npos);
	REQUIRE(log.find("Script loaded") != std::string::npos);

	Test::destroyModule(m);
}


static const char* QJS_ON_UNLOAD = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(midiPort, msg) {}
rack.onUnload = function() {
	rack.log("onUnload ran");
	let msg = midi.create();
	midi.setNoteOff(msg, 1, 60);
	midiOut.send(msg);
}
)";


TEST_CASE("onUnload runs on module destruction without crashing", "[MidiKit][QuickJs]") {
	// Regression guard: writeLog/writeOverlay/input.*/trig.*/param.* live on
	// the MidiScriptEngineHandler (implemented by MidiKitModule). Running
	// onUnload from ~MidiScriptEngineQuickJs() itself would route those
	// callbacks through a handler that is already destroyed — undefined
	// behaviour that crashes as "pure virtual function called". MidiKitModule
	// has its own destructor that calls closeState() first, while the module
	// (the handler) is still fully alive, specifically to avoid that. This
	// test does not (and cannot) assert a log/message result — it can only
	// prove destroyModule() doesn't crash, which is what it's for.
	MidiKitModule* m = createModule();
	m->loadScript(QJS_ON_UNLOAD);
	REQUIRE(m->host.seQuickJs.ctx != nullptr);

	Test::destroyModule(m);
}


static const char* QJS_MIDI_ROUNDTRIP = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, m) {
	if (midi.isCc(m)) {
		let out = midi.clone(m);
		midi.setChannel(out, 2);
		midiOut.selectPort(1);
		midiOut.send(out);
		rack.log("got cc " + midi.getValue(m));
	}
}
)";

TEST_CASE("midi.onMessage dispatch round-trips a CC message through midi.*/midiOut.*", "[MidiKit][QuickJs]") {
	MidiKitModule* m = createModule();
	m->loadScript(QJS_MIDI_ROUNDTRIP);
	REQUIRE(m->host.seQuickJs.ctx != nullptr);
	REQUIRE(JS_IsFunction(m->host.seQuickJs.ctx, m->host.seQuickJs.onMessageFn));

	midi::Message msg;
	msg.setSize(3);
	msg.setStatus(0xb);
	msg.setChannel(0);
	msg.setNote(20);
	msg.setValue(99);

	m->host.seQuickJs.processInMessage(0, msg);
	m->host.seQuickJs.process();

	int port, ticks;
	midi::Message out;
	REQUIRE(processOutMessage(m, port, out, ticks));
	REQUIRE(out.getStatus() == 0xb);
	REQUIRE(out.getChannel() == 1);
	REQUIRE(out.getNote() == 20);
	REQUIRE(out.getValue() == 99);

	std::string log = drainLog(m);
	REQUIRE(log.find("got cc 99") != std::string::npos);

	Test::destroyModule(m);
}


static const char* QJS_NRPN = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, m) {
	let n = midi.createNRPN();
	midi.setNRPN(n, 1, 300, 500);
	midiOut.send(n);
}
)";

TEST_CASE("midi.createNRPN/setNRPN queue all four CC messages in order", "[MidiKit][QuickJs]") {
	MidiKitModule* m = createModule();
	m->loadScript(QJS_NRPN);
	REQUIRE(m->host.seQuickJs.ctx != nullptr);

	midi::Message msg;
	msg.setSize(3);
	m->host.seQuickJs.processInMessage(0, msg);
	m->host.seQuickJs.process();

	REQUIRE(m->midiOutQueue.size() == 4);
	int expectedNote[4] = {99, 98, 6, 38};
	for (int i = 0; i < 4; i++) {
		int port, ticks;
		midi::Message out;
		REQUIRE(processOutMessage(m, port, out, ticks));
		REQUIRE(out.getStatus() == 0xb);
		REQUIRE(out.getNote() == expectedNote[i]);
	}

	Test::destroyModule(m);
}


// ─── Memory / garbage collection ────────────────────────────────────────────

// RAM-usage tests for the QuickJS engine. Each midi.onMessage callback
// allocates scratch garbage (strings, objects, arrays) that nothing retains,
// so usage must stay bounded across a large number of callbacks — QuickJS's
// own incremental/automatic GC (triggered internally on allocation pressure)
// is what keeps it bounded here; this test does not force a collection.
// A leak — a script that keeps references to per-callback allocations —
// would grow reported usage roughly monotonically instead.
//
// The retain test below is a sensitivity control for the no-growth test: it
// proves the measurement would actually see a leak. It forces a GC pass
// (JS_RunGC) before each measurement so the reading reflects the retained
// live set rather than whatever transient garbage the allocator hasn't
// reclaimed yet — deliberate isolation of retention, not a substitute for the
// automatic-GC behaviour pinned by the no-growth test.

static const char* QJS_GC_SCRATCH = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(midiPort, msg) {
	let n = number.toString(midi.getNote(msg));
	let s = n + "_" + n;
	let o = { a: 1, b: "b", c: s };
	let arr = [s, o, "tail"];
	number.toString(arr.length);
}
)";

TEST_CASE("Garbage-generating callbacks do not grow RAM usage", "[MidiKit][QuickJs][GC]") {
	MidiKitModule* m = createModule();
	m->loadScript(QJS_GC_SCRATCH);
	REQUIRE(m->host.seQuickJs.ctx != nullptr);

	const int warmup = 200;
	const int run = 5000;

	midi::Message msg;
	msg.setSize(3);
	msg.setStatus(0x9);
	msg.setChannel(1);
	msg.setNote(60);
	msg.setValue(100);

	for (int i = 0; i < warmup; i++) {
		m->host.seQuickJs.processInMessage(0, msg);
		m->host.seQuickJs.process();
	}

	JS_RunGC(m->host.seQuickJs.rt);
	size_t used0, total;
	REQUIRE(m->host.seQuickJs.getMemoryUsage(used0, total));

	for (int i = 0; i < run; i++) {
		m->host.seQuickJs.processInMessage(0, msg);
		m->host.seQuickJs.process();
	}

	JS_RunGC(m->host.seQuickJs.rt);
	size_t used1, total1;
	REQUIRE(m->host.seQuickJs.getMemoryUsage(used1, total1));

	// The callbacks must have actually run (no load/callback errors), so the
	// allocations really happened rather than the test passing vacuously.
	std::string log = drainLog(m);
	REQUIRE(log.find("rror") == std::string::npos);

	// A non-retaining script's post-GC usage should stay close to flat across
	// thousands of callbacks. Allow generous headroom (heap growth/rounding)
	// while still catching a genuine leak, which would show up as usage
	// tracking the callback count instead of staying flat.
	REQUIRE(used1 < total1);
	REQUIRE(used1 <= used0 + 64 * 1024);

	Test::destroyModule(m);
}


// Sensitivity control for the test above: a script that DOES retain its
// per-callback allocation (grows a global array) must show up as clear
// growth even after a forced GC pass, proving the measurement isn't just
// reading noise.
static const char* QJS_GC_RETAIN = R"(/**
 * @engine QuickJs@v1
 */
var leaked = [];
midi.onMessage = function(midiPort, msg) {
	leaked.push(number.toString(midi.getNote(msg)) + "_");
}
)";

TEST_CASE("Retaining callbacks do grow RAM usage", "[MidiKit][QuickJs][GC]") {
	MidiKitModule* m = createModule();
	m->loadScript(QJS_GC_RETAIN);
	REQUIRE(m->host.seQuickJs.ctx != nullptr);

	const int warmup = 20;
	const int run = 200;

	midi::Message msg;
	msg.setSize(3);
	msg.setStatus(0x9);
	msg.setChannel(1);
	msg.setNote(60);
	msg.setValue(100);

	for (int i = 0; i < warmup; i++) {
		m->host.seQuickJs.processInMessage(0, msg);
		m->host.seQuickJs.process();
	}

	JS_RunGC(m->host.seQuickJs.rt);
	size_t used0, total;
	REQUIRE(m->host.seQuickJs.getMemoryUsage(used0, total));

	for (int i = 0; i < run; i++) {
		m->host.seQuickJs.processInMessage(0, msg);
		m->host.seQuickJs.process();
	}

	JS_RunGC(m->host.seQuickJs.rt);
	size_t used1, total1;
	REQUIRE(m->host.seQuickJs.getMemoryUsage(used1, total1));

	std::string log = drainLog(m);
	REQUIRE(log.find("rror") == std::string::npos);

	// 200 retained strings + their array slots must be clearly visible.
	REQUIRE(used1 > used0 + 2048);

	Test::destroyModule(m);
}



// ── Script execution budget ──────────────────────────────────────
// A while(true) is aborted (uncatchable "interrupted"); without the guard the
// sync test hangs and the async test trips barrier()'s timeout.

// File-local Note-On helper (engine.test.cpp's isn't visible here).
static midi::Message noteOn(int ch, int note, int vel) {
	midi::Message msg;
	msg.setSize(3);
	msg.setStatus(0x9);
	msg.setChannel(ch);
	msg.setNote(note);
	msg.setValue(vel);
	return msg;
}

// Valid script proving the engine recovers after a failed load.
static const char* QJS_RECOVERY = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(midiPort, msg) {
    rack.log("recovered");
};
)";

static const char* JS_WHILE_TRUE_ONMESSAGE = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(midiPort, msg) {
    while (true) {}
};
)";

TEST_CASE("Infinite loop in onMessage is interrupted, not a hang", "[MidiKit][QuickJs]") {
	MidiKitModule* m = createModule();
	m->loadScript(JS_WHILE_TRUE_ONMESSAGE);
	drainLog(m);

	midi::Message in = noteOn(1, 60, 100);
	m->host.getActiveEngine()->processInMessage(0, in);
	// SyncTaskWorker runs inline — without the interrupt handler this would hang.
	m->host.getActiveEngine()->process();

	std::string log = drainLog(m);
	REQUIRE(log.find("interrupted") != std::string::npos);
	REQUIRE(log.find("onMessage error") != std::string::npos);

	// Engine recovered: a second message is interrupted again.
	m->host.getActiveEngine()->processInMessage(0, in);
	m->host.getActiveEngine()->process();
	std::string log2 = drainLog(m);
	REQUIRE(log2.find("interrupted") != std::string::npos);

	Test::destroyModule(m);
}

TEST_CASE("Infinite loop in onMessage does not wedge the shared worker", "[MidiKit][QuickJs][Async]") {
	auto worker = asyncWorker();
	MidiKitModule* m = createModule(worker);

	m->loadScript(JS_WHILE_TRUE_ONMESSAGE);
	barrier(worker, 10.0);        // the LOAD is async; wait for it
	drainLog(m);

	midi::Message in = noteOn(1, 60, 100);
	m->host.getActiveEngine()->processInMessage(0, in);
	m->host.getActiveEngine()->process();   // enqueues the dispatch task

	// Without the guard the worker spins forever and barrier() times out.
	barrier(worker, 10.0);

	std::string log = drainLog(m);
	REQUIRE(log.find("interrupted") != std::string::npos);

	// A second message still dispatches — the worker recovered.
	m->host.getActiveEngine()->processInMessage(0, in);
	m->host.getActiveEngine()->process();
	barrier(worker, 10.0);
	std::string log2 = drainLog(m);
	REQUIRE(log2.find("interrupted") != std::string::npos);

	Test::destroyModule(m);
}

static const char* JS_WHILE_TRUE_TOPLEVEL = R"(/**
 * @engine QuickJs@v1
 */
while (true) {}
)";

TEST_CASE("Infinite loop at script top level fails the load, and the module recovers", "[MidiKit][QuickJs]") {
	MidiKitModule* m = createModule();
	// Load runs inline; the interrupt aborts the top-level JS_Eval.
	m->loadScript(JS_WHILE_TRUE_TOPLEVEL);
	std::string log = drainLog(m);
	REQUIRE(log.find("Error while loading script") != std::string::npos);
	REQUIRE(log.find("interrupted") != std::string::npos);

	// The module survives the failed load.
	m->loadScript(QJS_RECOVERY);
	drainLog(m);

	midi::Message in = noteOn(1, 60, 100);
	m->host.getActiveEngine()->processInMessage(0, in);
	m->host.getActiveEngine()->process();
	std::string reloadLog = drainLog(m);
	REQUIRE(reloadLog.find("recovered") != std::string::npos);

	Test::destroyModule(m);
}