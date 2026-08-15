#include "MidiKit.test.hpp"

// ─── NRPN / RPN / 14-bit CC input — cross-engine suite ─────────────────────
//
// Covers the plan's §6 test list (var/MidiKit_nrpn_cc14_input_draft.md): the
// module-level assembly through MidiProcessor, the consumption rules in
// processMidi(), and the script-facing API that work items 1-7 added
// (midi.enableNrpnIn/enableRpnIn/enableCc14bitIn, the onNrpn/onRpn/onCc14bit
// callbacks, and the isNrpn/isRpn/isCc14bit/getControl/type-aware getValue
// accessors).
//
// These are script-driven END-TO-END tests: each case loads a script, feeds
// raw MIDI into the module's real input queue, and pumps process() past the
// divider, so the whole stack (decoder → processMidi consumption → engine
// dispatch → callback) is exercised, in both engines.
//
// The observable output is the script's runtime log. Scripts log every event
// they receive with a "P:" prefix (a load-time probe pattern borrowed from
// MidiKit.engine.test.cpp), and the harness asserts the exact ordered list of
// probe lines both engines produced. Framework chatter (load messages) lives
// in the load-time log, kept separate so it never pollutes the comparison.
//
// The two engines must produce IDENTICAL probe text: numbers format the same
// way, and booleans are coerced to "true"/"false" by both. Lua scripts use
// tostring() on booleans (the `..` operator would reject them); JS relies on
// String coercion.

using namespace StoermelderPackOne::MidiKit;

// ─── Message builders ───────────────────────────────────────────────────────

static midi::Message makeCc(uint8_t ch, uint8_t num, uint8_t value) {
	return Test::makeMidiMessage(0xb, ch, num, value);
}

static midi::Message makeNote(uint8_t ch, uint8_t n, uint8_t vel) {
	return Test::makeMidiMessage(0x9, ch, n, vel);
}

static midi::Message makePitchBend(uint8_t ch, uint8_t lsb, uint8_t msb) {
	return Test::makeMidiMessage(0xe, ch, lsb, msb);
}

static midi::Message makeClock() {
	return Test::makeMidiMessage(0xf, 0x8, 0, 0);
}

// The four CCs that make one NRPN parameter change on `ch`: parameter select
// (99/98), then data entry (6/38).
static std::vector<midi::Message> nrpnQuad(uint8_t ch, uint8_t pMsb, uint8_t pLsb, uint8_t vMsb, uint8_t vLsb) {
	return { makeCc(ch, 99, pMsb), makeCc(ch, 98, pLsb), makeCc(ch, 6, vMsb), makeCc(ch, 38, vLsb) };
}

// The four CCs that make one RPN parameter change (parameter select 101/100).
static std::vector<midi::Message> rpnQuad(uint8_t ch, uint8_t pMsb, uint8_t pLsb, uint8_t vMsb, uint8_t vLsb) {
	return { makeCc(ch, 101, pMsb), makeCc(ch, 100, pLsb), makeCc(ch, 6, vMsb), makeCc(ch, 38, vLsb) };
}

// An NRPN quad followed by a plain CC, for the slot-reuse test: the assembled
// event is dispatched first, then the plain CC reads the same slot.
static std::vector<midi::Message> nrpnQuadThenCc() {
	auto v = nrpnQuad(0, 4, 5, 20, 2);
	v.push_back(makeCc(0, 7, 64));
	return v;
}

// ─── Harness ────────────────────────────────────────────────────────────────

// Feeds raw MIDI into the module's real input queue and pumps process() past
// the divider (8), so the queue is actually decoded and dispatched. Under
// SyncTaskWorker the dispatch runs inline, so when this returns every callback
// has fired and its log entries are queued.
static void feedMidiPump(MidiKitModule* m, const std::vector<midi::Message>& msgs) {
	int64_t frame = 1;
	for (const auto& msg : msgs) m->midiInput.onMessage(msg);
	for (int i = 0; i < 9; i++) m->process(Test::makeProcessArgs(frame++));
}

// Extracts the "P:"-prefixed probe lines from a log dump, stripping the prefix.
static std::vector<std::string> extractProbes(const std::string& log) {
	std::vector<std::string> out;
	size_t pos = 0;
	while (pos < log.size()) {
		size_t nl = log.find('\n', pos);
		if (nl == std::string::npos) break;
		std::string line = log.substr(pos, nl - pos);
		if (line.compare(0, 2, "P:") == 0) out.push_back(line.substr(2));
		pos = nl + 1;
	}
	return out;
}

struct NrpnResult {
	std::string loadLog;
	std::vector<std::string> probes;
};

// Loads `script`, feeds `in`, and returns the runtime probe lines. Fails the
// test loudly if the script did not load or the load reported an error.
static NrpnResult runIn(const std::string& script, const std::vector<midi::Message>& in) {
	MidiKitModule* m = createModule();
	m->loadScript(script);

	NrpnResult r;
	r.loadLog = drainLog(m);
	CATCH_INFO("load log:\n" << r.loadLog);
	REQUIRE(r.loadLog.find("rror") == std::string::npos);
	REQUIRE(m->host.getActiveEngine() != nullptr);

	feedMidiPump(m, in);
	r.probes = extractProbes(drainLog(m));

	Test::destroyModule(m);
	return r;
}

