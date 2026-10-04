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

// Harness

// Feeds raw MIDI into the module's real input queue and pumps process() past
// the divider (8), so the queue is actually decoded and dispatched. Under
// SyncTaskWorker the dispatch runs inline, so when this returns every callback
// has fired and its log entries are queued.
static void feedMidiPump(MidiKitModule* m, const std::vector<midi::Message>& msgs) {
	int64_t frame = 1;
	for (const auto& msg : msgs) m->midiIns.ports[0].processor.getInput().onMessage(msg);
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

// Enable-binding validation helpers

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
	ModuleScaffold mods;
	// Channel 16 (1-based) is the last valid channel and must arm bit 15
	// (0-based) of the mask — a `ch >= 16` off-by-one would reject it, and a
	// missing upper bound would let it shift past the mask entirely. Both
	// engines.
	EngineVariant v = GENERATE(engineVariants("midi.enableNrpnIn(1, 16)"));
	CATCH_INFO("engine: " << v.engine);
	MidiKitModule* m = mods.create();
	m->loadScript(v.script);
	REQUIRE(m->host.getActiveEngine() != nullptr);
	REQUIRE(m->midiIns.isNrpnEnabled(15, false));
	REQUIRE(!m->midiIns.isNrpnEnabled(0, false));
}

TEST_CASE("enableCc14bitIn accepts channel 16 and arms bit 15", "[MidiKit][MidiProcessor]") {
	ModuleScaffold mods;
	EngineVariant v = GENERATE(engineVariants("midi.enableCc14bitIn(1, 7, 16)"));
	CATCH_INFO("engine: " << v.engine);
	MidiKitModule* m = mods.create();
	m->loadScript(v.script);
	REQUIRE(m->host.getActiveEngine() != nullptr);
	REQUIRE(m->midiIns.isCc14bitEnabled(15, 7));
	REQUIRE(!m->midiIns.isCc14bitEnabled(0, 7));
}


// Tests


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



// getControl/getValue/getNote return three
// different things for the same assembled message (getNote: the lead CC).
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
	// Parameter 517, combined 14-bit value 2562; getNote is the group's lead (CC 99).
	EngineVariant v = GENERATE(engineVariants(JS_THREE, LUA_THREE));
	assertProbes(v, nrpnQuad(0, 4, 5, 20, 2), {"three:517:2562:99"});
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
		{"onNrpn:517:2562:1:99", "onCc14bit:6:2562:1:6"});
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
		{"onNrpn:131:1285:1:99", "onNrpn:260:898:2:99"});
}

TEST_CASE("Parameter select alone fires nothing; following data entry does", "[MidiKit][MidiProcessor][CrossEngine]") {
	// The select CCs are consumed AND their assembled select event is dropped
	// by hasValue() gating — nothing reaches the script.
	EngineVariant v = GENERATE(engineVariants(JS_NRPN, LUA_NRPN));
	assertProbes(v, {makeCc(0, 99, 4), makeCc(0, 98, 5)}, {});
	// Data entry on an armed parameter assembles the one change.
	assertProbes(v, nrpnQuad(0, 4, 5, 20, 2), {"onNrpn:517:2562:1:99"});
}

TEST_CASE("NRPN quad assembles into one onNrpn with the decoded handle", "[MidiKit][MidiProcessor][CrossEngine]") {
	// param 4*128+5 = 517, value 20*128+2 = 2562, channel 1, completing CC 38.
	EngineVariant v = GENERATE(engineVariants(JS_NRPN, LUA_NRPN));
	assertProbes(v, nrpnQuad(0, 4, 5, 20, 2), {"onNrpn:517:2562:1:99"});
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
		{"onRpn:130:2562:1:101", "onNrpn:388:1287:1:99"});
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
		{"onMessage:7", "onCc14bit:7:0:1:7"});
}

TEST_CASE("Only-14-bit script still receives raw CC 98", "[MidiKit][MidiProcessor][CrossEngine]") {
	// isComponent alone must not drive consumption: CC 98 is a component of an
	// NRPN, but with only 14-bit enabled the matching enable is unset, so it
	// reaches onMessage raw. The NRPN select never reaches onNrpn (not enabled).
	EngineVariant v = GENERATE(engineVariants(JS_CC14, LUA_CC14));
	assertProbes(v, {makeCc(0, 99, 4), makeCc(0, 98, 5)}, {"onMessage:99", "onMessage:98"});
}

