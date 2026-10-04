// ─── NRPN / RPN / 14-bit CC input — cross-engine suite ─────────────────────
//
// Covers the module-level assembly through MidiProcessor, the consumption rules in
// processMidi(), and the script-facing API that exposes them
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
// has fired and its log entries are queued. The kit's frame counter keeps
// running, so consecutive calls never replay a frame.
static void feedMidiPump(Kit<>& kit, const std::vector<midi::Message>& msgs) {
	for (const auto& msg : msgs) kit.inject(msg);
	kit.pumpDivider();
}

// Loads `script`, feeds `in`, and returns the runtime probe lines ("P:" lines of
// the log, prefix stripped). Fails the test loudly if the script did not load or
// the load reported an error.
static std::vector<std::string> probesOf(const std::string& script, const std::vector<midi::Message>& in) {
	CATCH_INFO(script);
	Kit<> kit;
	std::string loadLog = kit.loadRaw(script);
	CATCH_INFO("load log:\n" << loadLog);
	REQUIRE(loadLog.find("rror") == std::string::npos);
	REQUIRE(kit.m->host.getActiveEngine() != nullptr);
	feedMidiPump(kit, in);
	return kit.probes();
}

// Asserts one engine's script produced exactly `expected` probe lines (empty
// asserts silence). The cross-engine tests call it once per FOR_EACH_LANG pass.
static void assertProbes(const std::string& script, const std::vector<midi::Message>& in,
                         const std::vector<std::string>& expected) {
	std::vector<std::string> probes = probesOf(script, in);
	CATCH_INFO("probes:\n" << [&]() { std::string s; for (auto& p : probes) s += "  " + p + "\n"; return s; }());
	REQUIRE(probes == expected);
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
	Kit<> kit;
	MidiKitModule* m = kit.m;
	m->loadScript(script);
	std::string log = drainLog(m);
	CATCH_INFO("log:\n" << log);
	REQUIRE(log.find(token) != std::string::npos);
	if (script.find("@engine QuickJs@v1") != std::string::npos)
		REQUIRE(m->host.seQuickJs.ctx == nullptr);
	else
		REQUIRE(m->host.seLua.L == nullptr);
}

TEST_CASE("enableNrpnIn rejects a bad midiPort", "[MidiKit][MidiProcessor]") {
	// Port 0 is below the 1-based range, on the shared NRPN/RPN binding path
	// (enableRpnIn is covered separately). Both engines.
	FOR_EACH_LANG;
	std::string script = enableScript(lang == Lang::Js, "midi.enableNrpnIn(0)");
	assertLoadRejected(script, "midiPort");
}

TEST_CASE("enableCc14bitIn rejects a bad midiPort", "[MidiKit][MidiProcessor]") {
	FOR_EACH_LANG;
	std::string script = enableScript(lang == Lang::Js, "midi.enableCc14bitIn(0)");
	assertLoadRejected(script, "midiPort");
}

TEST_CASE("enableNrpnIn and enableCc14bitIn reject a channel outside 1-16", "[MidiKit][MidiProcessor]") {
	// Channel is the boundary that matters most: an off-by-one (ch >= 16)
	// would silently accept 17 and shift past the 16-bit mask. Both ends of
	// the range are pinned, for both bindings, in both engines. The four calls
	// are each iterated over both engines by the combined generator.
	std::string call = GENERATE(std::string("midi.enableNrpnIn(1, 0)"),
	                            std::string("midi.enableNrpnIn(1, 17)"),
	                            std::string("midi.enableCc14bitIn(1, 7, 0)"),
	                            std::string("midi.enableCc14bitIn(1, 7, 17)"));
	FOR_EACH_LANG;
	std::string script = enableScript(lang == Lang::Js, call);
	assertLoadRejected(script, "channel");
}

TEST_CASE("enableNrpnIn accepts channel 16 and arms bit 15", "[MidiKit][MidiProcessor]") {
	// Channel 16 (1-based) is the last valid channel and must arm bit 15
	// (0-based) of the mask — a `ch >= 16` off-by-one would reject it, and a
	// missing upper bound would let it shift past the mask entirely. Both
	// engines.
	FOR_EACH_LANG;
	std::string script = enableScript(lang == Lang::Js, "midi.enableNrpnIn(1, 16)");
	Kit<> kit;
	MidiKitModule* m = kit.m;
	m->loadScript(script);
	REQUIRE(m->host.getActiveEngine() != nullptr);
	REQUIRE(m->midiIns.isNrpnEnabled(15, false));
	REQUIRE(!m->midiIns.isNrpnEnabled(0, false));
}