// ─── Both-engine iteration ─────────────────────────────────────────────────
// The cross-engine tests drive the two engines with Catch2's GENERATE, one
// engine per TEST_CASE body run — the same shape presetPaths() uses in
// MidiKit.examples.test.cpp. A failure then names the engine that broke and
// the other engine still runs. The full JS_*/LUA_* script constants stay.
//
// enableScript() (defined in the enable-binding helpers below) builds the
// one-line scripts the validation tests use; the single-arg overload below
// needs it.
static std::string enableScript(bool js, const std::string& call);

struct EngineVariant {
	const char* engine;   // "QuickJs" / "Lua" — for CATCH_INFO
	std::string script;   // std::string so both the static constants and
	                      // enableScript()'s output work here
};

// Yields each engine's variant of a JS/Lua script pair, one per GENERATE pass.
static auto engineVariants(const char* js, const char* lua) {
	return Catch::Generators::map(
		[js, lua](int i) -> EngineVariant {
			return i == 0 ? EngineVariant{"QuickJs", js} : EngineVariant{"Lua", lua};
		},
		Catch::Generators::range(0, 2));
}

// Overload for one-line enableScript calls: builds both engines' scripts from
// the same call fragment, so the validation tests use the same
// `EngineVariant v = GENERATE(engineVariants(...))` shape as the full-constant
// pairs without a JS/LUA constant pair per case.
static auto engineVariants(const std::string& call) {
	return Catch::Generators::map(
		[call](int i) -> EngineVariant {
			return i == 0 ? EngineVariant{"QuickJs", enableScript(true, call)}
			              : EngineVariant{"Lua", enableScript(false, call)};
		},
		Catch::Generators::range(0, 2));
}

// Runs one engine's variant against `in` and asserts it produced exactly
// `expected` probe lines (empty asserts silence) — the per-engine body of a
// GENERATE pass.
static void assertProbes(const EngineVariant& v, const std::vector<midi::Message>& in,
                         const std::vector<std::string>& expected) {
	CATCH_INFO("engine: " << v.engine << "\n" << v.script);
	NrpnResult r = runIn(v.script, in);
	CATCH_INFO("probes:\n" << [&]() { std::string s; for (auto& p : r.probes) s += "  " + p + "\n"; return s; }());
	REQUIRE(r.probes == expected);
}

// ─── Enable-binding validation helpers ──────────────────────────────────────

// Builds a minimal engine-tagged script whose body is the single call `call`.
// The enable-validation cases differ only in the argument list, so a full
// constant per case would be many near-identical pairs.
static std::string enableScript(bool js, const std::string& call) {
	if (js) return "/**\n * @engine QuickJs@v1\n */\n" + call + ";\n";
	return "--[[\n@engine minilua@v1\n--]]\n" + call + "\n";
}

// Loads one engine's variant and asserts the load was rejected: `token` is in
// the log and the engine state was torn down. The two engines word validation
// errors differently ("midiPort out of range" vs "bad midiPort", "channel
// must be 1-16" vs "bad channel"), so callers assert a common token rather
// than the full message. Which engine's teardown state to check is read from
// the script's @engine header.
static void assertLoadRejected(const std::string& script, const char* token) {
	MidiKitModule* m = createModule();
	m->loadScript(script);
	std::string log = drainLog(m);
	CATCH_INFO("log:\n" << log);
	REQUIRE(log.find(token) != std::string::npos);
	if (script.find("@engine QuickJs@v1") != std::string::npos)
		REQUIRE(m->host.seQuickJs.ctx == nullptr);
	else
		REQUIRE(m->host.seLua.L == nullptr);
	Test::destroyModule(m);
}

TEST_CASE("enableNrpnIn rejects a bad midiPort", "[MidiKit][MidiProcessor]") {
	// Port 0 is below the 1-based range, on the shared NRPN/RPN binding path
	// (enableRpnIn is covered separately). Both engines.
	EngineVariant v = GENERATE(engineVariants("midi.enableNrpnIn(0)"));
	assertLoadRejected(v.script, "midiPort");
}

TEST_CASE("enableCc14bitIn rejects a bad midiPort", "[MidiKit][MidiProcessor]") {
	EngineVariant v = GENERATE(engineVariants("midi.enableCc14bitIn(0)"));
	assertLoadRejected(v.script, "midiPort");
}

TEST_CASE("enableNrpnIn and enableCc14bitIn reject a channel outside 1-16", "[MidiKit][MidiProcessor]") {
	// Channel is the boundary that matters most: an off-by-one (ch >= 16)
	// would silently accept 17 and shift past the 16-bit mask. Both ends of
	// the range are pinned, for both bindings, in both engines. The four calls
	// are each iterated over both engines by the combined generator.
	EngineVariant v = GENERATE(engineVariants("midi.enableNrpnIn(1, 0)"),
	                           engineVariants("midi.enableNrpnIn(1, 17)"),
	                           engineVariants("midi.enableCc14bitIn(1, 7, 0)"),
	                           engineVariants("midi.enableCc14bitIn(1, 7, 17)"));
	assertLoadRejected(v.script, "channel");
}

TEST_CASE("enableNrpnIn accepts channel 16 and arms bit 15", "[MidiKit][MidiProcessor]") {
	// Channel 16 (1-based) is the last valid channel and must arm bit 15
	// (0-based) of the mask — a `ch >= 16` off-by-one would reject it, and a
	// missing upper bound would let it shift past the mask entirely. Both
	// engines.
	EngineVariant v = GENERATE(engineVariants("midi.enableNrpnIn(1, 16)"));
	CATCH_INFO("engine: " << v.engine);
	MidiKitModule* m = createModule();
	m->loadScript(v.script);
	REQUIRE(m->host.getActiveEngine() != nullptr);
	REQUIRE(m->isNrpnEnabled(15, false));
	REQUIRE(!m->isNrpnEnabled(0, false));
	Test::destroyModule(m);
}