TEST_CASE("MSB of 0 on a never-seen CC still assembles with its LSB", "[MidiKit][MidiProcessor][CrossEngine]") {
	// A 14-bit value below 128 is MSB 0 plus an LSB. Like any first MSB it escapes
	// raw (the decoder cannot know a pair is coming); the LSB completes the event.
	EngineVariant v = GENERATE(engineVariants(JS_CC14, LUA_CC14));
	assertProbes(v, {makeCc(0, 7, 0), makeCc(0, 39, 42)}, {"onMessage:7", "onCc14bit:7:42:1:7"});
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
	ModuleScaffold mods;
	MidiKitModule* m = mods.create();
	m->loadScript(JS_RELOAD_A);
	REQUIRE(m->host.getActiveEngine() != nullptr);
	drainLog(m);   // discard load chatter

	// Arm an NRPN parameter (select only).
	feedMidiPump(m, {makeCc(0, 99, 4), makeCc(0, 98, 5)});
	REQUIRE(m->midiIns.ports[0].processor.ccNrpnParam[0] == 517);
	REQUIRE(m->midiIns.isNrpnEnabled(0, false));

	// Reload with a script that defines the callbacks but does not enable.
	m->loadScript(JS_RELOAD_B);
	drainLog(m);   // discard reload chatter
	feedMidiPump(m, {});   // the audio thread carries out the decoder reset

	// The enable belongs to the outgoing script, and the decoder stream is
	// discontinuous — both are cleared.
	REQUIRE(m->midiIns.ports[0].processor.ccNrpnParam[0] == -1);
	REQUIRE(!m->midiIns.isNrpnEnabled(0, false));

	// Data entry after the reload is neither captured (no armed parameter) nor
	// consumed (no enable): the raw CCs reach onMessage, and onNrpn never fires.
	feedMidiPump(m, {makeCc(0, 6, 20), makeCc(0, 38, 2)});
	auto probes = extractProbes(drainLog(m));
	REQUIRE(probes == std::vector<std::string>({"onMessage:6", "onMessage:38"}));

}

TEST_CASE("onReset clears decoder state and enables", "[MidiKit][MidiProcessor]") {
	ModuleScaffold mods;
	MidiKitModule* m = mods.create();
	m->loadScript(JS_RELOAD_A);
	REQUIRE(m->host.getActiveEngine() != nullptr);

	// Arm an NRPN parameter.
	feedMidiPump(m, {makeCc(0, 99, 4), makeCc(0, 98, 5)});
	REQUIRE(m->midiIns.ports[0].processor.ccNrpnParam[0] == 517);
	REQUIRE(m->midiIns.isNrpnEnabled(0, false));

	m->onReset();

	// Same invariants as a script reload: no half-read stream state, no enables.
	REQUIRE(m->midiIns.ports[0].processor.ccNrpnParam[0] == -1);
	REQUIRE(!m->midiIns.isNrpnEnabled(0, false));
	REQUIRE(!m->midiIns.isCc14bitEnabled(0, 7));

	// onReset() tears the engine down, so reload one to observe the behaviour:
	// a parameter armed before the reset does not capture data entry after it,
	// and nothing is consumed — the raw CCs reach onMessage.
	m->loadScript(JS_RELOAD_B);
	REQUIRE(m->host.getActiveEngine() != nullptr);
	drainLog(m);   // discard reload chatter
	feedMidiPump(m, {makeCc(0, 6, 20), makeCc(0, 38, 2)});
	auto probes = extractProbes(drainLog(m));
	REQUIRE(probes == std::vector<std::string>({"onMessage:6", "onMessage:38"}));

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
	ModuleScaffold mods;
	EngineVariant v = GENERATE(engineVariants(JS_ENABLE_CC14_ALL, LUA_ENABLE_CC14_ALL));
	CATCH_INFO("engine: " << v.engine);
	MidiKitModule* m = mods.create();
	m->loadScript(v.script);
	REQUIRE(m->host.getActiveEngine() != nullptr);

	for (int ch = 0; ch < 16; ch++) {
		for (int cc = 0; cc < 32; cc++) {
			REQUIRE(m->midiIns.isCc14bitEnabled(ch, cc));
		}
	}
	// Out of the valid 0-31 MSB range there is nothing to enable.
	REQUIRE(!m->midiIns.isCc14bitEnabled(0, 32));

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
	ModuleScaffold mods;
	// midi.enableCc14bitIn(1, 7, 3) → channel 3 (0-based 2), MSB 7 only. The
	// 1-based → 0-based channel conversion is duplicated in each engine's
	// enableCc14bitIn binding, so a Lua-side slip must not pass unnoticed.
	EngineVariant v = GENERATE(engineVariants(JS_ENABLE_CC14_ONE, LUA_ENABLE_CC14_ONE));
	CATCH_INFO("engine: " << v.engine);
	MidiKitModule* m = mods.create();
	m->loadScript(v.script);
	REQUIRE(m->host.getActiveEngine() != nullptr);
	REQUIRE(m->midiIns.isCc14bitEnabled(2, 7));
	REQUIRE(!m->midiIns.isCc14bitEnabled(0, 7));
	REQUIRE(!m->midiIns.isCc14bitEnabled(2, 8));
}