TEST_CASE("enableCc14bitIn accepts channel 16 and arms bit 15", "[MidiKit][MidiProcessor]") {
	FOR_EACH_LANG;
	std::string script = enableScript(lang == Lang::Js, "midi.enableCc14bitIn(1, 7, 16)");
	Kit<> kit;
	MidiKitModule* m = kit.m;
	m->loadScript(script);
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
	Pair scripts{JS_FLAGS, LUA_FLAGS};
	FOR_EACH_LANG;
	assertProbes(scripts.get(lang), nrpnQuad(0, 4, 5, 20, 2), {"flags:true:false:false"});
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
	Pair scripts{JS_STALE, LUA_STALE};
	FOR_EACH_LANG;
	assertProbes(scripts.get(lang), nrpnQuadThenCc(), {"onNrpn:517:2562", "msg:false:false:false:64:7"});
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
	Pair scripts{JS_THREE, LUA_THREE};
	FOR_EACH_LANG;
	assertProbes(scripts.get(lang), nrpnQuad(0, 4, 5, 20, 2), {"three:517:2562:99"});
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
	Pair scripts{JS_CTRL, LUA_CTRL};
	FOR_EACH_LANG;
	assertProbes(scripts.get(lang), {makeCc(0, 7, 64), makeNote(0, 60, 100), makePitchBend(0, 1, 0), makeClock()},
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
	Pair scripts{JS_VAL, LUA_VAL};
	FOR_EACH_LANG;
	assertProbes(scripts.get(lang), {makeCc(0, 7, 64), makeNote(0, 60, 100)}, {"val:64:7", "val:100:60"});
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
	// MIDI, as the API documents.
	Pair scripts{JS_NOCB, LUA_NOCB};
	FOR_EACH_LANG;
	assertProbes(scripts.get(lang), nrpnQuad(0, 4, 5, 20, 2), {});
}

TEST_CASE("Component CCs are withheld while enabled and pass when not", "[MidiKit][MidiProcessor][CrossEngine]") {
	SECTION("Enabled: the four component CCs never reach onMessage") {
		Pair scripts{JS_NOCB, LUA_NOCB};
		FOR_EACH_LANG;
		assertProbes(scripts.get(lang), nrpnQuad(0, 4, 5, 20, 2), {});
	}
	SECTION("Not enabled: onMessage sees every component CC raw") {
		Pair scripts{JS_PASS, LUA_PASS};
		FOR_EACH_LANG;
		assertProbes(scripts.get(lang), nrpnQuad(0, 4, 5, 20, 2),
			{"onMessage:99", "onMessage:98", "onMessage:6", "onMessage:38"});
	}
}



// Blanket 14-bit + NRPN: the accepted overlap: CC 6/38
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

TEST_CASE("Blanket 14-bit + NRPN consumes CC 6/38 and fires both events", "[MidiKit][MidiProcessor][CrossEngine]") {
	// With both enabled, CC 6/38 are consumed by
	// the 14-bit rule (never raw) and data entry fires BOTH the NRPN and the
	// 14-bit event. Pins the accepted overlap so it is not later "fixed" into
	// NRPN precedence, which would suppress the onCc14bit.
	Pair scripts{JS_BOTH, LUA_BOTH};
	FOR_EACH_LANG;
	assertProbes(scripts.get(lang), nrpnQuad(0, 4, 5, 20, 2),
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
	Pair scripts{JS_ESCAPE, LUA_ESCAPE};
	FOR_EACH_LANG;
	assertProbes(scripts.get(lang), nrpnQuad(0, 4, 5, 20, 2),
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
	Pair scripts{JS_NRPN, LUA_NRPN};
	FOR_EACH_LANG;
	assertProbes(scripts.get(lang),
		{makeCc(0, 99, 1), makeCc(1, 99, 2), makeCc(0, 98, 3), makeCc(1, 98, 4),
		 makeCc(0, 6, 10), makeCc(0, 38, 5), makeCc(1, 6, 7), makeCc(1, 38, 2)},
		{"onNrpn:131:1285:1:99", "onNrpn:260:898:2:99"});
}

TEST_CASE("Parameter select alone fires nothing", "[MidiKit][MidiProcessor][CrossEngine]") {
	// The select CCs are consumed AND their assembled select event is dropped
	// by hasValue() gating — nothing reaches the script.
	Pair scripts{JS_NRPN, LUA_NRPN};
	FOR_EACH_LANG;
	assertProbes(scripts.get(lang), {makeCc(0, 99, 4), makeCc(0, 98, 5)}, {});
}

TEST_CASE("NRPN quad assembles into one onNrpn with the decoded handle, which only data entry fires", "[MidiKit][MidiProcessor][CrossEngine]") {
	// param 4*128+5 = 517, value 20*128+2 = 2562, channel 1, completing CC 38.
	Pair scripts{JS_NRPN, LUA_NRPN};
	FOR_EACH_LANG;
	assertProbes(scripts.get(lang), nrpnQuad(0, 4, 5, 20, 2), {"onNrpn:517:2562:1:99"});
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
	Pair scripts{JS_RPNNRPN, LUA_RPNNRPN};
	FOR_EACH_LANG;
	assertProbes(scripts.get(lang),
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
	Pair scripts{JS_CC14, LUA_CC14};
	FOR_EACH_LANG;
	assertProbes(scripts.get(lang), {makeCc(0, 7, 1), makeCc(0, 7, 0), makeCc(0, 39, 0)},
		{"onMessage:7", "onCc14bit:7:0:1:7"});
}

TEST_CASE("Only-14-bit script still receives raw CC 98", "[MidiKit][MidiProcessor][CrossEngine]") {
	// isComponent alone must not drive consumption: CC 98 is a component of an
	// NRPN, but with only 14-bit enabled the matching enable is unset, so it
	// reaches onMessage raw. The NRPN select never reaches onNrpn (not enabled).
	Pair scripts{JS_CC14, LUA_CC14};
	FOR_EACH_LANG;
	assertProbes(scripts.get(lang), {makeCc(0, 99, 4), makeCc(0, 98, 5)}, {"onMessage:99", "onMessage:98"});
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
	Kit<> kit;
	MidiKitModule* m = kit.m;
	m->loadScript(JS_RELOAD_A);
	REQUIRE(m->host.getActiveEngine() != nullptr);
	drainLog(m);   // discard load chatter

	// Arm an NRPN parameter (select only).
	feedMidiPump(kit, {makeCc(0, 99, 4), makeCc(0, 98, 5)});
	REQUIRE(m->midiIns.ports[0].processor.ccNrpnParam[0] == 517);
	REQUIRE(m->midiIns.isNrpnEnabled(0, false));

	// Reload with a script that defines the callbacks but does not enable.
	m->loadScript(JS_RELOAD_B);
	drainLog(m);   // discard reload chatter
	feedMidiPump(kit, {});   // the audio thread carries out the decoder reset

	// The enable belongs to the outgoing script, and the decoder stream is
	// discontinuous — both are cleared.
	REQUIRE(m->midiIns.ports[0].processor.ccNrpnParam[0] == -1);
	REQUIRE(!m->midiIns.isNrpnEnabled(0, false));

	// Data entry after the reload is neither captured (no armed parameter) nor
	// consumed (no enable): the raw CCs reach onMessage, and onNrpn never fires.
	feedMidiPump(kit, {makeCc(0, 6, 20), makeCc(0, 38, 2)});
	auto probes = kit.probes();
	REQUIRE(probes == std::vector<std::string>({"onMessage:6", "onMessage:38"}));

}

TEST_CASE("onReset clears decoder state and enables", "[MidiKit][MidiProcessor]") {
	Kit<> kit;
	MidiKitModule* m = kit.m;
	m->loadScript(JS_RELOAD_A);
	REQUIRE(m->host.getActiveEngine() != nullptr);

	// Arm an NRPN parameter.
	feedMidiPump(kit, {makeCc(0, 99, 4), makeCc(0, 98, 5)});
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
	feedMidiPump(kit, {makeCc(0, 6, 20), makeCc(0, 38, 2)});
	auto probes = kit.probes();
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
	Pair scripts{JS_ENABLE_CC14_ALL, LUA_ENABLE_CC14_ALL};
	FOR_EACH_LANG;
	Kit<> kit;
	MidiKitModule* m = kit.m;
	m->loadScript(scripts.get(lang));
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
	// midi.enableCc14bitIn(1, 7, 3) → channel 3 (0-based 2), MSB 7 only. The
	// 1-based → 0-based channel conversion is duplicated in each engine's
	// enableCc14bitIn binding, so a Lua-side slip must not pass unnoticed.
	Pair scripts{JS_ENABLE_CC14_ONE, LUA_ENABLE_CC14_ONE};
	FOR_EACH_LANG;
	Kit<> kit;
	MidiKitModule* m = kit.m;
	m->loadScript(scripts.get(lang));
	REQUIRE(m->host.getActiveEngine() != nullptr);
	REQUIRE(m->midiIns.isCc14bitEnabled(2, 7));
	REQUIRE(!m->midiIns.isCc14bitEnabled(0, 7));
	REQUIRE(!m->midiIns.isCc14bitEnabled(2, 8));
}

TEST_CASE("enableNrpnIn honours a per-channel argument", "[MidiKit][MidiProcessor]") {
	// midi.enableNrpnIn(1, 3) → channel 3 (0-based 2) only.
	Pair scripts{JS_ENABLE_NRPN_CH, LUA_ENABLE_NRPN_CH};
	FOR_EACH_LANG;
	Kit<> kit;
	MidiKitModule* m = kit.m;
	m->loadScript(scripts.get(lang));
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
	// The one assertion that pins the `kind` wiring: RPN must arm
	// rpnEnabledMask (isNrpnEnabled(ch, true)) and NOT nrpnEnabledMask
	// (isNrpnEnabled(ch, false)). Both engines — each has its own binding
	// passing the kind argument.
	Pair scripts{JS_ENABLE_RPN_CH, LUA_ENABLE_RPN_CH};
	FOR_EACH_LANG;
	Kit<> kit;
	MidiKitModule* m = kit.m;
	m->loadScript(scripts.get(lang));
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
	Pair scripts{JS_ENABLE_CC14_BAD, LUA_ENABLE_CC14_BAD};
	FOR_EACH_LANG;
	assertLoadRejected(scripts.get(lang), "cc must be 0-31");
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
	Pair scripts{JS_ENABLE_RPN_BADPORT, LUA_ENABLE_RPN_BADPORT};
	FOR_EACH_LANG;
	assertLoadRejected(scripts.get(lang), "midiPort");
}

// ─── Received groups are group handles ──────────────────────────────────────
// A received NRPN/RPN/14-bit CC has the same shape as a created and set one:
// 4 or 2 slots, the lead's bytes, and the decode fields every getter answers from.

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

TEST_CASE("Received group: every accessor matches a created and set group", "[MidiKit][MidiProcessor][CrossEngine]") {
	FOR_EACH_LANG;
	bool lua = lang == Lang::Lua;
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

TEST_CASE("Received group: getNote and getControl answer the lead and the number", "[MidiKit][MidiProcessor][CrossEngine]") {
	struct Case { size_t kind; const char* expected; };
	// isNrpn:isRpn:isCc14bit : control : value : channel : note
	std::vector<Case> cases = {
		{ 0, "R:true:false:false:517:2562:1:99" },
		{ 1, "R:false:true:false:517:2562:1:101" },
		{ 2, "R:false:false:true:7:130:1:7" },
	};
	auto kinds = groupKinds();
	FOR_EACH_LANG;
	bool lua = lang == Lang::Lua;
	for (const Case& c : cases) {
		CATCH_INFO(std::string(lua ? "lua " : "js ") + kinds[c.kind].name);
		auto p = probesOf(kindScript(lua, kinds[c.kind], describe(lua, "R", "msg")), kinds[c.kind].in);
		REQUIRE(p == std::vector<std::string>({ c.expected }));
	}
}

TEST_CASE("Received group: setValue sets the combined value, other setters raise", "[MidiKit][MidiProcessor][CrossEngine]") {
	auto kinds = groupKinds();
	const char* tails[] = { "message is an NRPN; use midi.setNRPN()", "message is an RPN; use midi.setRPN()", "message is a 14-bit CC; use midi.setCc14bit()" };
	FOR_EACH_LANG;
	bool lua = lang == Lang::Lua;
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

TEST_CASE("Received group: the store holds 4 (NRPN/RPN) or 2 (14-bit CC) slots of the default 32", "[MidiKit][MidiProcessor][CrossEngine]") {
	auto kinds = groupKinds();
	FOR_EACH_LANG;
	bool lua = lang == Lang::Lua;
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
	Pair scripts{js, lua};
	FOR_EACH_LANG;
	assertProbes(scripts.get(lang), in, {"g:true:517:2562", "m:false:7:false:0", "g:true:130:388"});
}


// ─── Decoder input that is not a complete quad or pair ──────────────────────
// What real devices send: 7-bit
// NRPN data entry, plain 7-bit controllers 0-31 next to a 14-bit enable,
// running parameters, MSB-only 14-bit updates. Each case states what a receiver
// does with it.

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
	FOR_EACH_LANG;
	bool lua = lang == Lang::Lua;
	CATCH_INFO(std::string(lua ? "lua" : "js"));
	auto p = probesOf(gapScript(lua, enable, hook, tag, args), in);
	REQUIRE(p == expected);
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
	FOR_EACH_LANG;
	bool lua = lang == Lang::Lua;
	CATCH_INFO(std::string(lua ? "lua" : "js"));
	auto in = nrpnQuad(0, 0, 5, 64, 0);
	in.push_back(makeCc(0, 6, 70));
	auto p = probesOf(gapScript(lua, "enableNrpnIn", "onNrpn", "n", "1, null, \"msb\""), in);
	// The quad fires twice (coarse, then combined), the CC 6 = 70 change once.
	REQUIRE(p == std::vector<std::string>({ "n:5:8192", "n:5:8192", "n:5:8960" }));
}

TEST_CASE("Decoder: a running parameter (data entry without a new select) fires again", "[MidiKit][MidiProcessor]") {
	auto in = nrpnQuad(0, 0, 5, 64, 0);
	in.push_back(makeCc(0, 6, 65));
	in.push_back(makeCc(0, 38, 1));
	checkGap("enableNrpnIn", "onNrpn", "n", in, { "n:5:8192", "n:5:8321" });
}

TEST_CASE("Decoder: data increment and decrement are not part of the assembled message", "[MidiKit][MidiProcessor]") {
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
static MidiKitModule* loadEnable(Kit<>& kit, bool lua, const std::string& call) {
	kit.m->loadScript(enableScript(!lua, enableCall(lua, call)));
	return kit.m;
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
	FOR_EACH_LANG;
	bool lua = lang == Lang::Lua;
	for (const Case& c : cases) {
		CATCH_INFO(std::string(lua ? "lua: " : "js: ") + c.call);
		Kit<> kit;
		MidiKitModule* m = loadEnable(kit, lua, c.call);
		std::string log = drainLog(m);
		CATCH_INFO("log:\n" << log);
		REQUIRE(log.find(c.token) != std::string::npos);
		REQUIRE(!m->midiIns.isNrpnEnabled(0, false));
		REQUIRE(!m->midiIns.isNrpnEnabled(0, true));
		REQUIRE(nrpnMsbMask(m) == 0);
		REQUIRE(rpnMsbMask(m) == 0);
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
	FOR_EACH_LANG;
	bool lua = lang == Lang::Lua;
	for (const Case& c : cases) {
		CATCH_INFO(std::string(lua ? "lua: " : "js: ") + c.call);
		Kit<> kit;
		MidiKitModule* m = loadEnable(kit, lua, c.call);
		REQUIRE(drainLog(m).find("rror") == std::string::npos);
		// The kind is enabled on every channel the call named, whatever its mode.
		REQUIRE(m->midiIns.isNrpnEnabled(2, c.rpn));
		REQUIRE((c.rpn ? rpnMsbMask(m) : nrpnMsbMask(m)) == c.mask);
		REQUIRE((c.rpn ? nrpnMsbMask(m) : rpnMsbMask(m)) == 0);
	}
}

TEST_CASE("enableNrpnIn: the last call for a channel decides its data entry mode", "[MidiKit][MidiProcessor]") {
	FOR_EACH_LANG;
	bool lua = lang == Lang::Lua;
	CATCH_INFO(std::string(lua ? "lua" : "js"));
	// All channels "msb", then channel 5 again without a mode: back to "lsb".
	Kit<> kit;
	MidiKitModule* m = loadEnable(kit, lua, "midi.enableNrpnIn(1, null, \"msb\"); midi.enableNrpnIn(1, 5)");
	REQUIRE(drainLog(m).find("rror") == std::string::npos);
	REQUIRE(nrpnMsbMask(m) == uint16_t(0xFFFF & ~(1 << 4)));
	// Enabling stayed additive: every channel is still enabled.
	for (int ch = 0; ch < 16; ch++) REQUIRE(m->midiIns.isNrpnEnabled(ch, false));
}

TEST_CASE("enableNrpnIn: a reload brings the data entry mode back to the default", "[MidiKit][MidiProcessor]") {
	FOR_EACH_LANG;
	bool lua = lang == Lang::Lua;
	CATCH_INFO(std::string(lua ? "lua" : "js"));
	Kit<> kit;
	MidiKitModule* m = loadEnable(kit, lua, "midi.enableNrpnIn(1, null, \"msb\"); midi.enableRpnIn(1, null, \"msb\")");
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
		Kit<> kit;
		MidiKitModule* m = kit.m;
		m->loadScript(isLua ? lua : js);
		REQUIRE(drainLog(m).find("rror") == std::string::npos);

		// Default mode: CC 6 alone fires nothing.
		feedMidiPump(kit, { makeCc(0, 99, 0), makeCc(0, 98, 5), makeCc(0, 6, 64) });
		REQUIRE(kit.probes().empty());
		REQUIRE(nrpnMsbMask(m) == 0);

		feedMidiPump(kit, { makeNote(0, 60, 100) });
		drainLog(m);
		REQUIRE(nrpnMsbMask(m) == 0xFFFF);

		feedMidiPump(kit, { makeCc(0, 6, 65) });
		REQUIRE(kit.probes() == std::vector<std::string>({ "n:5:8320" }));
	}
}

// MidiProcessor integration
// The module decodes the incoming stream through MidiProcessor before it
// reaches the engine, so NRPN/RPN/14-bit CC assembly happens once on the audio
// thread rather than in every script.
// Feeds raw MIDI into the module's real input queue and runs process() enough
// times to clear the divider (8), so the queue is actually pumped.
static void feedMidi(MidiKitModule* m, std::vector<midi::Message> msgs, int64_t& frame) {
	for (auto& msg : msgs) m->midiIns.ports[0].processor.getInput().onMessage(msg);
	for (int i = 0; i < 9; i++) m->process(Test::makeProcessArgs(frame++));
}

static midi::Message cc(uint8_t ch, uint8_t num, uint8_t value) {
	return Test::makeMidiMessage(0xb, ch, num, value);
}

TEST_CASE("Incoming MIDI is decoded before reaching the engine", "[MidiKit][MidiProcessor]") {
	Kit<> kit;
	MidiKitModule* m = kit.m;
	AttachedEngine<RecordingEngine> attached(m);
	RecordingEngine& eng = attached.eng;
	int64_t frame = 1;

	SECTION("A plain CC arrives as an undecoded message") {
		feedMidi(m, { cc(0, 7, 64) }, frame);

		REQUIRE(eng.received.size() == 1);
		REQUIRE(eng.received[0].type == StoermelderPackOne::MessageEx::Type::CC);
		REQUIRE(eng.received[0].msg.getNote() == 7);
		REQUIRE(eng.received[0].msg.getValue() == 64);
		// Not part of an extended message, so no decode result rides along.
		REQUIRE(eng.received[0].isComponent == false);
		REQUIRE(eng.received[0].paramNumber == -1);
		REQUIRE(eng.received[0].extraValue == -1);
	}

	SECTION("A note is passed through untouched") {
		feedMidi(m, { Test::makeMidiMessage(0x9, 0, 60, 100) }, frame);

		REQUIRE(eng.received.size() == 1);
		REQUIRE(eng.received[0].type == StoermelderPackOne::MessageEx::Type::NOTE_ON);
		REQUIRE(eng.received[0].msg.getNote() == 60);
	}

	SECTION("NRPN components are flagged, and the assembled event is not queued") {
		// Four CCs make one NRPN. Every one of them still reaches the engine as a
		// raw CC -- this integration must not change what onMessage sees -- but
		// each is marked as belonging to an extended message.
		feedMidi(m, { cc(0, 99, 4), cc(0, 98, 5), cc(0, 6, 20), cc(0, 38, 2) }, frame);

		// Exactly four: the assembled NRPN events are dropped rather than queued,
		// so onMessage still fires once per real MIDI message.
		REQUIRE(eng.received.size() == 4);
		for (auto& q : eng.received) {
			CATCH_INFO("cc=" << int(q.msg.getNote()));
			REQUIRE(q.type == StoermelderPackOne::MessageEx::Type::CC);
			REQUIRE(q.isComponent == true);
		}
	}

	SECTION("A 14-bit CC pair is flagged from the second message on") {
		// The decoder cannot know a pair is coming, so the first MSB escapes
		// unflagged; the LSB completes a tracked pair and is flagged.
		feedMidi(m, { cc(0, 5, 3), cc(0, 32 + 5, 10) }, frame);

		REQUIRE(eng.received.size() == 2);
		REQUIRE(eng.received[0].isComponent == false);
		REQUIRE(eng.received[1].isComponent == true);
	}
}

TEST_CASE("Decoder state is cleared on reset and script load", "[MidiKit][MidiProcessor]") {
	// Kit has the synchronous worker: the test depends on the load having landed.
	Kit<> kit;
	MidiKitModule* m = kit.m;
	AttachedEngine<RecordingEngine> attached(m);
	int64_t frame = 1;

	// Arm an NRPN parameter, leaving the decoder mid-sequence.
	feedMidi(m, { cc(0, 99, 4), cc(0, 98, 5) }, frame);
	REQUIRE(m->midiIns.ports[0].processor.ccNrpnParam[0] == (4 * 128 + 5));

	SECTION("onReset() drops it") {
		m->onReset();
		REQUIRE(m->midiIns.ports[0].processor.ccNrpnParam[0] == -1);
	}

	SECTION("loadScript() drops it, so a new script inherits no half-read state") {
		// Detach the recorder first: loadScript() closes the outgoing engine, and
		// the real Lua engine it installs must be the one teardown sees. Leaving a
		// stack-allocated RecordingEngine as activeEngine across the switch calls
		// virtuals on it during module destruction, after it has gone out of scope.
		m->host.getActiveEngine() = nullptr;
		m->loadScript(LUA_SCRIPT);
		// The reset is a request the audio thread carries out on its next sample.
		m->process(Test::makeProcessArgs(frame++));
		REQUIRE(m->midiIns.ports[0].processor.ccNrpnParam[0] == -1);
	}
}

TEST_CASE("The port's processor owns the queue it pumps", "[MidiKit][MidiProcessor]") {
	Kit<> kit;
	// The queue lives in the processor: no second queue sits beside it, and
	// getInput() (JSON, widget binding, test injection) is the one it pumps.
	MidiKitModule* m = kit.m;

	REQUIRE(m->midiIns.ports[0].processor.ownedInput != nullptr);
	REQUIRE(&m->midiIns.ports[0].processor.getInput() == m->midiIns.ports[0].processor.ownedInput.get());
}


// ── Received NRPN / RPN / 14-bit CC as group handles ───────────────────────
// A received group is rebuilt from its decoded number and value, so what a
// script forwards, reschedules or cancels is the whole group on the wire.

struct GroupInput {
	const char* enable;   // the enable call for the input kind
	const char* hook;     // the callback it reaches
	std::vector<int> in;  // controller, value pairs, flattened, fed on one channel
	std::vector<int> out; // the controllers a forward must produce
};

static std::string groupScript(bool lua, const GroupInput& g, const std::string& body) {
	if (lua) {
		return std::string("--[[\n@engine minilua@v1\n--]]\n") + "midi." + g.enable + "(1)\nmidi." + g.hook + " = function(port, msg)\n" + body + "\nend\n";
	}
	return std::string("/**\n * @engine QuickJs@v1\n */\nmidi.") + g.enable + "(1);\nmidi." + g.hook + " = function(port, msg) {\n" + body + "\n};\n";
}

static void injectGroup(TimingRig& rig, const GroupInput& g, int channel, int64_t frame) {
	for (size_t i = 0; i + 1 < g.in.size(); i += 2) rig.inject(Test::makeMidiMessage(0xb, channel, g.in[i], g.in[i + 1]), frame);
}

static std::vector<GroupInput> groupInputs() {
	return {
		// NRPN 517 (4 * 128 + 5), value 2562 (20 * 128 + 2)
		{ "enableNrpnIn", "onNrpn", { 99, 4, 98, 5, 6, 20, 38, 2 }, { 99, 98, 6, 38 } },
		{ "enableRpnIn", "onRpn", { 101, 4, 100, 5, 6, 20, 38, 2 }, { 101, 100, 6, 38 } },
		// 14-bit CC on MSB controller 7, value 130
		{ "enableCc14bitIn", "onCc14bit", { 7, 1, 39, 2 }, { 7, 39 } },
	};
}

static std::vector<int> sentValues(const TimingRecorder& rec) {
	std::vector<int> v;
	for (const Out& s : rec.sent) v.push_back(s.value);
	return v;
}

TEST_CASE("Received group: midiOut.send forwards the whole group, in wire order", "[MidiKit][Cc][Timing]") {
	FOR_EACH_LANG;
	bool lua = lang == Lang::Lua;
	for (const GroupInput& g : groupInputs()) {
		CATCH_INFO(std::string(lua ? "lua " : "js ") + g.hook);
		TimingRig rig(groupScript(lua, g, lua ? "    midiOut.send(msg)" : "    midiOut.send(msg);").c_str());
		injectGroup(rig, g, 2, 8);
		rig.run(100);

		REQUIRE(sentNotes(rig.rec) == g.out);
		std::vector<int> values;
		for (size_t i = 1; i < g.in.size(); i += 2) values.push_back(g.in[i]);
		REQUIRE(sentValues(rig.rec) == values);
		for (const Out& s : rig.rec.sent) {
			REQUIRE(s.status == 0xb);
			REQUIRE(s.channel == 2);
		}
	}
}

TEST_CASE("Received group: an LSB-only data entry forwards with CC 6 = 0", "[MidiKit][Cc][Timing]") {
	GroupInput g = { "enableNrpnIn", "onNrpn", { 99, 4, 98, 5, 38, 7 }, {} };
	FOR_EACH_LANG;
	bool lua = lang == Lang::Lua;
	CATCH_INFO(std::string(lua ? "lua" : "js"));
	TimingRig rig(groupScript(lua, g, lua ? "    midiOut.send(msg)" : "    midiOut.send(msg);").c_str());
	injectGroup(rig, g, 0, 8);
	rig.run(100);

	REQUIRE(sentNotes(rig.rec) == std::vector<int>({ 99, 98, 6, 38 }));
	REQUIRE(sentValues(rig.rec) == std::vector<int>({ 4, 5, 0, 7 }));
}

TEST_CASE("Received group: setChannel moves every message of the group", "[MidiKit][Cc][Timing]") {
	FOR_EACH_LANG;
	bool lua = lang == Lang::Lua;
	for (const GroupInput& g : groupInputs()) {
		CATCH_INFO(std::string(lua ? "lua " : "js ") + g.hook);
		TimingRig rig(groupScript(lua, g, lua ? "    midi.setChannel(msg, 5); midiOut.send(msg)" : "    midi.setChannel(msg, 5); midiOut.send(msg);").c_str());
		injectGroup(rig, g, 0, 8);
		rig.run(100);

		REQUIRE(sentNotes(rig.rec) == g.out);
		for (const Out& s : rig.rec.sent) REQUIRE(s.channel == 4);
	}
}

TEST_CASE("Received group: midiOut.cancel(msg) takes the scheduled group with that number", "[MidiKit][Cancel][Cc][Timing]") {
	// A note schedules NRPN 300 on channel 1 (a created group); the received NRPN
	// then cancels by its own number: 300 removes it, 301 does not.
	static const char* js = R"(/**
 * @engine QuickJs@v1
 */
midi.enableNrpnIn(1);
midi.onMessage = function(port, msg) {
    let n = midi.createNRPN();
    midi.setNRPN(n, 1, 300, 1000);
    midiOut.sendAfterMs(n, 10);
};
midi.onNrpn = function(port, msg) { midiOut.cancel(msg); };
)";
	static const char* lua = R"(--[[
@engine minilua@v1
--]]
midi.enableNrpnIn(1)
midi.onMessage = function(port, msg)
    local n = midi.createNRPN()
    midi.setNRPN(n, 1, 300, 1000)
    midiOut.sendAfterMs(n, 10)
end
midi.onNrpn = function(port, msg) midiOut.cancel(msg) end
)";
	struct Case { int numberLsb; std::vector<int> controllers; };
	// 300 = 2 * 128 + 44
	std::vector<Case> cases = { { 44, { } }, { 45, { 99, 98, 6, 38 } } };
	for (bool isLua : { false, true }) {
		for (const Case& c : cases) {
			CATCH_INFO(std::string(isLua ? "lua" : "js") + " number LSB " + std::to_string(c.numberLsb));
			TimingRig rig(isLua ? lua : js);
			rig.inject(noteOn(0, 60, 100), 8);
			rig.inject(Test::makeMidiMessage(0xb, 0, 99, 2), 16);
			rig.inject(Test::makeMidiMessage(0xb, 0, 98, c.numberLsb), 16);
			rig.inject(Test::makeMidiMessage(0xb, 0, 6, 1), 16);
			rig.inject(Test::makeMidiMessage(0xb, 0, 38, 1), 16);
			rig.run(cancelRunUntil());

			REQUIRE(sentNotes(rig.rec) == c.controllers);
		}
	}
}


// ── Round trip: what a script creates, a second MIDI-KIT receives ───────────
// Received and created groups have the same shape; this feeds a
// created group's wire bytes into a receiving module's decoder, so the two ends
// have to agree on the numbers. An empty `expected` means the receiver fires nothing.

struct RoundTrip {
	const char* name;
	const char* create;   // JS statements creating `h`; also valid Lua apart from `let`
	const char* enable;   // the receiver's enable call
	const char* hook;
	std::string expected; // control:value the receiver logs
};

static std::vector<std::string> roundTripProbes(const std::string& sendScript, const RoundTrip& rt, bool lua) {
	TimingRig sender(sendScript.c_str());
	sender.inject(noteOn(0, 60, 100), 8);
	sender.run(100);
	REQUIRE_FALSE(sender.rec.sent.empty());

	std::string recv = lua
		? std::string("--[[\n@engine minilua@v1\n--]]\nmidi.") + rt.enable + "(1)\nmidi." + rt.hook + " = function(port, msg) rack.log('P:' .. midi.getControl(msg) .. ':' .. midi.getValue(msg)) end\n"
		: std::string("/**\n * @engine QuickJs@v1\n */\nmidi.") + rt.enable + "(1);\nmidi." + rt.hook + " = function(port, msg) { rack.log('P:' + midi.getControl(msg) + ':' + midi.getValue(msg)); };\n";
	TimingRig receiver(recv.c_str());
	for (const Out& s : sender.rec.sent) {
		receiver.inject(Test::makeMidiMessage(s.status, s.channel, s.note, s.value), 8);
	}
	receiver.run(100);

	std::vector<std::string> probes;
	std::string log = drainLog(receiver.m);
	size_t pos = 0;
	while (pos < log.size()) {
		size_t nl = log.find('\n', pos);
		if (nl == std::string::npos) break;
		if (log.compare(pos, 2, "P:") == 0) probes.push_back(log.substr(pos + 2, nl - pos - 2));
		pos = nl + 1;
	}
	return probes;
}

TEST_CASE("Round trip: a created group is received as the same number and value", "[MidiKit][MidiProcessor][Timing]") {
	std::vector<RoundTrip> cases = {
		{ "NRPN", "const h = midi.createNRPN(); midi.setNRPN(h, 2, 300, 1000);", "enableNrpnIn", "onNrpn", "300:1000" },
		{ "NRPN value 0", "const h = midi.createNRPN(); midi.setNRPN(h, 2, 300, 0);", "enableNrpnIn", "onNrpn", "300:0" },
		{ "NRPN max", "const h = midi.createNRPN(); midi.setNRPN(h, 2, 16383, 16383);", "enableNrpnIn", "onNrpn", "16383:16383" },
		{ "RPN", "const h = midi.createRPN(); midi.setRPN(h, 2, 300, 1000);", "enableRpnIn", "onRpn", "300:1000" },
		// The RPN null is the spec's "no parameter": only the select pair is sent and a
		// receiver fires nothing. NRPN 16383 above is an ordinary parameter.
		{ "RPN 16383 (the null)", "const h = midi.createRPN(); midi.setRPN(h, 2, 16383, 1000);", "enableRpnIn", "onRpn", "" },
		{ "14-bit", "const h = midi.createCc14bit(); midi.setCc14bit(h, 2, 7, 1000);", "enableCc14bitIn", "onCc14bit", "7:1000" },
		{ "14-bit below 128", "const h = midi.createCc14bit(); midi.setCc14bit(h, 2, 7, 100);", "enableCc14bitIn", "onCc14bit", "7:100" },
		{ "14-bit zero", "const h = midi.createCc14bit(); midi.setCc14bit(h, 2, 7, 0);", "enableCc14bitIn", "onCc14bit", "7:0" },
	};
	for (const RoundTrip& rt : cases) {
		for (bool lua : { false, true }) {
			CATCH_INFO(std::string(rt.name) + (lua ? " lua" : " js"));
			std::string create = rt.create;
			if (lua) {
				// The same statements in Lua.
				size_t at;
				while ((at = create.find("const ")) != std::string::npos) create.replace(at, 6, "local ");
				while ((at = create.find(";")) != std::string::npos) create.replace(at, 1, "");
			}
			std::string script = lua
				? std::string("--[[\n@engine minilua@v1\n--]]\nmidi.onMessage = function(port, msg)\n" + create + "\nmidiOut.send(h)\nend\n")
				: std::string("/**\n * @engine QuickJs@v1\n */\nmidi.onMessage = function(port, msg) {\n" + create + "\nmidiOut.send(h);\n};\n");
			std::vector<std::string> expected;
			if (!rt.expected.empty()) expected.push_back(rt.expected);
			REQUIRE(roundTripProbes(script, rt, lua) == expected);
		}
	}
}


TEST_CASE("Received group: forwarding an MSB-only change sends the whole group with CC 38 = 0", "[MidiKit][Cc][Timing]") {
	// A 7-bit device (99, 98, 6) in msb mode. The forward carries what B.2 rebuilds:
	// a complete group, with the missing LSB as 0.
	FOR_EACH_LANG;
	bool lua = lang == Lang::Lua;
	for (const char* hook : { "onNrpn", "onRpn" }) {
		bool rpn = std::string(hook) == "onRpn";
		CATCH_INFO(std::string(lua ? "lua " : "js ") + hook);
		std::string enable = std::string(rpn ? "midi.enableRpnIn(1, null, \"msb\")" : "midi.enableNrpnIn(1, null, \"msb\")");
		std::string script;
		if (lua) {
			size_t at = enable.find("null");
			enable.replace(at, 4, "nil");
			script = "--[[\n@engine minilua@v1\n--]]\n" + enable + "\nmidi." + hook + " = function(port, msg) midiOut.send(msg) end\n";
		}
		else {
			script = "/**\n * @engine QuickJs@v1\n */\n" + enable + ";\nmidi." + hook + " = function(port, msg) { midiOut.send(msg); };\n";
		}
		TimingRig rig(script.c_str());
		int sel = rpn ? 101 : 99;
		rig.inject(Test::makeMidiMessage(0xb, 0, sel, 0), 8);
		rig.inject(Test::makeMidiMessage(0xb, 0, sel - 1, 5), 8);
		rig.inject(Test::makeMidiMessage(0xb, 0, 6, 64), 8);
		rig.run(100);

		REQUIRE(sentNotes(rig.rec) == std::vector<int>({ sel, sel - 1, 6, 38 }));
		REQUIRE(sentValues(rig.rec) == std::vector<int>({ 0, 5, 64, 0 }));
	}
}


TEST_CASE("Received group: cancel by a received handle needs the same channel as the scheduled group", "[MidiKit][Cancel][Cc][Timing]") {
	// groupOf() reads the channel from the handle's bytes and the number from its
	// decode fields; both have to agree between a received and a created group.
	// A created NRPN 300 is scheduled on `created` (1-based). A received NRPN 300
	// or 301 arrives on channel `received` (0-based) and cancels by its handle.
	std::string js = R"(/**
 * @engine QuickJs@v1
 */
midi.enableNrpnIn(1);
midi.onMessage = function(port, msg) {
    let n = midi.createNRPN();
    midi.setNRPN(n, CREATED, 300, 1000);
    midiOut.sendAfterMs(n, 10);
};
midi.onNrpn = function(port, msg) { midiOut.cancel(msg); };
)";
	std::string lua = R"(--[[
@engine minilua@v1
--]]
midi.enableNrpnIn(1)
midi.onMessage = function(port, msg)
    local n = midi.createNRPN()
    midi.setNRPN(n, CREATED, 300, 1000)
    midiOut.sendAfterMs(n, 10)