TEST_CASE("enableCc14bitIn accepts channel 16 and arms bit 15", "[MidiKit][MidiProcessor]") {
	EngineVariant v = GENERATE(engineVariants("midi.enableCc14bitIn(1, 7, 16)"));
	CATCH_INFO("engine: " << v.engine);
	MidiKitModule* m = createModule();
	m->loadScript(v.script);
	REQUIRE(m->host.getActiveEngine() != nullptr);
	REQUIRE(m->isCc14bitEnabled(15, 7));
	REQUIRE(!m->isCc14bitEnabled(0, 7));
	Test::destroyModule(m);
}


// ─── Tests ──────────────────────────────────────────────────────────────────


// Enables NRPN, logs the assembled-type flags inside onNrpn.
static const char* JS_FLAGS = R"(/**
 * @engine QuickJs@v1
 */
midi.enableNrpnIn(1);

midi.onNrpn = function(midiPort, msg) {
    rack.log("P:flags:" + midi.isNrpn(msg) + ":" + midi.isRpn(msg) + ":" + midi.isCc14bit(msg));
};
)";

static const char* LUA_FLAGS = R"(--[[
@engine minilua@v1
--]]
midi.enableNrpnIn(1)

midi.onNrpn = function(midiPort, msg)
    rack.log("P:flags:" .. tostring(midi.isNrpn(msg)) .. ":" .. tostring(midi.isRpn(msg)) .. ":" .. tostring(midi.isCc14bit(msg)))
end
)";

TEST_CASE("isNrpn/isRpn/isCc14bit flags inside onNrpn", "[MidiKit][MidiProcessor][CrossEngine]") {
	EngineVariant v = GENERATE(engineVariants(JS_FLAGS, LUA_FLAGS));
	assertProbes(v, nrpnQuad(0, 4, 5, 20, 2), {"flags:true:false:false"});
}



// Enables NRPN; onMessage reports the flags plus the raw value/note, to pin
// that a plain message does not read a previous assembled one's decode state.
static const char* JS_STALE = R"(/**
 * @engine QuickJs@v1
 */
midi.enableNrpnIn(1);

midi.onNrpn = function(midiPort, msg) {
    rack.log("P:onNrpn:" + midi.getControl(msg) + ":" + midi.getValue(msg));
};

midi.onMessage = function(midiPort, msg) {
    rack.log("P:msg:" + midi.isNrpn(msg) + ":" + midi.isRpn(msg) + ":" + midi.isCc14bit(msg) + ":" + midi.getValue(msg) + ":" + midi.getNote(msg));
};
)";

static const char* LUA_STALE = R"(--[[
@engine minilua@v1
--]]
midi.enableNrpnIn(1)

midi.onNrpn = function(midiPort, msg)
    rack.log("P:onNrpn:" .. midi.getControl(msg) .. ":" .. midi.getValue(msg))
end

midi.onMessage = function(midiPort, msg)
    rack.log("P:msg:" .. tostring(midi.isNrpn(msg)) .. ":" .. tostring(midi.isRpn(msg)) .. ":" .. tostring(midi.isCc14bit(msg)) .. ":" .. midi.getValue(msg) .. ":" .. midi.getNote(msg))
end
)";

TEST_CASE("A plain message after an assembled one reads its own value", "[MidiKit][MidiProcessor][CrossEngine]") {
	// Slot 0 is reused per callback, so the decode result must not leak from
	// the assembled NRPN into the following plain CC: getValue returns the raw
	// 7-bit byte and all three predicates read false.
	EngineVariant v = GENERATE(engineVariants(JS_STALE, LUA_STALE));
	assertProbes(v, nrpnQuadThenCc(), {"onNrpn:517:2562", "msg:false:false:false:64:7"});
}



// Pins the §3.0 split on one handle: getControl/getValue/getNote return three
// different things for the same assembled message.
static const char* JS_THREE = R"(/**
 * @engine QuickJs@v1
 */
midi.enableNrpnIn(1);

midi.onNrpn = function(midiPort, msg) {
    rack.log("P:three:" + midi.getControl(msg) + ":" + midi.getValue(msg) + ":" + midi.getNote(msg));
};
)";

static const char* LUA_THREE = R"(--[[
@engine minilua@v1
--]]
midi.enableNrpnIn(1)

midi.onNrpn = function(midiPort, msg)
    rack.log("P:three:" .. midi.getControl(msg) .. ":" .. midi.getValue(msg) .. ":" .. midi.getNote(msg))
end
)";

TEST_CASE("getNote/getControl/getValue differ on the same assembled handle", "[MidiKit][MidiProcessor][CrossEngine]") {
	// Completing CC 38, parameter 517, combined 14-bit value 2562.
	EngineVariant v = GENERATE(engineVariants(JS_THREE, LUA_THREE));
	assertProbes(v, nrpnQuad(0, 4, 5, 20, 2), {"three:517:2562:38"});
}



// No enables: onMessage reports getControl + getNote, for the whole-family
// getter test (pins that on a plain CC getControl equals getNote, the older
// spelling).
static const char* JS_CTRL = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(midiPort, msg) {
    rack.log("P:ctrl:" + midi.getControl(msg) + ":" + midi.getNote(msg));
};
)";