TEST_CASE("enableNrpnIn honours a per-channel argument", "[MidiKit][MidiProcessor]") {
	ModuleScaffold mods;
	// midi.enableNrpnIn(1, 3) → channel 3 (0-based 2) only.
	EngineVariant v = GENERATE(engineVariants(JS_ENABLE_NRPN_CH, LUA_ENABLE_NRPN_CH));
	CATCH_INFO("engine: " << v.engine);
	MidiKitModule* m = mods.create();
	m->loadScript(v.script);
	REQUIRE(m->host.getActiveEngine() != nullptr);
	REQUIRE(m->midiIns.isNrpnEnabled(2, false));
	REQUIRE(!m->midiIns.isNrpnEnabled(0, false));
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
	ModuleScaffold mods;
	// The one assertion that pins the `kind` wiring: RPN must arm
	// rpnEnabledMask (isNrpnEnabled(ch, true)) and NOT nrpnEnabledMask
	// (isNrpnEnabled(ch, false)). Both engines — each has its own binding
	// passing the kind argument.
	EngineVariant v = GENERATE(engineVariants(JS_ENABLE_RPN_CH, LUA_ENABLE_RPN_CH));
	CATCH_INFO("engine: " << v.engine);
	MidiKitModule* m = mods.create();
	m->loadScript(v.script);
	REQUIRE(m->host.getActiveEngine() != nullptr);

	// midi.enableRpnIn(1, 3) → channel 3 (0-based 2), RPN mask only.
	REQUIRE(m->midiIns.isNrpnEnabled(2, true));    // RPN mask bit set
	REQUIRE(!m->midiIns.isNrpnEnabled(2, false));  // NRPN mask bit NOT set
	REQUIRE(!m->midiIns.isNrpnEnabled(0, true));   // other channels untouched

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

// ─── Received groups are group handles ──────────────────────────────────────
// A received NRPN/RPN/14-bit CC has the same shape as a created and set one
// (Addendum B of var/MidiKit_cancel_scheduled_draft.md): 4 or 2 slots, the lead's
// bytes, and the decode fields every getter answers from.

struct GroupKind {
	const char* name;
	const char* enable;
	const char* hook;
	const char* create;     // constructor
	std::string set;        // setter call on `m`, given the received handle `msg`
	std::vector<midi::Message> in;
	int slots;
};

static std::vector<GroupKind> groupKinds() {
	return {
		{ "NRPN", "enableNrpnIn", "onNrpn", "createNRPN", "setNRPN", nrpnQuad(0, 4, 5, 20, 2), 4 },
		{ "RPN", "enableRpnIn", "onRpn", "createRPN", "setRPN", rpnQuad(0, 4, 5, 20, 2), 4 },
		{ "CC14", "enableCc14bitIn", "onCc14bit", "createCc14bit", "setCc14bit", { makeCc(0, 7, 1), makeCc(0, 39, 2) }, 2 },
	};
}

// A script for `kind` whose hook runs `body` (JS or Lua) with `msg` the received handle.
static std::string kindScript(bool lua, const GroupKind& k, const std::string& body) {
	if (lua) return std::string("--[[\n@engine minilua@v1\n--]]\nmidi.") + k.enable + "(1)\nmidi." + k.hook + " = function(port, msg)\n" + body + "\nend\n";
	return std::string("/**\n * @engine QuickJs@v1\n */\nmidi.") + k.enable + "(1);\nmidi." + k.hook + " = function(port, msg) {\n" + body + "\n};\n";
}

// Logs every group accessor of handle `h` under `tag`.
static std::string describe(bool lua, const char* tag, const char* h) {
	std::string t = tag, m = h;
	if (lua) {
		return "rack.log('P:" + t + ":' .. tostring(midi.isNrpn(" + m + ")) .. ':' .. tostring(midi.isRpn(" + m + ")) .. ':' .. tostring(midi.isCc14bit(" + m + ")) .. ':' .. midi.getControl(" + m + ") .. ':' .. midi.getValue(" + m + ") .. ':' .. midi.getChannel(" + m + ") .. ':' .. midi.getNote(" + m + "))";
	}
	return "rack.log('P:" + t + ":' + midi.isNrpn(" + m + ") + ':' + midi.isRpn(" + m + ") + ':' + midi.isCc14bit(" + m + ") + ':' + midi.getControl(" + m + ") + ':' + midi.getValue(" + m + ") + ':' + midi.getChannel(" + m + ") + ':' + midi.getNote(" + m + "));";
}

// Runs `script` over `in` and returns its probe lines.
static std::vector<std::string> probesOf(const std::string& script, const std::vector<midi::Message>& in) {
	CATCH_INFO(script);
	NrpnResult r = runIn(script, in);
	return r.probes;
}

TEST_CASE("Received group: every accessor matches a created and set group", "[MidiKit][MidiProcessor][CrossEngine]") {
	for (bool lua : { false, true }) {
		for (const GroupKind& k : groupKinds()) {
			CATCH_INFO(std::string(lua ? "lua " : "js ") + k.name);
			// The same channel, number and value, read back through the getters.
			std::string make = std::string("local n = midi.") + k.create + "(); midi." + k.set + "(n, midi.getChannel(msg), midi.getControl(msg), midi.getValue(msg))";
			if (!lua) make = std::string("let n = midi.") + k.create + "(); midi." + k.set + "(n, midi.getChannel(msg), midi.getControl(msg), midi.getValue(msg));";
			std::string body = describe(lua, "R", "msg") + "\n" + make + "\n" + describe(lua, "C", "n");
			auto p = probesOf(kindScript(lua, k, body), k.in);
			REQUIRE(p.size() == 2);
			REQUIRE(p[0].substr(1) == p[1].substr(1));
			// And it really is the lead: CC 99 / 101, or the MSB controller.
			REQUIRE(p[0].find("true") != std::string::npos);
		}
	}
}

TEST_CASE("Received group: getNote and getControl answer the lead and the number", "[MidiKit][MidiProcessor][CrossEngine]") {
	struct Case { size_t kind; const char* expected; };
	// isNrpn:isRpn:isCc14bit : control : value : channel : note
	std::vector<Case> cases = {
		{ 0, "R:true:false:false:517:2562:1:99" },
		{ 1, "R:false:true:false:517:2562:1:101" },
		{ 2, "R:false:false:true:7:130:1:7" },
	};
	auto kinds = groupKinds();
	for (bool lua : { false, true }) {
		for (const Case& c : cases) {
			CATCH_INFO(std::string(lua ? "lua " : "js ") + kinds[c.kind].name);
			auto p = probesOf(kindScript(lua, kinds[c.kind], describe(lua, "R", "msg")), kinds[c.kind].in);
			REQUIRE(p == std::vector<std::string>({ c.expected }));
		}
	}
}

TEST_CASE("Received group: setValue sets the combined value, other setters raise", "[MidiKit][MidiProcessor][CrossEngine]") {
	auto kinds = groupKinds();
	const char* tails[] = { "message is an NRPN; use midi.setNRPN()", "message is an RPN; use midi.setRPN()", "message is a 14-bit CC; use midi.setCc14bit()" };
	for (bool lua : { false, true }) {
		for (size_t i = 0; i < kinds.size(); i++) {
			CATCH_INFO(std::string(lua ? "lua " : "js ") + kinds[i].name);
			std::string body = lua
				? "midi.setValue(msg, 1000)\n" + describe(true, "V", "msg") + "\nlocal ok, err = pcall(midi.setNote, msg, 1)\nrack.log('P:E:' .. tostring(err))\n" + describe(true, "W", "msg")
				: "midi.setValue(msg, 1000);\n" + describe(false, "V", "msg") + "\ntry { midi.setNote(msg, 1); } catch (e) { rack.log('P:E:' + e); }\n" + describe(false, "W", "msg");
			auto p = probesOf(kindScript(lua, kinds[i], body), kinds[i].in);
			REQUIRE(p.size() == 3);
			// setValue took: the value is 1000 and the number is untouched.
			REQUIRE(p[0].find(":1000:1:") != std::string::npos);
			REQUIRE(p[1].find(tails[i]) != std::string::npos);
			// The rejected setter left the handle unchanged.
			REQUIRE(p[0].substr(1) == p[2].substr(1));
		}
	}
}

TEST_CASE("Received group: the store holds 4 (NRPN/RPN) or 2 (14-bit CC) slots of the default 32", "[MidiKit][MidiProcessor][CrossEngine]") {
	auto kinds = groupKinds();
	for (bool lua : { false, true }) {
		for (const GroupKind& k : kinds) {
			CATCH_INFO(std::string(lua ? "lua " : "js ") + k.name);
			std::string body = lua
				? "local ok = 0\nlocal good, err = pcall(function() for i = 1, 40 do midi.create(); ok = ok + 1 end end)\nrack.log('P:ok:' .. ok)\nrack.log('P:err:' .. tostring(err))"
				: "let ok = 0;\ntry { for (let i = 0; i < 40; i++) { midi.create(); ok++; } } catch (e) { rack.log('P:err:' + e); }\nrack.log('P:ok:' + ok);";
			auto p = probesOf(kindScript(lua, k, body), k.in);
			REQUIRE(p.size() == 2);
			std::string ok = "ok:" + std::to_string(32 - k.slots);
			REQUIRE((p[0] == ok || p[1] == ok));
			REQUIRE((p[0].find("message store full") != std::string::npos || p[1].find("message store full") != std::string::npos));
		}
	}
}

TEST_CASE("Received group: a following plain message and group start clean", "[MidiKit][MidiProcessor][CrossEngine]") {
	static const char* js = R"(/**
 * @engine QuickJs@v1
 */
midi.enableNrpnIn(1);
midi.onNrpn = function(port, msg) {
    rack.log("P:g:" + midi.isNrpn(msg) + ":" + midi.getControl(msg) + ":" + midi.getValue(msg));
};
midi.onMessage = function(port, msg) {
    let h = midi.create();
    rack.log("P:m:" + midi.isNrpn(msg) + ":" + midi.getControl(msg) + ":" + midi.isNrpn(h) + ":" + midi.getValue(h));
};
)";
	static const char* lua = R"(--[[
@engine minilua@v1
--]]
midi.enableNrpnIn(1)
midi.onNrpn = function(port, msg)
    rack.log("P:g:" .. tostring(midi.isNrpn(msg)) .. ":" .. midi.getControl(msg) .. ":" .. midi.getValue(msg))