end
midi.onNrpn = function(port, msg) midiOut.cancel(msg) end
)";
	struct Case { int created; int received; int numberLsb; bool cancelled; };
	// 300 = 2 * 128 + 44
	std::vector<Case> cases = {
		{ 1, 0, 44, true },    // same channel and number
		{ 1, 1, 44, false },   // same number on another channel: not cancelled
		{ 2, 0, 44, false },   // created on channel 2, received on channel 1
		{ 2, 1, 44, true },    // channel 2 on both sides
		{ 16, 15, 44, true },  // the highest channel
		{ 1, 15, 44, false },
		{ 2, 1, 45, false },   // same channel, other number
	};
	for (bool isLua : { false, true }) {
		for (const Case& c : cases) {
			CATCH_INFO(std::string(isLua ? "lua" : "js") + " created " + std::to_string(c.created) + " received " + std::to_string(c.received) + " lsb " + std::to_string(c.numberLsb));
			std::string script = isLua ? lua : js;
			size_t at = script.find("CREATED");
			script.replace(at, 7, std::to_string(c.created));
			TimingRig rig(script.c_str());
			rig.inject(noteOn(0, 60, 100), 8);
			rig.inject(Test::makeMidiMessage(0xb, c.received, 99, 2), 16);
			rig.inject(Test::makeMidiMessage(0xb, c.received, 98, c.numberLsb), 16);
			rig.inject(Test::makeMidiMessage(0xb, c.received, 6, 1), 16);
			rig.inject(Test::makeMidiMessage(0xb, c.received, 38, 1), 16);
			rig.run(cancelRunUntil());

			REQUIRE(sentNotes(rig.rec) == (c.cancelled ? std::vector<int>{} : std::vector<int>({ 99, 98, 6, 38 })));
			for (const Out& sent : rig.rec.sent) REQUIRE(sent.channel == c.created - 1);
		}
	}
}