static const char* LUA_CTRL = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    rack.log("P:ctrl:" .. midi.getControl(msg) .. ":" .. midi.getNote(msg))
end
)";

TEST_CASE("getControl answers for the whole message family", "[MidiKit][MidiProcessor][CrossEngine]") {
	// Controller number on a plain CC (equal to getNote there); -1 on note,
	// pitch bend and clock. The second field is getNote, unchanged.
	EngineVariant v = GENERATE(engineVariants(JS_CTRL, LUA_CTRL));
	assertProbes(v, {makeCc(0, 7, 64), makeNote(0, 60, 100), makePitchBend(0, 1, 0), makeClock()},
		{"ctrl:7:7", "ctrl:-1:60", "ctrl:-1:1", "ctrl:-1:0"});
}



// No enables: onMessage reports getValue/getNote, for the 7-bit non-widening
// test.
static const char* JS_VAL = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(midiPort, msg) {
    rack.log("P:val:" + midi.getValue(msg) + ":" + midi.getNote(msg));
};
)";

static const char* LUA_VAL = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    rack.log("P:val:" .. midi.getValue(msg) .. ":" .. midi.getNote(msg))
end
)";

TEST_CASE("getValue stays 7-bit on plain messages", "[MidiKit][MidiProcessor][CrossEngine]") {
	// The type-aware widening must not leak into what existing scripts read.
	EngineVariant v = GENERATE(engineVariants(JS_VAL, LUA_VAL));
	assertProbes(v, {makeCc(0, 7, 64), makeNote(0, 60, 100)}, {"val:64:7", "val:100:60"});
}



// Enables NRPN but defines no onNrpn: an enabled kind with no handler must
// drop the assembled message silently and not fall back to onMessage.
static const char* JS_NOCB = R"(/**
 * @engine QuickJs@v1
 */
midi.enableNrpnIn(1);

midi.onMessage = function(midiPort, msg) {
    rack.log("P:onMessage:" + midi.getControl(msg));
};
)";

static const char* LUA_NOCB = R"(--[[
@engine minilua@v1
--]]
midi.enableNrpnIn(1)

midi.onMessage = function(midiPort, msg)
    rack.log("P:onMessage:" .. midi.getControl(msg))
end
)";

// No enables at all: onMessage must see every component CC raw.
static const char* JS_PASS = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(midiPort, msg) {
    rack.log("P:onMessage:" + midi.getControl(msg));
};
)";

static const char* LUA_PASS = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    rack.log("P:onMessage:" .. midi.getControl(msg))
end
)";

TEST_CASE("An enabled kind with no callback drops the message silently", "[MidiKit][MidiProcessor][CrossEngine]") {
	// NRPN enabled, no onNrpn: the assembled message reaches nothing, and the
	// components are withheld from onMessage — the script sees strictly less
	// MIDI, exactly as the plan warns.
	EngineVariant v = GENERATE(engineVariants(JS_NOCB, LUA_NOCB));
	assertProbes(v, nrpnQuad(0, 4, 5, 20, 2), {});
}

TEST_CASE("Component CCs are withheld while enabled and pass when not", "[MidiKit][MidiProcessor][CrossEngine]") {
	SECTION("Enabled: the four component CCs never reach onMessage") {
		EngineVariant v = GENERATE(engineVariants(JS_NOCB, LUA_NOCB));
		assertProbes(v, nrpnQuad(0, 4, 5, 20, 2), {});
	}
	SECTION("Not enabled: onMessage sees every component CC raw") {
		EngineVariant v = GENERATE(engineVariants(JS_PASS, LUA_PASS));
		assertProbes(v, nrpnQuad(0, 4, 5, 20, 2),
			{"onMessage:99", "onMessage:98", "onMessage:6", "onMessage:38"});
	}
}



// Blanket 14-bit + NRPN: the §4.1 overlap, accepted as option (1) — CC 6/38
// are consumed (never reach onMessage) and BOTH the NRPN and the 14-bit event
// fire.
static const char* JS_BOTH = R"(/**
 * @engine QuickJs@v1
 */
midi.enableCc14bitIn(1);
midi.enableNrpnIn(1);

midi.onMessage = function(midiPort, msg) {
    rack.log("P:onMessage:" + midi.getControl(msg));
};

midi.onNrpn = function(midiPort, msg) {
    rack.log("P:onNrpn:" + midi.getControl(msg) + ":" + midi.getValue(msg) + ":" + midi.getChannel(msg) + ":" + midi.getNote(msg));
};

midi.onCc14bit = function(midiPort, msg) {
    rack.log("P:onCc14bit:" + midi.getControl(msg) + ":" + midi.getValue(msg) + ":" + midi.getChannel(msg) + ":" + midi.getNote(msg));
};
)";

static const char* LUA_BOTH = R"(--[[
@engine minilua@v1
--]]
midi.enableCc14bitIn(1)
midi.enableNrpnIn(1)

midi.onMessage = function(midiPort, msg)
    rack.log("P:onMessage:" .. midi.getControl(msg))
end

midi.onNrpn = function(midiPort, msg)
    rack.log("P:onNrpn:" .. midi.getControl(msg) .. ":" .. midi.getValue(msg) .. ":" .. midi.getChannel(msg) .. ":" .. midi.getNote(msg))
end

midi.onCc14bit = function(midiPort, msg)
    rack.log("P:onCc14bit:" .. midi.getControl(msg) .. ":" .. midi.getValue(msg) .. ":" .. midi.getChannel(msg) .. ":" .. midi.getNote(msg))