end
midi.onMessage = function(port, msg)
    local h = midi.create()
    rack.log("P:m:" .. tostring(midi.isNrpn(msg)) .. ":" .. midi.getControl(msg) .. ":" .. tostring(midi.isNrpn(h)) .. ":" .. midi.getValue(h))
end
)";
	std::vector<midi::Message> in = nrpnQuad(0, 4, 5, 20, 2);
	in.push_back(makeCc(0, 7, 64));
	for (auto& m : nrpnQuad(0, 1, 2, 3, 4)) in.push_back(m);
	EngineVariant v = GENERATE(engineVariants(js, lua));
	assertProbes(v, in, {"g:true:517:2562", "m:false:7:false:0", "g:true:130:388"});
}


// ─── Decoder input that is not a complete quad or pair ──────────────────────
// What real devices send (var/MidiKit_incoming_vs_created_review.md, T1): 7-bit
// NRPN data entry, plain 7-bit controllers 0-31 next to a 14-bit enable,
// running parameters, MSB-only 14-bit updates. Each case states what a receiver
// does with it. A case that is a known gap, failing on purpose until it is decided,
// carries the [decoder-gap] tag (`./build/test/MidiKit.test "[decoder-gap]"`).

// Enables one kind and logs the raw CCs (m:note:value) and the assembled events
// (n:/r:/c: control:value) a script sees.
// `args` is the enable call's argument list in JS spelling: null becomes nil in Lua.
static std::string gapScript(bool lua, const char* enable, const char* hook, const char* tag, const char* args = "1") {
	std::string t = tag;
	std::string a = args;
	if (lua) {
		size_t at;
		while ((at = a.find("null")) != std::string::npos) a.replace(at, 4, "nil");
	}
	if (lua) {
		return std::string("--[[\n@engine minilua@v1\n--]]\nmidi.") + enable + "(" + a + ")\n"
			"midi.onMessage = function(port, msg) rack.log('P:m:' .. midi.getNote(msg) .. ':' .. midi.getValue(msg)) end\n"
			"midi." + hook + " = function(port, msg) rack.log('P:" + t + ":' .. midi.getControl(msg) .. ':' .. midi.getValue(msg)) end\n";
	}
	return std::string("/**\n * @engine QuickJs@v1\n */\nmidi.") + enable + "(" + a + ");\n"
		"midi.onMessage = function(port, msg) { rack.log('P:m:' + midi.getNote(msg) + ':' + midi.getValue(msg)); };\n"
		"midi." + hook + " = function(port, msg) { rack.log('P:" + t + ":' + midi.getControl(msg) + ':' + midi.getValue(msg)); };\n";
}