TEST_CASE("Received group: a running parameter is forwarded with the select the device never sent", "[MidiKit][Cc][Timing]") {
	// The device selects NRPN 517 once, then sends data entry only (running parameter).
	// Every received change is a complete group, so each forward carries 99 and 98.
	FOR_EACH_LANG;
	bool lua = lang == Lang::Lua;
	for (bool rpn : { false, true }) {
		CATCH_INFO(std::string(lua ? "lua " : "js ") + (rpn ? "rpn" : "nrpn"));
		GroupInput g = rpn ? GroupInput{ "enableRpnIn", "onRpn", {}, {} } : GroupInput{ "enableNrpnIn", "onNrpn", {}, {} };
		TimingRig rig(groupScript(lua, g, lua ? "    midiOut.send(msg)" : "    midiOut.send(msg);").c_str());
		int sel = rpn ? 101 : 99;
		auto cc = [&](int num, int value) { rig.inject(Test::makeMidiMessage(0xb, 0, num, value), 8); };
		cc(sel, 4); cc(sel - 1, 5); cc(6, 20); cc(38, 2);   // a full write
		cc(6, 21); cc(38, 3);                              // only the data entry
		rig.run(100);

		REQUIRE(sentNotes(rig.rec) == std::vector<int>({ sel, sel - 1, 6, 38, sel, sel - 1, 6, 38 }));
		REQUIRE(sentValues(rig.rec) == std::vector<int>({ 4, 5, 20, 2, 4, 5, 21, 3 }));
	}
}