end
)";

TEST_CASE("Blanket 14-bit + NRPN consumes CC 6/38 — option (1)", "[MidiKit][MidiProcessor][CrossEngine]") {
	// §4.1 resolved as option (1): with both enabled, CC 6/38 are consumed by
	// the 14-bit rule (never raw) and data entry fires BOTH the NRPN and the
	// 14-bit event. Pins the accepted overlap so it is not later "fixed" into
	// option (2) (NRPN precedence, which would suppress the onCc14bit).
	EngineVariant v = GENERATE(engineVariants(JS_BOTH, LUA_BOTH));
	assertProbes(v, nrpnQuad(0, 4, 5, 20, 2),
		{"onNrpn:517:2562:1:38", "onCc14bit:6:2562:1:38"});
}



// Per-CC 14-bit registration on CC 7 only: the escape hatch. NRPN data entry
// (CC 6/38) is NOT consumed by the 14-bit rule and reaches onMessage raw.
static const char* JS_ESCAPE = R"(/**
 * @engine QuickJs@v1
 */
midi.enableCc14bitIn(1, 7);

midi.onMessage = function(midiPort, msg) {
    rack.log("P:onMessage:" + midi.getControl(msg));
};
)";

static const char* LUA_ESCAPE = R"(--[[
@engine minilua@v1
--]]
midi.enableCc14bitIn(1, 7)

midi.onMessage = function(midiPort, msg)
    rack.log("P:onMessage:" .. midi.getControl(msg))
end
)";

TEST_CASE("Per-CC registration is the escape hatch for CC 6/38", "[MidiKit][MidiProcessor][CrossEngine]") {
	// With 14-bit registered on CC 7 only (not blanket), NRPN data entry on
	// CC 6/38 is not consumed by the 14-bit rule and reaches onMessage raw —
	// all four CCs of the quad.
	EngineVariant v = GENERATE(engineVariants(JS_ESCAPE, LUA_ESCAPE));
	assertProbes(v, nrpnQuad(0, 4, 5, 20, 2),
		{"onMessage:99", "onMessage:98", "onMessage:6", "onMessage:38"});
}



// Enables NRPN and logs every callback it receives. onNrpn/onRpn/onCc14bit
// report control, value, channel and completing-CC; onMessage reports only the
// controller number, so a test can tell at a glance which raw CCs leaked
// through.
static const char* JS_NRPN = R"(/**
 * @engine QuickJs@v1
 */
midi.enableNrpnIn(1);

midi.onMessage = function(midiPort, msg) {
    rack.log("P:onMessage:" + midi.getControl(msg));
};

midi.onNrpn = function(midiPort, msg) {
    rack.log("P:onNrpn:" + midi.getControl(msg) + ":" + midi.getValue(msg) + ":" + midi.getChannel(msg) + ":" + midi.getNote(msg));
};

midi.onRpn = function(midiPort, msg) {
    rack.log("P:onRpn:" + midi.getControl(msg) + ":" + midi.getValue(msg) + ":" + midi.getChannel(msg) + ":" + midi.getNote(msg));
};

midi.onCc14bit = function(midiPort, msg) {
    rack.log("P:onCc14bit:" + midi.getControl(msg) + ":" + midi.getValue(msg) + ":" + midi.getChannel(msg) + ":" + midi.getNote(msg));
};
)";

static const char* LUA_NRPN = R"(--[[
@engine minilua@v1
--]]
midi.enableNrpnIn(1)

midi.onMessage = function(midiPort, msg)
    rack.log("P:onMessage:" .. midi.getControl(msg))
end

midi.onNrpn = function(midiPort, msg)
    rack.log("P:onNrpn:" .. midi.getControl(msg) .. ":" .. midi.getValue(msg) .. ":" .. midi.getChannel(msg) .. ":" .. midi.getNote(msg))
end

midi.onRpn = function(midiPort, msg)
    rack.log("P:onRpn:" .. midi.getControl(msg) .. ":" .. midi.getValue(msg) .. ":" .. midi.getChannel(msg) .. ":" .. midi.getNote(msg))
end

midi.onCc14bit = function(midiPort, msg)
    rack.log("P:onCc14bit:" .. midi.getControl(msg) .. ":" .. midi.getValue(msg) .. ":" .. midi.getChannel(msg) .. ":" .. midi.getNote(msg))
end
)";

TEST_CASE("Interleaved NRPN on two channels assemble independently", "[MidiKit][MidiProcessor][CrossEngine]") {
	// Channel 1: param 1*128+3 = 131, value 10*128+5 = 1285.
	// Channel 2: param 2*128+4 = 260, value 7*128+2 = 898.
	EngineVariant v = GENERATE(engineVariants(JS_NRPN, LUA_NRPN));
	assertProbes(v,
		{makeCc(0, 99, 1), makeCc(1, 99, 2), makeCc(0, 98, 3), makeCc(1, 98, 4),
		 makeCc(0, 6, 10), makeCc(0, 38, 5), makeCc(1, 6, 7), makeCc(1, 38, 2)},
		{"onNrpn:131:1285:1:38", "onNrpn:260:898:2:38"});
}