static void checkGap(const char* enable, const char* hook, const char* tag, const std::vector<midi::Message>& in, const std::vector<std::string>& expected, const char* args = "1") {
	for (bool lua : { false, true }) {
		CATCH_INFO(std::string(lua ? "lua" : "js"));
		auto p = probesOf(gapScript(lua, enable, hook, tag, args), in);
		REQUIRE(p == expected);
	}
}

TEST_CASE("A 7-bit NRPN data entry (CC 6 alone) fires in msb mode, and repeats", "[MidiKit][MidiProcessor]") {
	// CC 6 = 64 is the value 64 << 7 = 8192; CC 6 = 65 is 8320.
	std::vector<midi::Message> in = { makeCc(0, 99, 0), makeCc(0, 98, 5), makeCc(0, 6, 64), makeCc(0, 6, 65) };
	checkGap("enableNrpnIn", "onNrpn", "n", in, { "n:5:8192", "n:5:8320" }, "1, null, \"msb\"");
	checkGap("enableNrpnIn", "onNrpn", "n", in, { "n:5:8192", "n:5:8320" }, "1, 1, \"msb\"");
	// Default mode: CC 6 only stores the MSB, so nothing fires and nothing reaches onMessage.
	checkGap("enableNrpnIn", "onNrpn", "n", in, {});
	checkGap("enableNrpnIn", "onNrpn", "n", in, {}, "1, null, \"lsb\"");
	// Enabled for channel 2 only: the CCs on channel 1 are not claimed, so they arrive raw.
	checkGap("enableNrpnIn", "onNrpn", "n", in, { "m:99:0", "m:98:5", "m:6:64", "m:6:65" }, "1, 2, \"msb\"");
}