// A received SysEx longer than a script can create (8192 payload bytes) is
// dropped whole at the input, with a notice in the log: a script that forwards or
// clones what it receives can never exceed the cap that setSysEx enforces.
static midi::Message sysExOf(size_t totalBytes) {
	midi::Message m;
	m.setSize(int(totalBytes));
	m.bytes[0] = 0xf0;
	for (size_t i = 1; i + 1 < totalBytes; i++) m.bytes[i] = uint8_t(i % 128);
	m.bytes[totalBytes - 1] = 0xf7;
	return m;
}

TEST_CASE("A received SysEx up to the payload cap is forwarded; a longer one is dropped with a notice", "[MidiKit][MidiProcessor][CrossEngine]") {
	FOR_EACH_LANG;
	const size_t maxTotal = size_t(StoermelderPackOne::MidiScript::MidiScriptEngine::sysExMaxPayloadLength) + 2;
	REQUIRE(maxTotal == 8194);

	Kit<> kit;
	kit.load(onMessage(lang, "midiOut.send(msg);"));

	std::vector<Out> sent = kit.dispatchPumped(sysExOf(maxTotal));
	REQUIRE(sent.size() == 1);
	REQUIRE(sent[0].bytes.size() == maxTotal);
	REQUIRE(kit.log().find("longer than") == std::string::npos);

	sent = kit.dispatchPumped(sysExOf(maxTotal + 1));
	REQUIRE(sent.empty());
	REQUIRE(kit.log().find("longer than") != std::string::npos);

	// The input keeps working afterwards.
	sent = kit.dispatchPumped(sysExOf(100));
	REQUIRE(sent.size() == 1);
}