TEST_CASE("Parameter select alone fires nothing; following data entry does", "[MidiKit][MidiProcessor][CrossEngine]") {
	// The select CCs are consumed AND their assembled select event is dropped
	// by hasValue() gating — nothing reaches the script.
	EngineVariant v = GENERATE(engineVariants(JS_NRPN, LUA_NRPN));
	assertProbes(v, {makeCc(0, 99, 4), makeCc(0, 98, 5)}, {});
	// Data entry on an armed parameter assembles the one change.
	assertProbes(v, nrpnQuad(0, 4, 5, 20, 2), {"onNrpn:517:2562:1:38"});
}

TEST_CASE("NRPN quad assembles into one onNrpn with the decoded handle", "[MidiKit][MidiProcessor][CrossEngine]") {
	// param 4*128+5 = 517, value 20*128+2 = 2562, channel 1, completing CC 38.
	EngineVariant v = GENERATE(engineVariants(JS_NRPN, LUA_NRPN));
	assertProbes(v, nrpnQuad(0, 4, 5, 20, 2), {"onNrpn:517:2562:1:38"});
}



// Enables NRPN + RPN so data entry can attribute to either type.
static const char* JS_RPNNRPN = R"(/**
 * @engine QuickJs@v1
 */
midi.enableNrpnIn(1);
midi.enableRpnIn(1);

midi.onNrpn = function(midiPort, msg) {
    rack.log("P:onNrpn:" + midi.getControl(msg) + ":" + midi.getValue(msg) + ":" + midi.getChannel(msg) + ":" + midi.getNote(msg));
};

midi.onRpn = function(midiPort, msg) {
    rack.log("P:onRpn:" + midi.getControl(msg) + ":" + midi.getValue(msg) + ":" + midi.getChannel(msg) + ":" + midi.getNote(msg));
};
)";

static const char* LUA_RPNNRPN = R"(--[[
@engine minilua@v1
--]]
midi.enableNrpnIn(1)
midi.enableRpnIn(1)

midi.onNrpn = function(midiPort, msg)
    rack.log("P:onNrpn:" .. midi.getControl(msg) .. ":" .. midi.getValue(msg) .. ":" .. midi.getChannel(msg) .. ":" .. midi.getNote(msg))
end

midi.onRpn = function(midiPort, msg)
    rack.log("P:onRpn:" .. midi.getControl(msg) .. ":" .. midi.getValue(msg) .. ":" .. midi.getChannel(msg) .. ":" .. midi.getNote(msg))
end
)";

TEST_CASE("RPN and NRPN data entry attributes to the type last selected", "[MidiKit][MidiProcessor][CrossEngine]") {
	// RPN param 1*128+2 = 130 armed first, then NRPN 3*128+4 = 388; each data
	// entry lands on whichever parameter is armed at that moment.
	EngineVariant v = GENERATE(engineVariants(JS_RPNNRPN, LUA_RPNNRPN));
	assertProbes(v,
		{makeCc(0, 101, 1), makeCc(0, 100, 2), makeCc(0, 6, 20), makeCc(0, 38, 2),
		 makeCc(0, 99, 3), makeCc(0, 98, 4), makeCc(0, 6, 10), makeCc(0, 38, 7)},
		{"onRpn:130:2562:1:38", "onNrpn:388:1287:1:38"});
}



// Blanket 14-bit only: a component of NRPN (CC 98) must still reach onMessage,
// because the matching enable (NRPN) is not set — isComponent alone must not
// drive consumption.
static const char* JS_CC14 = R"(/**
 * @engine QuickJs@v1
 */
midi.enableCc14bitIn(1);

midi.onMessage = function(midiPort, msg) {
    rack.log("P:onMessage:" + midi.getControl(msg));
};

midi.onCc14bit = function(midiPort, msg) {
    rack.log("P:onCc14bit:" + midi.getControl(msg) + ":" + midi.getValue(msg) + ":" + midi.getChannel(msg) + ":" + midi.getNote(msg));
};
)";

static const char* LUA_CC14 = R"(--[[
@engine minilua@v1
--]]
midi.enableCc14bitIn(1)

midi.onMessage = function(midiPort, msg)
    rack.log("P:onMessage:" .. midi.getControl(msg))
end

midi.onCc14bit = function(midiPort, msg)
    rack.log("P:onCc14bit:" .. midi.getControl(msg) .. ":" .. midi.getValue(msg) .. ":" .. midi.getChannel(msg) .. ":" .. midi.getNote(msg))
end
)";

TEST_CASE("A 14-bit value of 0 still fires", "[MidiKit][MidiProcessor][CrossEngine]") {
	// extraValue 0 is a value (hasValue() is >= 0), not a select placeholder:
	// an MSB seen before, reset to 0, then an LSB of 0 assembles value 0. The
	// first MSB escapes raw (documented — the decoder cannot know a pair is
	// coming), the rest are consumed.
	EngineVariant v = GENERATE(engineVariants(JS_CC14, LUA_CC14));
	assertProbes(v, {makeCc(0, 7, 1), makeCc(0, 7, 0), makeCc(0, 39, 0)},
		{"onMessage:7", "onCc14bit:7:0:1:39"});
}

TEST_CASE("Only-14-bit script still receives raw CC 98", "[MidiKit][MidiProcessor][CrossEngine]") {
	// isComponent alone must not drive consumption: CC 98 is a component of an
	// NRPN, but with only 14-bit enabled the matching enable is unset, so it
	// reaches onMessage raw. The NRPN select never reaches onNrpn (not enabled).
	EngineVariant v = GENERATE(engineVariants(JS_CC14, LUA_CC14));
	assertProbes(v, {makeCc(0, 99, 4), makeCc(0, 98, 5)}, {"onMessage:99", "onMessage:98"});
}