TEST_CASE("A coarse change after a full quad fires in msb mode", "[MidiKit][MidiProcessor]") {
	for (bool lua : { false, true }) {
		CATCH_INFO(std::string(lua ? "lua" : "js"));
		auto in = nrpnQuad(0, 0, 5, 64, 0);
		in.push_back(makeCc(0, 6, 70));
		auto p = probesOf(gapScript(lua, "enableNrpnIn", "onNrpn", "n", "1, null, \"msb\""), in);
		// The quad fires twice (coarse, then combined), the CC 6 = 70 change once.
		REQUIRE(p == std::vector<std::string>({ "n:5:8192", "n:5:8192", "n:5:8960" }));
	}
}

TEST_CASE("Decoder gap: a running parameter (data entry without a new select) fires again", "[MidiKit][MidiProcessor][decoder-gap]") {
	auto in = nrpnQuad(0, 0, 5, 64, 0);
	in.push_back(makeCc(0, 6, 65));
	in.push_back(makeCc(0, 38, 1));
	checkGap("enableNrpnIn", "onNrpn", "n", in, { "n:5:8192", "n:5:8321" });
}

TEST_CASE("Decoder gap: data increment and decrement are not part of the assembled message", "[MidiKit][MidiProcessor][decoder-gap]") {
	// Pins what happens to CC 96/97 after an armed parameter: they are unsupported,
	// so they reach onMessage like any other CC.
	auto in = nrpnQuad(0, 0, 5, 64, 0);
	in.push_back(makeCc(0, 97, 1));
	checkGap("enableNrpnIn", "onNrpn", "n", in, { "n:5:8192", "m:97:1" });
}

TEST_CASE("Enabling every 14-bit CC claims controllers 0-31, a per-CC enable leaves the others alone", "[MidiKit][MidiProcessor]") {
	// Intended: enableCc14bitIn(1) says every MSB 0-31 is a 14-bit controller, so a
	// 7-bit mod wheel on CC 1 is withheld after its first message (the script
	// should register the controllers it means). With enableCc14bitIn(1, 7) only
	// CC 7/39 are claimed.
	std::vector<midi::Message> in = { makeCc(0, 1, 10), makeCc(0, 1, 20), makeCc(0, 1, 30), makeCc(0, 1, 40) };
	checkGap("enableCc14bitIn", "onCc14bit", "c", in, { "m:1:10" });
	checkGap("enableCc14bitIn", "onCc14bit", "c", in, { "m:1:10", "m:1:20", "m:1:30", "m:1:40" }, "1, 7");
}

TEST_CASE("A 14-bit change needs both messages: a new MSB alone fires nothing", "[MidiKit][MidiProcessor]") {
	// Accepted behaviour: an event is produced by the LSB, completing the pair. A new
	// MSB is withheld (it is a component by then) and waits for its LSB, so a device
	// has to send both for every change.
	checkGap("enableCc14bitIn", "onCc14bit", "c", { makeCc(0, 7, 1), makeCc(0, 39, 2), makeCc(0, 7, 3) },
		{ "m:7:1", "c:7:130" });
	// The LSB then completes it with the new MSB.
	checkGap("enableCc14bitIn", "onCc14bit", "c", { makeCc(0, 7, 1), makeCc(0, 39, 2), makeCc(0, 7, 3), makeCc(0, 39, 2) },
		{ "m:7:1", "c:7:130", "c:7:386" });
}