TEST_CASE("MSB of 0 on a never-seen CC produces no event", "[MidiKit][MidiProcessor][CrossEngine]") {
	// MidiProcessor's deliberate deviation: an MSB of value 0 is ignored unless
	// already seen, so CC 7=0 then CC 39 assembles nothing — both reach
	// onMessage raw.
	EngineVariant v = GENERATE(engineVariants(JS_CC14, LUA_CC14));
	assertProbes(v, {makeCc(0, 7, 0), makeCc(0, 39, 42)}, {"onMessage:7", "onMessage:39"});
}



// For the direct module-state tests: A enables NRPN and defines the callbacks;
// B defines the same callbacks but does NOT enable.
static const char* JS_RELOAD_A = R"(/**
 * @engine QuickJs@v1
 */
midi.enableNrpnIn(1);

midi.onMessage = function(midiPort, msg) {
    rack.log("P:onMessage:" + midi.getControl(msg));
};

midi.onNrpn = function(midiPort, msg) {
    rack.log("P:onNrpn:" + midi.getControl(msg) + ":" + midi.getValue(msg));
};
)";

static const char* JS_RELOAD_B = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(midiPort, msg) {
    rack.log("P:onMessage:" + midi.getControl(msg));
};

midi.onNrpn = function(midiPort, msg) {
    rack.log("P:onNrpn:" + midi.getControl(msg) + ":" + midi.getValue(msg));
};
)";

TEST_CASE("Script reload clears enables and decoder state", "[MidiKit][MidiProcessor]") {
	MidiKitModule* m = createModule();
	m->loadScript(JS_RELOAD_A);
	REQUIRE(m->host.getActiveEngine() != nullptr);
	drainLog(m);   // discard load chatter

	// Arm an NRPN parameter (select only).
	feedMidiPump(m, {makeCc(0, 99, 4), makeCc(0, 98, 5)});
	REQUIRE(m->midiProcessor.ccNrpnParam[0] == 517);
	REQUIRE(m->isNrpnEnabled(0, false));

	// Reload with a script that defines the callbacks but does not enable.
	m->loadScript(JS_RELOAD_B);
	drainLog(m);   // discard reload chatter

	// The enable belongs to the outgoing script, and the decoder stream is
	// discontinuous — both are cleared.
	REQUIRE(m->midiProcessor.ccNrpnParam[0] == -1);
	REQUIRE(!m->isNrpnEnabled(0, false));

	// Data entry after the reload is neither captured (no armed parameter) nor
	// consumed (no enable): the raw CCs reach onMessage, and onNrpn never fires.
	feedMidiPump(m, {makeCc(0, 6, 20), makeCc(0, 38, 2)});
	auto probes = extractProbes(drainLog(m));
	REQUIRE(probes == std::vector<std::string>({"onMessage:6", "onMessage:38"}));

	Test::destroyModule(m);
}

TEST_CASE("onReset clears decoder state and enables", "[MidiKit][MidiProcessor]") {
	MidiKitModule* m = createModule();
	m->loadScript(JS_RELOAD_A);
	REQUIRE(m->host.getActiveEngine() != nullptr);

	// Arm an NRPN parameter.
	feedMidiPump(m, {makeCc(0, 99, 4), makeCc(0, 98, 5)});
	REQUIRE(m->midiProcessor.ccNrpnParam[0] == 517);
	REQUIRE(m->isNrpnEnabled(0, false));

	m->onReset();

	// Same invariants as a script reload: no half-read stream state, no enables.
	REQUIRE(m->midiProcessor.ccNrpnParam[0] == -1);
	REQUIRE(!m->isNrpnEnabled(0, false));
	REQUIRE(!m->isCc14bitEnabled(0, 7));

	// onReset() tears the engine down, so reload one to observe the behaviour:
	// a parameter armed before the reset does not capture data entry after it,
	// and nothing is consumed — the raw CCs reach onMessage.
	m->loadScript(JS_RELOAD_B);
	REQUIRE(m->host.getActiveEngine() != nullptr);
	drainLog(m);   // discard reload chatter
	feedMidiPump(m, {makeCc(0, 6, 20), makeCc(0, 38, 2)});
	auto probes = extractProbes(drainLog(m));
	REQUIRE(probes == std::vector<std::string>({"onMessage:6", "onMessage:38"}));

	Test::destroyModule(m);
}



// Validation helpers: minimal scripts for the enable-binding checks.
static const char* JS_ENABLE_CC14_ALL = R"(/**
 * @engine QuickJs@v1
 */
midi.enableCc14bitIn(1);
)";

static const char* LUA_ENABLE_CC14_ALL = R"(--[[
@engine minilua@v1
--]]
midi.enableCc14bitIn(1)
)";

TEST_CASE("enableCc14bitIn without cc enables all 32 MSBs on every channel", "[MidiKit][MidiProcessor]") {
	EngineVariant v = GENERATE(engineVariants(JS_ENABLE_CC14_ALL, LUA_ENABLE_CC14_ALL));
	CATCH_INFO("engine: " << v.engine);
	MidiKitModule* m = createModule();
	m->loadScript(v.script);
	REQUIRE(m->host.getActiveEngine() != nullptr);

	for (int ch = 0; ch < 16; ch++) {
		for (int cc = 0; cc < 32; cc++) {
			REQUIRE(m->isCc14bitEnabled(ch, cc));
		}
	}
	// Out of the valid 0-31 MSB range there is nothing to enable.
	REQUIRE(!m->isCc14bitEnabled(0, 32));

	Test::destroyModule(m);
}