TEST_CASE("A 14-bit value below 128 is decoded (MSB of 0 on a new controller)", "[MidiKit][MidiProcessor]") {
	// What setCc14bit(h, 1, 7, 100) sends: CC 7 = 0, CC 39 = 100. The MSB escapes raw.
	checkGap("enableCc14bitIn", "onCc14bit", "c", { makeCc(0, 7, 0), makeCc(0, 39, 100) }, { "m:7:0", "c:7:100" });
}


// ─── enableNrpnIn / enableRpnIn: the dataEntry argument ─────────────────────

static std::string enableCall(bool lua, const std::string& call) {
	std::string c = call;
	if (lua) {
		size_t at;
		while ((at = c.find("null")) != std::string::npos) c.replace(at, 4, "nil");
		while ((at = c.find("undefined")) != std::string::npos) c.replace(at, 9, "nil");
	}
	return c;
}

// Loads `call` as a script (JS spelling, null/undefined become nil in Lua) and returns the module.
static MidiKitModule* loadEnable(bool lua, const std::string& call) {
	MidiKitModule* m = createModule();
	m->loadScript(enableScript(!lua, enableCall(lua, call)));
	return m;
}

static uint16_t nrpnMsbMask(MidiKitModule* m) { return m->midiIns.ports[0].extendedCc.msbDataEntryNrpnMask.load(); }
static uint16_t rpnMsbMask(MidiKitModule* m) { return m->midiIns.ports[0].extendedCc.msbDataEntryRpnMask.load(); }

TEST_CASE("enableNrpnIn/enableRpnIn: a bad dataEntry is rejected and enables nothing", "[MidiKit][MidiProcessor]") {
	struct Case { const char* call; const char* token; };
	std::vector<Case> cases = {
		{ "midi.enableNrpnIn(1, null, \"foo\")", "dataEntry" },
		{ "midi.enableRpnIn(1, null, \"MSB\")", "dataEntry" },
		{ "midi.enableNrpnIn(1, 1, \"\")", "dataEntry" },
		// Not a string: the engines word it differently, both name the function.
		{ "midi.enableNrpnIn(1, null, true)", "enableNrpnIn" },
		{ "midi.enableRpnIn(1, null, {})", "enableRpnIn" },
		// Too many arguments.
		{ "midi.enableNrpnIn(1, 1, \"msb\", 1)", "bad args" },
	};
	for (bool lua : { false, true }) {
		for (const Case& c : cases) {
			CATCH_INFO(std::string(lua ? "lua: " : "js: ") + c.call);
			MidiKitModule* m = loadEnable(lua, c.call);
			std::string log = drainLog(m);
			CATCH_INFO("log:\n" << log);
			REQUIRE(log.find(c.token) != std::string::npos);
			REQUIRE(!m->midiIns.isNrpnEnabled(0, false));
			REQUIRE(!m->midiIns.isNrpnEnabled(0, true));
			REQUIRE(nrpnMsbMask(m) == 0);
			REQUIRE(rpnMsbMask(m) == 0);
			Test::destroyModule(m);
		}
	}
}

TEST_CASE("enableNrpnIn/enableRpnIn: null, undefined and nil channel mean all channels", "[MidiKit][MidiProcessor]") {
	struct Case { const char* call; bool rpn; uint16_t mask; };
	std::vector<Case> cases = {
		{ "midi.enableNrpnIn(1, null, \"msb\")", false, 0xFFFF },
		{ "midi.enableNrpnIn(1, undefined, \"msb\")", false, 0xFFFF },
		{ "midi.enableRpnIn(1, null, \"msb\")", true, 0xFFFF },
		{ "midi.enableNrpnIn(1, 3, \"msb\")", false, 1 << 2 },
		{ "midi.enableNrpnIn(1, null, \"lsb\")", false, 0 },
		{ "midi.enableNrpnIn(1, null)", false, 0 },
		{ "midi.enableNrpnIn(1)", false, 0 },
	};
	for (bool lua : { false, true }) {
		for (const Case& c : cases) {
			CATCH_INFO(std::string(lua ? "lua: " : "js: ") + c.call);
			MidiKitModule* m = loadEnable(lua, c.call);
			REQUIRE(drainLog(m).find("rror") == std::string::npos);
			// The kind is enabled on every channel the call named, whatever its mode.
			REQUIRE(m->midiIns.isNrpnEnabled(2, c.rpn));
			REQUIRE((c.rpn ? rpnMsbMask(m) : nrpnMsbMask(m)) == c.mask);
			REQUIRE((c.rpn ? nrpnMsbMask(m) : rpnMsbMask(m)) == 0);
			Test::destroyModule(m);
		}
	}
}