static const char* JS_ENABLE_CC14_ONE = R"(/**
 * @engine QuickJs@v1
 */
midi.enableCc14bitIn(1, 7, 3);
)";

static const char* LUA_ENABLE_CC14_ONE = R"(--[[
@engine minilua@v1
--]]
midi.enableCc14bitIn(1, 7, 3)
)";

static const char* JS_ENABLE_NRPN_CH = R"(/**
 * @engine QuickJs@v1
 */
midi.enableNrpnIn(1, 3);
)";

static const char* LUA_ENABLE_NRPN_CH = R"(--[[
@engine minilua@v1
--]]
midi.enableNrpnIn(1, 3)
)";

TEST_CASE("enableCc14bitIn honours a per-channel argument", "[MidiKit][MidiProcessor]") {
	// midi.enableCc14bitIn(1, 7, 3) → channel 3 (0-based 2), MSB 7 only. The
	// 1-based → 0-based channel conversion is duplicated in each engine's
	// enableCc14bitIn binding, so a Lua-side slip must not pass unnoticed.
	EngineVariant v = GENERATE(engineVariants(JS_ENABLE_CC14_ONE, LUA_ENABLE_CC14_ONE));
	CATCH_INFO("engine: " << v.engine);
	MidiKitModule* m = createModule();
	m->loadScript(v.script);
	REQUIRE(m->host.getActiveEngine() != nullptr);
	REQUIRE(m->isCc14bitEnabled(2, 7));
	REQUIRE(!m->isCc14bitEnabled(0, 7));
	REQUIRE(!m->isCc14bitEnabled(2, 8));
	Test::destroyModule(m);
}

TEST_CASE("enableNrpnIn honours a per-channel argument", "[MidiKit][MidiProcessor]") {
	// midi.enableNrpnIn(1, 3) → channel 3 (0-based 2) only.
	EngineVariant v = GENERATE(engineVariants(JS_ENABLE_NRPN_CH, LUA_ENABLE_NRPN_CH));
	CATCH_INFO("engine: " << v.engine);
	MidiKitModule* m = createModule();
	m->loadScript(v.script);
	REQUIRE(m->host.getActiveEngine() != nullptr);
	REQUIRE(m->isNrpnEnabled(2, false));
	REQUIRE(!m->isNrpnEnabled(0, false));
	Test::destroyModule(m);
}



// RPN per-channel and bad-port scripts. RPN is the only enable binding with no
// direct coverage of its own argument handling — in particular the `kind`
// argument, which if wired backwards would arm the NRPN mask instead of the
// RPN one.
static const char* JS_ENABLE_RPN_CH = R"(/**
 * @engine QuickJs@v1
 */
midi.enableRpnIn(1, 3);
)";

static const char* LUA_ENABLE_RPN_CH = R"(--[[
@engine minilua@v1
--]]
midi.enableRpnIn(1, 3)
)";

TEST_CASE("enableRpnIn honours a per-channel argument and arms the RPN mask", "[MidiKit][MidiProcessor]") {
	// The one assertion that pins the `kind` wiring: RPN must arm
	// rpnEnabledMask (isNrpnEnabled(ch, true)) and NOT nrpnEnabledMask
	// (isNrpnEnabled(ch, false)). Both engines — each has its own binding
	// passing the kind argument.
	EngineVariant v = GENERATE(engineVariants(JS_ENABLE_RPN_CH, LUA_ENABLE_RPN_CH));
	CATCH_INFO("engine: " << v.engine);
	MidiKitModule* m = createModule();
	m->loadScript(v.script);
	REQUIRE(m->host.getActiveEngine() != nullptr);

	// midi.enableRpnIn(1, 3) → channel 3 (0-based 2), RPN mask only.
	REQUIRE(m->isNrpnEnabled(2, true));    // RPN mask bit set
	REQUIRE(!m->isNrpnEnabled(2, false));  // NRPN mask bit NOT set
	REQUIRE(!m->isNrpnEnabled(0, true));   // other channels untouched

	Test::destroyModule(m);
}



static const char* JS_ENABLE_CC14_BAD = R"(/**
 * @engine QuickJs@v1
 */
midi.enableCc14bitIn(1, 99);
)";

static const char* LUA_ENABLE_CC14_BAD = R"(--[[
@engine minilua@v1
--]]
midi.enableCc14bitIn(1, 99)
)";

TEST_CASE("enableCc14bitIn rejects cc outside 0..31", "[MidiKit][MidiProcessor]") {
	EngineVariant v = GENERATE(engineVariants(JS_ENABLE_CC14_BAD, LUA_ENABLE_CC14_BAD));
	assertLoadRejected(v.script, "cc must be 0-31");
}



static const char* JS_ENABLE_RPN_BADPORT = R"(/**
 * @engine QuickJs@v1
 */
midi.enableRpnIn(0);
)";

static const char* LUA_ENABLE_RPN_BADPORT = R"(--[[
@engine minilua@v1
--]]
midi.enableRpnIn(0)
)";

TEST_CASE("enableRpnIn rejects a bad midiPort", "[MidiKit][MidiProcessor]") {
	// The two engines word the error differently ("midiPort out of range" vs
	// "bad midiPort"), so assert the common token rather than the full text.
	EngineVariant v = GENERATE(engineVariants(JS_ENABLE_RPN_BADPORT, LUA_ENABLE_RPN_BADPORT));
	assertLoadRejected(v.script, "midiPort");
}