TEST_CASE("enableNrpnIn: the last call for a channel decides its data entry mode", "[MidiKit][MidiProcessor]") {
	for (bool lua : { false, true }) {
		CATCH_INFO(std::string(lua ? "lua" : "js"));
		// All channels "msb", then channel 5 again without a mode: back to "lsb".
		MidiKitModule* m = loadEnable(lua, "midi.enableNrpnIn(1, null, \"msb\"); midi.enableNrpnIn(1, 5)");
		REQUIRE(drainLog(m).find("rror") == std::string::npos);
		REQUIRE(nrpnMsbMask(m) == uint16_t(0xFFFF & ~(1 << 4)));
		// Enabling stayed additive: every channel is still enabled.
		for (int ch = 0; ch < 16; ch++) REQUIRE(m->midiIns.isNrpnEnabled(ch, false));
		Test::destroyModule(m);
	}
}

TEST_CASE("enableNrpnIn: a reload brings the data entry mode back to the default", "[MidiKit][MidiProcessor]") {
	for (bool lua : { false, true }) {
		CATCH_INFO(std::string(lua ? "lua" : "js"));
		MidiKitModule* m = loadEnable(lua, "midi.enableNrpnIn(1, null, \"msb\"); midi.enableRpnIn(1, null, \"msb\")");
		REQUIRE(nrpnMsbMask(m) == 0xFFFF);
		REQUIRE(rpnMsbMask(m) == 0xFFFF);

		// Script B enables NRPN without a mode: it gets the default.
		m->loadScript(enableScript(!lua, "midi.enableNrpnIn(1)"));
		REQUIRE(nrpnMsbMask(m) == 0);
		REQUIRE(rpnMsbMask(m) == 0);

		// And a script that does not enable at all leaves nothing behind either.
		m->loadScript(enableScript(!lua, "midi.enableNrpnIn(1, null, \"msb\")"));
		REQUIRE(nrpnMsbMask(m) == 0xFFFF);
		m->loadScript(enableScript(!lua, "rack.log('P:none')"));
		REQUIRE(nrpnMsbMask(m) == 0);
		REQUIRE(rpnMsbMask(m) == 0);
		Test::destroyModule(m);
	}
}

TEST_CASE("enableNrpnIn: switching the mode from a callback applies to the following CC 6", "[MidiKit][MidiProcessor]") {
	// A note is the "menu item": it switches port 1 to msb mode.
	static const char* js = R"(/**
 * @engine QuickJs@v1
 */
midi.enableNrpnIn(1);
midi.onMessage = function(port, msg) {
    if (midi.isNoteOn(msg)) midi.enableNrpnIn(1, null, "msb");
};
midi.onNrpn = function(port, msg) { rack.log("P:n:" + midi.getControl(msg) + ":" + midi.getValue(msg)); };
)";
	static const char* lua = R"(--[[
@engine minilua@v1
--]]
midi.enableNrpnIn(1)
midi.onMessage = function(port, msg)
    if midi.isNoteOn(msg) then midi.enableNrpnIn(1, nil, "msb") end
end
midi.onNrpn = function(port, msg) rack.log("P:n:" .. midi.getControl(msg) .. ":" .. midi.getValue(msg)) end
)";
	for (bool isLua : { false, true }) {
		CATCH_INFO(std::string(isLua ? "lua" : "js"));
		MidiKitModule* m = createModule();
		m->loadScript(isLua ? lua : js);
		REQUIRE(drainLog(m).find("rror") == std::string::npos);

		// Default mode: CC 6 alone fires nothing.
		feedMidiPump(m, { makeCc(0, 99, 0), makeCc(0, 98, 5), makeCc(0, 6, 64) });
		REQUIRE(extractProbes(drainLog(m)).empty());
		REQUIRE(nrpnMsbMask(m) == 0);

		feedMidiPump(m, { makeNote(0, 60, 100) });
		drainLog(m);
		REQUIRE(nrpnMsbMask(m) == 0xFFFF);

		feedMidiPump(m, { makeCc(0, 6, 65) });
		REQUIRE(extractProbes(drainLog(m)) == std::vector<std::string>({ "n:5:8320" }));
		Test::destroyModule(m);
	}
}
