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
		EngineRun r = run(script);
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
rack.log("PROBE:" + number.toString(16777217));
rack.log("PROBE:" + number.toString(123456789012));
rack.log("PROBE:" + number.toString(-16777219));
rack.log("PROBE:" + number.toString(1e20));
)";

static const char* LUA_NUMBER_TOSTRING = R"(--[[
@engine minilua@v1
--]]
rack.log("PROBE:" .. number.toString(42))
rack.log("PROBE:" .. number.toString(3.14))
rack.log("PROBE:" .. number.toString(-100))
rack.log("PROBE:" .. number.toString(1 / 3))
rack.log("PROBE:" .. number.toString(0))
rack.log("PROBE:" .. number.toString(16777217))
rack.log("PROBE:" .. number.toString(123456789012))
rack.log("PROBE:" .. number.toString(-16777219))
rack.log("PROBE:" .. number.toString(1e20))
)";

TEST_CASE("number.toString is identical", "[MidiKit][CrossEngine]") {
	requireLoggedValues(JS_NUMBER_TOSTRING, LUA_NUMBER_TOSTRING, {"42", "3.14", "-100", "0.333333", "0",
		// integers above 2^24 stay exact (a float would round them)
		"16777217", "123456789012", "-16777219", "1e+20"});
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
// Wire bytes of the simple message setters (note, CC, pitch wheel, program change, pressure, SysEx, raw), clone, and the sendAfterTrigger argument forms.
//
// Part of the cross-engine suite: the shared helpers (run, requireEquivalent, EngineRun, ...)
// are in MidiKit.test.hpp.

// setNoteOn / midiOut.send

static const char* JS_NOTE_ON = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let out = midi.create();
    midi.setNoteOn(out, 1, 60, 100);
    midiOut.send(out);
};
)";

static const char* LUA_NOTE_ON = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local out = midi.create()
    midi.setNoteOn(out, 1, 60, 100)
    midiOut.send(out)
end
)";

TEST_CASE("setNoteOn produces identical wire bytes", "[MidiKit][CrossEngine]") {
	requireBytes(runBoth(Pair{JS_NOTE_ON, LUA_NOTE_ON}), {{0x90, 60, 100}});
}


// CC reroute (echoes incoming messages via getters/setters)
// Increments the CC number of every incoming CC message by 1 and forwards
// it. Unlike the setNoteOn/setCc cases above, this exercises getters against
// the incoming message (handle 0) rather than only constructing a fresh one,
// and needs a CC message fed in rather than the default NoteOn.

static const char* JS_CC_REROUTE = R"(/**
 * @engine QuickJs@v1
 * @description CC number +1 passthrough
 */
midi.onMessage = function(port, msg) {
    if (midi.getType(msg) === midi.CC) {
        midi.setNote(msg, midi.getNote(msg) + 1);
        midiOut.send(msg);
    }
};
)";

static const char* LUA_CC_REROUTE = R"(--[[
@engine minilua@v1
@description CC number +1 passthrough
--]]
midi.onMessage = function(port, msg)
    if midi.getType(msg) == midi.CC then
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

	requireBytes(runBoth(Pair{JS_CC_REROUTE, LUA_CC_REROUTE}, cc), {{0xb0, 11, 64}});
}


// setCc

static const char* JS_CC = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let out = midi.create();
    midi.setCc(out, 2, 74, 127);
    midiOut.send(out);
};
)";

static const char* LUA_CC = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local out = midi.create()
    midi.setCc(out, 2, 74, 127)
    midiOut.send(out)
end
)";

TEST_CASE("setCc produces identical wire bytes", "[MidiKit][CrossEngine]") {
	requireBytes(runBoth(Pair{JS_CC, LUA_CC}), {{0xb1, 74, 127}});
}



// The scripts call the API from midi.onMessage: run() rejects load-time errors.
static std::string jsOnMessage(const std::string& body) {
	return "/**\n * @engine QuickJs@v1\n */\nmidi.onMessage = function(port, msg) {\n" + body + "\n};\n";
}
static std::string luaOnMessage(const std::string& body) {
	return "--[[\n@engine minilua@v1\n--]]\nmidi.onMessage = function(port, msg)\n" + body + "\nend\n";
}

TEST_CASE("setRaw caps the message length like setSysEx", "[MidiKit][CrossEngine]") {
	const size_t cap = StoermelderPackOne::MidiScript::MidiScriptEngine::sysExMaxPayloadLength + 2;
	for (size_t bytes : {cap, cap + 1}) {
		std::string call = "let m = midi.create(); midi.setRaw(m, \"" + std::string(bytes * 2, '0') + "\"); midiOut.send(m);";
		std::string luaCall = "local m = midi.create(); midi.setRaw(m, \"" + std::string(bytes * 2, '0') + "\"); midiOut.send(m)";
		requireEquivalentLog(jsOnMessage(call), luaOnMessage(luaCall), "exceeds maximum", bytes > cap);
	}
}

TEST_CASE("Lua trig.setGate rejects more than three arguments like QuickJS", "[MidiKit]") {
	EngineRun js = run(jsOnMessage("trig.setGate(1, 2, 5, 9);"));
	EngineRun lua = run(luaOnMessage("trig.setGate(1, 2, 5, 9)"));
	CATCH_INFO(js.log);
	CATCH_INFO(lua.log);
	REQUIRE(js.log.find("trig.setGate") != std::string::npos);
	REQUIRE(lua.log.find("trig.setGate") != std::string::npos);
}

TEST_CASE("QuickJS trig.setHigh/setLow/setTrigger errors name their own function", "[MidiKit]") {
	for (const char* fn : {"setHigh", "setLow", "setTrigger"}) {
		EngineRun js = run(jsOnMessage(std::string("trig.") + fn + "(\"x\");"));
		CATCH_INFO(js.log);
		REQUIRE(js.log.find(std::string("trig.") + fn + ": bad args") != std::string::npos);
	}
}


// setSysEx

static const char* JS_SYSEX = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let out = midi.create();
    midi.setSysEx(out, "43104c0000");
    midiOut.send(out);
};
)";

static const char* LUA_SYSEX = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local out = midi.create()
    midi.setSysEx(out, "43104c0000")
    midiOut.send(out)
end
)";

TEST_CASE("setSysEx frames the payload identically", "[MidiKit][CrossEngine]") {
	requireBytes(runBoth(Pair{JS_SYSEX, LUA_SYSEX}), {{0xf0, 0x43, 0x10, 0x4c, 0x00, 0x00, 0xf7}});
}


// getSysEx / getSysExLength round-trip
// The payload getter (renamed from getSysExData) plus its size guard: the
// script can check the payload length before reading it, and getSysEx returns
// the payload with the f0/f7 framing excluded. The empty payload round-trips
// as "[]".

static const char* JS_GET_SYSEX = R"(/**
 * @engine QuickJs@v1
 */
rack.onLoad = function() {
    let m1 = midi.create();
    midi.setSysEx(m1, "43104c0000");
    rack.log("PROBE:" + number.toString(midi.getSysExLength(m1)));
    rack.log("PROBE:" + midi.getSysEx(m1));

    let m2 = midi.create();
    midi.setSysEx(m2, "");
    rack.log("PROBE:" + number.toString(midi.getSysExLength(m2)));
    rack.log("PROBE:[" + midi.getSysEx(m2) + "]");
};
)";

static const char* LUA_GET_SYSEX = R"(--[[
@engine minilua@v1
--]]
rack.onLoad = function()
    local m1 = midi.create()
    midi.setSysEx(m1, "43104c0000")
    rack.log("PROBE:" .. number.toString(midi.getSysExLength(m1)))
    rack.log("PROBE:" .. midi.getSysEx(m1))

    local m2 = midi.create()
    midi.setSysEx(m2, "")
    rack.log("PROBE:" .. number.toString(midi.getSysExLength(m2)))
    rack.log("PROBE:[" .. midi.getSysEx(m2) .. "]")
end
)";

TEST_CASE("getSysEx returns the payload and getSysExLength the size", "[MidiKit][CrossEngine]") {
	requireLoggedValues(JS_GET_SYSEX, LUA_GET_SYSEX, {"5", "43104c0000", "0", "[]"});
}


// sendAfterTrigger (3-arg form)
// Regression coverage for a bug where Lua used to misread the
// 3-arg form as (msg, trigPort, ticks) instead of matching QuickJs's
// (midiPort-selected, msg, ticks). Comparing the two engines directly is
// exactly the check that would have caught #7 the moment it was introduced,
// rather than requiring a human to notice the scripts behaved differently.

static const char* JS_SEND_AFTER_TRIGGER = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let out = midi.create();
    midi.setNoteOn(out, 1, 60, 100);
    midiOut.sendAfterTrigger(out, 10);
};
)";

static const char* LUA_SEND_AFTER_TRIGGER = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local out = midi.create()
    midi.setNoteOn(out, 1, 60, 100)
    midiOut.sendAfterTrigger(out, 10)
end
)";

TEST_CASE("sendAfterTrigger 2-arg form is identical", "[MidiKit][CrossEngine]") {
	Both b = runBoth(Pair{JS_SEND_AFTER_TRIGGER, LUA_SEND_AFTER_TRIGGER});
	requireBytes(b, {{0x90, 60, 100}});
	// The delay is the argument #7 swapped: ticks, not the port.
	requireTicks(b, {10});
}



// getRaw / setRaw round trip

static const char* JS_RAW = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let out = midi.create();
    midi.setRaw(out, "f11a");
    midiOut.send(out);
};
)";

static const char* LUA_RAW = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local out = midi.create()
    midi.setRaw(out, "f11a")
    midiOut.send(out)
end
)";

TEST_CASE("setRaw writes identical bytes with no framing", "[MidiKit][CrossEngine]") {
	requireBytes(runBoth(Pair{JS_RAW, LUA_RAW}), {{0xf1, 0x1a}});
}


// setPitchWheel

static const char* JS_PITCH_WHEEL = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let out = midi.create();
    midi.setPitchWheel(out, 2, 12345);
    midiOut.send(out);
};
)";

static const char* LUA_PITCH_WHEEL = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local out = midi.create()
    midi.setPitchWheel(out, 2, 12345)
    midiOut.send(out)
end
)";

TEST_CASE("setPitchWheel produces identical wire bytes", "[MidiKit][CrossEngine]") {
	requireBytes(runBoth(Pair{JS_PITCH_WHEEL, LUA_PITCH_WHEEL}), {{0xe1, 12345 & 0x7f, 12345 >> 7}});
}


// setProgramChange

static const char* JS_PROGRAM_CHANGE = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let out = midi.create();
    midi.setProgramChange(out, 4, 10);
    midiOut.send(out);
};
)";

static const char* LUA_PROGRAM_CHANGE = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local out = midi.create()
    midi.setProgramChange(out, 4, 10)
    midiOut.send(out)
end
)";

TEST_CASE("setProgramChange produces identical wire bytes", "[MidiKit][CrossEngine]") {
	requireBytes(runBoth(Pair{JS_PROGRAM_CHANGE, LUA_PROGRAM_CHANGE}), {{0xc3, 10}});

	// Literal bytes too: equivalence alone passes with both engines wrong.
	Pair scripts{JS_PROGRAM_CHANGE, LUA_PROGRAM_CHANGE};
	FOR_EACH_LANG;
	const char* script = scripts.get(lang);
	CATCH_INFO(script);
	EngineRun r = run(script);
	REQUIRE(r.sent.size() == 1);
	REQUIRE(r.sent[0].bytes == std::vector<uint8_t>{0xc3, 10});
}


// getProgramChange round-trips the program number

static const char* JS_GET_PROGRAM_CHANGE = R"(/**
 * @engine QuickJs@v1
 */
rack.onLoad = function() {
    let low = midi.create();
    midi.setProgramChange(low, 4, 0);
    rack.log("PROBE:" + number.toString(midi.getProgramChange(low)));

    let high = midi.create();
    midi.setProgramChange(high, 4, 127);
    rack.log("PROBE:" + number.toString(midi.getProgramChange(high)));
};
)";

static const char* LUA_GET_PROGRAM_CHANGE = R"(--[[
@engine minilua@v1
--]]
rack.onLoad = function()
    local low = midi.create()
    midi.setProgramChange(low, 4, 0)
    rack.log("PROBE:" .. number.toString(midi.getProgramChange(low)))

    local high = midi.create()
    midi.setProgramChange(high, 4, 127)
    rack.log("PROBE:" .. number.toString(midi.getProgramChange(high)))
end
)";

TEST_CASE("getProgramChange round-trips the program number", "[MidiKit][CrossEngine]") {
	requireLoggedValues(JS_GET_PROGRAM_CHANGE, LUA_GET_PROGRAM_CHANGE, {"0", "127"});
}


// setChanPressure (2-byte message)

static const char* JS_CHAN_PRESSURE = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let out = midi.create();
    midi.setChanPressure(out, 5, 80);
    midiOut.send(out);
};
)";

static const char* LUA_CHAN_PRESSURE = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local out = midi.create()
    midi.setChanPressure(out, 5, 80)
    midiOut.send(out)
end
)";

TEST_CASE("setChanPressure produces identical 2-byte wire message", "[MidiKit][CrossEngine]") {
	requireBytes(runBoth(Pair{JS_CHAN_PRESSURE, LUA_CHAN_PRESSURE}), {{0xd4, 80}});

	Pair scripts{JS_CHAN_PRESSURE, LUA_CHAN_PRESSURE};
	FOR_EACH_LANG;
	const char* script = scripts.get(lang);
	CATCH_INFO(script);
	EngineRun r = run(script);
	REQUIRE(r.sent.size() == 1);
	REQUIRE(r.sent[0].bytes == std::vector<uint8_t>{0xd4, 80});
}


// setKeyPressure

static const char* JS_KEY_PRESSURE = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let out = midi.create();
    midi.setKeyPressure(out, 6, 64, 90);
    midiOut.send(out);
};
)";

static const char* LUA_KEY_PRESSURE = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local out = midi.create()
    midi.setKeyPressure(out, 6, 64, 90)
    midiOut.send(out)
end
)";

TEST_CASE("setKeyPressure produces identical wire bytes", "[MidiKit][CrossEngine]") {
	requireBytes(runBoth(Pair{JS_KEY_PRESSURE, LUA_KEY_PRESSURE}), {{0xa5, 64, 90}});
}




// Setter arguments: rounded to an integer and clamped, never wrapped.
// Expected bytes are asserted outright, since both engines agreeing on garbage
// would pass requireEquivalent.

TEST_CASE("setters clamp and round out-of-range arguments identically", "[MidiKit][CrossEngine]") {
	struct Case {
		const char* call;
		std::vector<uint8_t> bytes;
	};
	const Case cases[] = {
		{"midi.setNoteOn(m, -5, 60, 100)", {0x90, 60, 100}},
		{"midi.setNoteOn(m, 99, 60, 100)", {0x9f, 60, 100}},
		{"midi.setNoteOn(m, 1, 132, 100)", {0x90, 127, 100}},
		{"midi.setNoteOn(m, 1, -3, 100)", {0x90, 0, 100}},
		{"midi.setNoteOn(m, 1, 60.4, 100)", {0x90, 60, 100}},
		{"midi.setNoteOn(m, 1, 60.5, 100)", {0x90, 61, 100}},
		{"midi.setNoteOn(m, 1, 60, 1000000)", {0x90, 60, 127}},
		{"midi.setCc(m, 1, 130, 200)", {0xb0, 127, 127}},
		{"midi.setProgramChange(m, 1, 130)", {0xc0, 127}},
		{"midi.setChanPressure(m, 1, 300)", {0xd0, 127}},
		{"midi.setPitchWheel(m, 1, -100)", {0xe0, 0, 0}},
		{"midi.setPitchWheel(m, 1, 20000)", {0xe0, 0x7f, 0x7f}},
		{"midi.setPitchWheel(m, 1, 8192.4)", {0xe0, 0, 0x40}},
		{"midi.setNote(m, 128)", {0x90, 127, 100}},
		{"midi.setValue(m, 255)", {0x90, 60, 127}},
	};
	for (const Case& c : cases) {
		std::string call = c.call;
		// setNote/setValue modify an existing message, so build a NoteOn first.
		std::string pre = "midi.setNoteOn(m, 1, 60, 100); ";
		bool needsPre = call.find("setNote(") != std::string::npos || call.find("setValue(") != std::string::npos;
		std::string body = (needsPre ? pre : "") + call + "; midiOut.send(m);";
		EngineRun js = run(jsOnMessage("let m = midi.create(); " + body));
		EngineRun lua = run(luaOnMessage("local m = midi.create(); " + body));
		CATCH_INFO(c.call);
		CATCH_INFO(js.log);
		CATCH_INFO(lua.log);
		REQUIRE(js.sent.size() == 1);
		REQUIRE(lua.sent.size() == 1);
		REQUIRE(js.sent[0].bytes == c.bytes);
		REQUIRE(lua.sent[0].bytes == c.bytes);
	}
}

TEST_CASE("setCc14bit clamps its MSB controller to 0-31, so the LSB is always cc + 32", "[MidiKit][CrossEngine]") {
	// Only controllers 0-31 have an LSB partner at cc + 32. Above that the LSB
	// would wrap onto an MSB controller (100 -> 4) or leave the 14-bit range.
	struct Case { const char* cc; uint8_t msb; };
	const Case cases[] = { {"100", 31}, {"127", 31}, {"32", 31}, {"31", 31}, {"5", 5}, {"0", 0}, {"-1", 0} };
	for (const Case& c : cases) {
		CATCH_INFO("cc " << c.cc);
		const std::vector<uint8_t> msbBytes = {0xb0, c.msb, 64};
		const std::vector<uint8_t> lsbBytes = {0xb0, uint8_t(c.msb + 32), 64};

		// Pair handle: sent as one group.
		EngineRun js = run(jsOnMessage(std::string("let m = midi.createCc14bit(); midi.setCc14bit(m, 1, ") + c.cc + ", 8256); midiOut.send(m);"));
		EngineRun lua = run(luaOnMessage(std::string("local m = midi.createCc14bit(); midi.setCc14bit(m, 1, ") + c.cc + ", 8256); midiOut.send(m)"));
		CATCH_INFO(js.log);
		CATCH_INFO(lua.log);
		REQUIRE(js.sent.size() == 2);
		REQUIRE(lua.sent.size() == 2);
		for (EngineRun* r : {&js, &lua}) {
			REQUIRE(r->sent[0].bytes == msbBytes);
			REQUIRE(r->sent[1].bytes == lsbBytes);
		}

		// Two independent handles.
		EngineRun js2 = run(jsOnMessage(std::string("let a = midi.create(); let b = midi.create(); midi.setCc14bit(a, b, 1, ") + c.cc + ", 8256); midiOut.send(a); midiOut.send(b);"));
		EngineRun lua2 = run(luaOnMessage(std::string("local a = midi.create(); local b = midi.create(); midi.setCc14bit(a, b, 1, ") + c.cc + ", 8256); midiOut.send(a); midiOut.send(b)"));
		CATCH_INFO(js2.log);
		CATCH_INFO(lua2.log);
		REQUIRE(js2.sent.size() == 2);
		REQUIRE(lua2.sent.size() == 2);
		for (EngineRun* r : {&js2, &lua2}) {
			REQUIRE(r->sent[0].bytes == msbBytes);
			REQUIRE(r->sent[1].bytes == lsbBytes);
		}
	}
}

TEST_CASE("setCc14bit rounds and clamps its value to 14 bits", "[MidiKit][CrossEngine]") {
	for (const char* v : {"20000", "-5"}) {
		std::string args = std::string("midi.setCc14bit(m, 1, 1, ") + v + ");";
		EngineRun js = run(jsOnMessage("let m = midi.createCc14bit(); " + args + " midiOut.send(m);"));
		EngineRun lua = run(luaOnMessage("local m = midi.createCc14bit(); " + args + " midiOut.send(m)"));
		CATCH_INFO(js.log);
		CATCH_INFO(lua.log);
		REQUIRE(!js.sent.empty());
		REQUIRE(js.sent.size() == lua.sent.size());
		for (size_t i = 0; i < js.sent.size(); i++) {
			REQUIRE(js.sent[i].bytes == lua.sent[i].bytes);
			REQUIRE(js.sent[i].bytes.size() == 3);
			REQUIRE(js.sent[i].bytes[2] < 0x80);
		}
	}
}


// Send-side RPN: the NRPN chain with CC 101/100 selecting the parameter.
TEST_CASE("createRPN/setRPN send RPN select + data entry identically", "[MidiKit][CrossEngine]") {
	// RPN 0 (pitch-bend sensitivity) = 12 semitones on channel 2.
	std::string call = "midi.setRPN(m, 2, 0, 12); midiOut.send(m);";
	EngineRun js = run(jsOnMessage("let m = midi.createRPN(); " + call));
	EngineRun lua = run(luaOnMessage("local m = midi.createRPN(); " + call));
	CATCH_INFO(js.log);
	CATCH_INFO(lua.log);
	const std::vector<std::vector<uint8_t>> expected = {
		{0xb1, 101, 0}, {0xb1, 100, 0}, {0xb1, 6, 0}, {0xb1, 38, 12}
	};
	REQUIRE(js.sent.size() == 4);
	REQUIRE(lua.sent.size() == 4);
	for (size_t i = 0; i < 4; i++) {
		REQUIRE(js.sent[i].bytes == expected[i]);
		REQUIRE(lua.sent[i].bytes == expected[i]);
	}
}

// RPN 16383 (127/127) is the spec's "null": no parameter selected, so only the
// select pair is sent, with no data entry, and the handle has no value. The NRPN
// spec has no null, so NRPN 16383 is an ordinary parameter with all four CCs.
TEST_CASE("setRPN with 16383 sends only the RPN null select, NRPN 16383 stays a full group", "[MidiKit][CrossEngine]") {
	struct Case { const char* create; const char* set; std::vector<std::vector<uint8_t>> bytes; };
	const Case cases[] = {
		// The value is ignored for the null.
		{ "createRPN", "setRPN", {{0xb1, 101, 127}, {0xb1, 100, 127}} },
		{ "createNRPN", "setNRPN", {{0xb1, 99, 127}, {0xb1, 98, 127}, {0xb1, 6, 7}, {0xb1, 38, 9}} },
	};
	for (const Case& c : cases) {
		CATCH_INFO(c.create);
		std::string jsCall = std::string("let m = midi.") + c.create + "(); midi." + c.set + "(m, 2, 16383, 7 * 128 + 9); midiOut.send(m);";
		std::string luaCall = std::string("local m = midi.") + c.create + "(); midi." + c.set + "(m, 2, 16383, 7 * 128 + 9); midiOut.send(m)";
		EngineRun js = run(jsOnMessage(jsCall));
		EngineRun lua = run(luaOnMessage(luaCall));
		CATCH_INFO(js.log);
		CATCH_INFO(lua.log);
		REQUIRE(js.sent.size() == c.bytes.size());
		REQUIRE(lua.sent.size() == c.bytes.size());
		for (size_t i = 0; i < c.bytes.size(); i++) {
			REQUIRE(js.sent[i].bytes == c.bytes[i]);
			REQUIRE(lua.sent[i].bytes == c.bytes[i]);
		}
	}
}

TEST_CASE("The RPN null handle has a number but no value, and its setters and clone follow", "[MidiKit][CrossEngine]") {
	// isRpn, control, value, channel; then the same after setChannel, setValue and a clone.
	std::string js = "let g = midi.createRPN(); midi.setRPN(g, 2, 16383, 1000);"
		"rack.log('A:' + [(midi.getType(g) === midi.RPN), midi.getControl(g), midi.getValue(g), midi.getChannel(g)].join(' '));"
		"midi.setChannel(g, 5); midi.setValue(g, 77);"
		"let c = midi.clone(g);"
		"rack.log('B:' + [(midi.getType(c) === midi.RPN), midi.getControl(c), midi.getValue(c), midi.getChannel(c)].join(' '));"
		"midiOut.send(c);";
	std::string lua = "local g = midi.createRPN(); midi.setRPN(g, 2, 16383, 1000);"
		"rack.log('A:' .. table.concat({tostring(midi.getType(g) == midi.RPN), midi.getControl(g), midi.getValue(g), midi.getChannel(g)}, ' '));"
		"midi.setChannel(g, 5); midi.setValue(g, 77);"
		"local c = midi.clone(g);"
		"rack.log('B:' .. table.concat({tostring(midi.getType(c) == midi.RPN), midi.getControl(c), midi.getValue(c), midi.getChannel(c)}, ' '));"
		"midiOut.send(c)";
	EngineRun r1 = run(jsOnMessage(js));
	EngineRun r2 = run(luaOnMessage(lua));
	CATCH_INFO(r1.log);
	CATCH_INFO(r2.log);
	for (const EngineRun* r : { &r1, &r2 }) {
		REQUIRE(r->log.find("A:true 16383 -1 2") != std::string::npos);
		REQUIRE(r->log.find("B:true 16383 -1 5") != std::string::npos);
		// The clone is the whole null group: just the two selects, on the new channel.
		REQUIRE(r->sent.size() == 2);
		REQUIRE(r->sent[0].bytes == std::vector<uint8_t>({0xb4, 101, 127}));
		REQUIRE(r->sent[1].bytes == std::vector<uint8_t>({0xb4, 100, 127}));
	}
}

TEST_CASE("setNRPN and setRPN reject each other's handles", "[MidiKit][CrossEngine]") {
	struct Case { const char* create; const char* set; const char* error; };
	const Case cases[] = {
		{"midi.createRPN()", "midi.setNRPN", "not an NRPN"},
		{"midi.createNRPN()", "midi.setRPN", "not an RPN"},
	};
	for (const Case& c : cases) {
		std::string call = std::string(c.set) + "(m, 1, 0, 0);";
		EngineRun lua = run(luaOnMessage(std::string("local m = ") + c.create + "; " + call));
		CATCH_INFO(lua.log);
		REQUIRE(lua.log.find(c.error) != std::string::npos);
		// JS names the same misuse "invalid nrpn/rpn message".
		EngineRun js = run(jsOnMessage(std::string("let m = ") + c.create + "; " + call));
		CATCH_INFO(js.log);
		REQUIRE(js.log.find("invalid") != std::string::npos);
	}
}


// rack.msToFrames / rack.framesToMs at the test module's 44100 Hz.
TEST_CASE("rack.msToFrames and framesToMs convert at the sample rate", "[MidiKit][CrossEngine]") {
	struct Case { const char* expr; const char* expected; };
	const Case cases[] = {
		{"rack.msToFrames(10)", "441"},
		{"rack.msToFrames(0)", "0"},
		{"rack.msToFrames(-10)", "-441"},
		{"rack.msToFrames(0.5)", "22"},          // 22.05 rounds to a whole frame
		{"rack.msToFrames(1000)", "44100"},
		{"rack.framesToMs(44100)", "1000"},
		{"rack.framesToMs(441)", "10"},
		{"rack.framesToMs(0)", "0"},
	};
	for (const Case& c : cases) {
		CATCH_INFO(c.expr);
		EngineRun js = run(jsOnMessage(std::string("rack.log(") + c.expr + ");"));
		EngineRun lua = run(luaOnMessage(std::string("rack.log(") + c.expr + ")"));
		CATCH_INFO(js.log);
		CATCH_INFO(lua.log);
		REQUIRE(js.log.find(c.expected) != std::string::npos);
		REQUIRE(lua.log.find(c.expected) != std::string::npos);
	}

	// Usable with sendAtFrame: 10 ms after the event is still the event's frame
	// plus 441, so the message is sent (identically in both engines).
	requireBytes(runBoth(
		"let m = midi.create(); midi.setNoteOn(m, 1, 60, 100); midiOut.sendAtFrame(m, rack.getEventFrame() + rack.msToFrames(10));",
		"local m = midi.create(); midi.setNoteOn(m, 1, 60, 100); midiOut.sendAtFrame(m, rack.getEventFrame() + rack.msToFrames(10))"),
		{{0x90, 60, 100}});

	// Non-numbers are rejected rather than treated as 0.
	for (const char* fn : {"msToFrames", "framesToMs"}) {
		EngineRun js = run(jsOnMessage(std::string("rack.") + fn + "(\"x\");"));
		EngineRun lua = run(luaOnMessage(std::string("rack.") + fn + "(\"x\")"));
		CATCH_INFO(js.log);
		CATCH_INFO(lua.log);
		REQUIRE(js.log.find(fn) != std::string::npos);
		REQUIRE(!lua.log.empty());
	}
}


// midi.clone
// clone(msg) must produce an independent copy: same MIDI payload, but a
// fresh, unsent message. Editing the clone must not touch the source (a
// naive "return the same slot" alias would fail the first case below), and
// cloning the incoming message (handle 0) is the common idiom "send
// a modified copy of the incoming message".

static const char* JS_MIDI_CLONE = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let src = midi.create();
    midi.setNoteOn(src, 1, 60, 100);
    let copy = midi.clone(src);
    midi.setNote(copy, 70);      // edit the clone only
    midiOut.send(src);           // source unchanged -> note 60
    midiOut.send(copy);          // clone carries the edit -> note 70
};
)";

static const char* LUA_MIDI_CLONE = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local src = midi.create()
    midi.setNoteOn(src, 1, 60, 100)
    local copy = midi.clone(src)
    midi.setNote(copy, 70)
    midiOut.send(src)
    midiOut.send(copy)
end
)";

TEST_CASE("midi.clone is an independent copy in both engines", "[MidiKit][CrossEngine]") {
	// The source keeps note 60; the clone carries the edit.
	requireBytes(runBoth(Pair{JS_MIDI_CLONE, LUA_MIDI_CLONE}), {{0x90, 60, 100}, {0x90, 70, 100}});
}


static const char* JS_MIDI_CLONE_INCOMING = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let copy = midi.clone(msg);   // deep copy of the incoming note-on
    midi.setChannel(copy, 5);     // reroute to channel 5
    midiOut.send(copy);
};
)";

static const char* LUA_MIDI_CLONE_INCOMING = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local copy = midi.clone(msg)
    midi.setChannel(copy, 5)
    midiOut.send(copy)
end
)";

TEST_CASE("midi.clone of the incoming message sends a modified copy", "[MidiKit][CrossEngine]") {
	// The default input is a Note-On on channel index 1; the clone moves to channel 5 (index 4).
	requireBytes(runBoth(Pair{JS_MIDI_CLONE_INCOMING, LUA_MIDI_CLONE_INCOMING}), {{0x94, 60, 100}});
}


// setNoteOff

static const char* JS_NOTE_OFF = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let out = midi.create();
    midi.setNoteOff(out, 7, 48);
    midiOut.send(out);
};
)";

static const char* LUA_NOTE_OFF = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local out = midi.create()
    midi.setNoteOff(out, 7, 48)
    midiOut.send(out)
end
)";

TEST_CASE("setNoteOff produces identical wire bytes", "[MidiKit][CrossEngine]") {
	requireBytes(runBoth(Pair{JS_NOTE_OFF, LUA_NOTE_OFF}), {{0x86, 48, 0}});
}


// setNoteOff velocity
// The optional 4th arg sets the release velocity (byte 3), read back with
// getValue — symmetric with note-on velocity. The 3-arg form keeps velocity
// 0 for backward compatibility; the velocity clamps to 0-127.

static const char* JS_NOTE_OFF_VEL = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let out = midi.create();
    midi.setNoteOff(out, 7, 48, 100);
    midiOut.send(out);
};
)";

static const char* LUA_NOTE_OFF_VEL = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local out = midi.create()
    midi.setNoteOff(out, 7, 48, 100)
    midiOut.send(out)
end
)";

static const char* JS_NOTE_OFF_VEL_PROBE = R"(/**
 * @engine QuickJs@v1
 */
rack.onLoad = function() {
    let a = midi.create();
    midi.setNoteOff(a, 7, 48, 100);
    rack.log("PROBE:" + number.toString(midi.getValue(a)));
    midi.setNoteOff(a, 7, 48);
    rack.log("PROBE:" + number.toString(midi.getValue(a)));
    midi.setNoteOff(a, 7, 48, 500);
    rack.log("PROBE:" + number.toString(midi.getValue(a)));
    midi.setNoteOff(a, 7, 48, -5);
    rack.log("PROBE:" + number.toString(midi.getValue(a)));
};
)";

static const char* LUA_NOTE_OFF_VEL_PROBE = R"(--[[
@engine minilua@v1
--]]
rack.onLoad = function()
    local a = midi.create()
    midi.setNoteOff(a, 7, 48, 100)
    rack.log("PROBE:" .. number.toString(midi.getValue(a)))
    midi.setNoteOff(a, 7, 48)
    rack.log("PROBE:" .. number.toString(midi.getValue(a)))
    midi.setNoteOff(a, 7, 48, 500)
    rack.log("PROBE:" .. number.toString(midi.getValue(a)))
    midi.setNoteOff(a, 7, 48, -5)
    rack.log("PROBE:" .. number.toString(midi.getValue(a)))
end
)";

TEST_CASE("setNoteOff velocity is sent as byte 3, round-trips via getValue and clamps", "[MidiKit][CrossEngine]") {
	requireBytes(runBoth(Pair{JS_NOTE_OFF_VEL, LUA_NOTE_OFF_VEL}), {{0x86, 48, 100}});
	requireLoggedValues(JS_NOTE_OFF_VEL_PROBE, LUA_NOTE_OFF_VEL_PROBE, {"100", "0", "127", "0"});
}


// Group handles (createNRPN / createRPN / createCc14bit) and the single-message
// setters: a setter that writes only the lead slot would leave a broken group,
// so setChannel() rechannels the whole group and the others raise an error.

namespace {

struct GroupKind {
	const char* name;
	const char* js;        // creates and sets the group handle `g`
	const char* lua;
	size_t size;           // messages the group sends
	const char* error;     // the tail of the error a single-message setter raises
};

const GroupKind GROUP_KINDS[] = {
	{ "NRPN", "let g = midi.createNRPN(); midi.setNRPN(g, 1, 300, 1000);",
	          "local g = midi.createNRPN(); midi.setNRPN(g, 1, 300, 1000)",
	  4, "message is an NRPN; use midi.setNRPN()" },
	{ "RPN", "let g = midi.createRPN(); midi.setRPN(g, 1, 300, 1000);",
	         "local g = midi.createRPN(); midi.setRPN(g, 1, 300, 1000)",
	  4, "message is an RPN; use midi.setRPN()" },
	{ "14-bit CC", "let g = midi.createCc14bit(); midi.setCc14bit(g, 1, 5, 12864);",
	               "local g = midi.createCc14bit(); midi.setCc14bit(g, 1, 5, 12864)",
	  2, "message is a 14-bit CC; use midi.setCc14bit()" },
};

// `call` inside a try/catch (pcall) that logs "ERR <message>".
std::string jsGuarded(const std::string& call) {
	return "try { " + call + " } catch (e) { rack.log('ERR ' + e.message); }";
}
std::string luaGuarded(const std::string& call) {
	return "local ok, err = pcall(function() " + call + " end); if not ok then rack.log('ERR ' .. err) end";
}

}

TEST_CASE("setChannel on a group handle rechannels every message of the group", "[MidiKit][CrossEngine]") {
	for (const GroupKind& k : GROUP_KINDS) {
		CATCH_INFO(k.name);
		EngineRun base = run(jsOnMessage(std::string(k.js) + " midiOut.send(g);"));
		EngineRun js = run(jsOnMessage(std::string(k.js) + " midi.setChannel(g, 5); midiOut.send(g);"));
		EngineRun lua = run(luaOnMessage(std::string(k.lua) + "; midi.setChannel(g, 5); midiOut.send(g)"));
		CATCH_INFO(js.log);
		CATCH_INFO(lua.log);

		REQUIRE(base.sent.size() == k.size);
		REQUIRE(js.sent.size() == k.size);
		REQUIRE(lua.sent.size() == k.size);
		for (size_t i = 0; i < k.size; i++) {
			// Only the channel nibble differs from the group set on channel 1.
			REQUIRE(js.sent[i].bytes[0] == 0xb4);
			REQUIRE(js.sent[i].bytes[1] == base.sent[i].bytes[1]);
			REQUIRE(js.sent[i].bytes[2] == base.sent[i].bytes[2]);
			REQUIRE(js.sent[i].bytes == lua.sent[i].bytes);
		}
	}
}

TEST_CASE("The group setter overrides an earlier setChannel, and setChannel on an unset group is harmless", "[MidiKit][CrossEngine]") {
	for (const GroupKind& k : GROUP_KINDS) {
		CATCH_INFO(k.name);
		// setChannel first, then the group setter: its channel wins on every member.
		std::string create = std::string(k.js);
		std::string head = create.substr(0, create.find(" midi.set"));
		std::string setter = create.substr(create.find(" midi.set"));
		std::string luaCreate = k.lua;
		std::string luaHead = luaCreate.substr(0, luaCreate.find("; midi.set"));
		std::string luaSetter = luaCreate.substr(luaCreate.find("; midi.set"));

		EngineRun js = run(jsOnMessage(head + " midi.setChannel(g, 5);" + setter + " midiOut.send(g);"));
		EngineRun lua = run(luaOnMessage(luaHead + "; midi.setChannel(g, 5)" + luaSetter + "; midiOut.send(g)"));
		CATCH_INFO(js.log);
		CATCH_INFO(lua.log);
		REQUIRE(js.log.empty());
		REQUIRE(lua.log.empty());
		REQUIRE(js.sent.size() == k.size);
		REQUIRE(js.sent == lua.sent);
		for (const auto& m : js.sent) REQUIRE(m.bytes[0] == 0xb0);

		// On a group that was never set, it raises nothing.
		EngineRun unsetJs = run(jsOnMessage(head + " midi.setChannel(g, 5);"));
		EngineRun unsetLua = run(luaOnMessage(luaHead + "; midi.setChannel(g, 5)"));
		REQUIRE(unsetJs.log.empty());
		REQUIRE(unsetLua.log.empty());
	}
}

TEST_CASE("Single-message setters raise an error on a group handle and leave it unchanged", "[MidiKit][CrossEngine]") {
	// Each call is valid on a plain handle. `p` is a plain handle. (setValue is
	// not here: on a group it sets the combined 14-bit value.)
	struct Setter { const char* fn; const char* args; };
	const Setter setters[] = {
		{ "setNote", "g, 1" },
		{ "setCc", "g, 1, 7, 1" },
		{ "setNoteOn", "g, 1, 60, 100" },
		{ "setNoteOff", "g, 1, 60" },
		{ "setKeyPressure", "g, 1, 60, 1" },
		{ "setChanPressure", "g, 1, 1" },
		{ "setPitchWheel", "g, 1, 100" },
		{ "setProgramChange", "g, 1, 1" },
		{ "setSysEx", "g, \"01\"" },
		{ "setRaw", "g, \"b00101\"" },
		// Five-argument setCc14bit: the group handle as either message.
		{ "setCc14bit", "g, p, 1, 5, 1" },
		{ "setCc14bit", "p, g, 1, 5, 1" },
	};
	for (const GroupKind& k : GROUP_KINDS) {
		EngineRun base = run(jsOnMessage(std::string(k.js) + " midiOut.send(g);"));
		REQUIRE(base.sent.size() == k.size);
		for (const Setter& s : setters) {
			CATCH_INFO(std::string(k.name) + " " + s.fn + "(" + s.args + ")");
			std::string call = std::string("midi.") + s.fn + "(" + s.args + ");";
			std::string expected = std::string("midi.") + s.fn + ": " + k.error;

			EngineRun js = run(jsOnMessage(std::string(k.js) + " let p = midi.create(); " + jsGuarded(call) + " midiOut.send(g);"));
			EngineRun lua = run(luaOnMessage(std::string(k.lua) + "; local p = midi.create(); " + luaGuarded(call) + "; midiOut.send(g)"));
			CATCH_INFO(js.log);
			CATCH_INFO(lua.log);

			REQUIRE(js.log.find(expected) != std::string::npos);
			REQUIRE(lua.log.find(expected) != std::string::npos);
			// The group still goes out exactly as it was set.
			REQUIRE(js.sent == base.sent);
			REQUIRE(lua.sent == base.sent);
		}
	}
}

TEST_CASE("Single-message setters on plain handles and on the incoming message are unaffected", "[MidiKit][CrossEngine]") {
	EngineRun js = run(jsOnMessage("midi.setChannel(msg, 3); midi.setValue(msg, 9); midi.setNote(msg, 61); midiOut.send(msg);"));
	EngineRun lua = run(luaOnMessage("midi.setChannel(msg, 3); midi.setValue(msg, 9); midi.setNote(msg, 61); midiOut.send(msg)"));
	CATCH_INFO(js.log);
	CATCH_INFO(lua.log);
	REQUIRE(js.log.empty());
	REQUIRE(lua.log.empty());
	REQUIRE(js.sent.size() == 1);
	REQUIRE(js.sent == lua.sent);
	REQUIRE((js.sent[0].bytes[0] & 0x0f) == 2);
	REQUIRE(js.sent[0].bytes[1] == 61);
	REQUIRE(js.sent[0].bytes[2] == 9);

	EngineRun plainJs = run(jsOnMessage("let m = midi.create(); midi.setNoteOn(m, 1, 60, 100); midi.setChannel(m, 4); midi.setNote(m, 62); midi.setValue(m, 7); midiOut.send(m);"));
	EngineRun plainLua = run(luaOnMessage("local m = midi.create(); midi.setNoteOn(m, 1, 60, 100); midi.setChannel(m, 4); midi.setNote(m, 62); midi.setValue(m, 7); midiOut.send(m)"));
	REQUIRE(plainJs.log.empty());
	REQUIRE(plainJs.sent == plainLua.sent);
	REQUIRE(plainJs.sent.size() == 1);
	REQUIRE(plainJs.sent[0].bytes == std::vector<uint8_t>({0x93, 62, 7}));
}


// midi.clone of a group handle is a group again: all its messages and the chain
// flags, independent of the source.

TEST_CASE("midi.clone of a group handle clones the whole group independently", "[MidiKit][CrossEngine]") {
	for (const GroupKind& k : GROUP_KINDS) {
		CATCH_INFO(k.name);
		EngineRun base = run(jsOnMessage(std::string(k.js) + " midiOut.send(g);"));
		REQUIRE(base.sent.size() == k.size);

		// The clone sends the same group as the source...
		EngineRun js = run(jsOnMessage(std::string(k.js) + " let c = midi.clone(g); midiOut.send(c);"));
		EngineRun lua = run(luaOnMessage(std::string(k.lua) + "; local c = midi.clone(g); midiOut.send(c)"));
		CATCH_INFO(js.log);
		CATCH_INFO(lua.log);
		REQUIRE(js.log.empty());
		REQUIRE(lua.log.empty());
		REQUIRE(js.sent == base.sent);
		REQUIRE(lua.sent == base.sent);

		// ...and is a group handle: its own channel applies to all of it, and
		// the source stays on channel 1 (no shared slots).
		EngineRun ch = run(jsOnMessage(std::string(k.js) + " let c = midi.clone(g); midi.setChannel(c, 5); midiOut.send(c); midiOut.send(g);"));
		EngineRun chLua = run(luaOnMessage(std::string(k.lua) + "; local c = midi.clone(g); midi.setChannel(c, 5); midiOut.send(c); midiOut.send(g)"));
		REQUIRE(ch.sent.size() == 2 * k.size);
		REQUIRE(ch.sent == chLua.sent);
		for (size_t i = 0; i < k.size; i++) {
			REQUIRE(ch.sent[i].bytes[0] == 0xb4);
			REQUIRE(ch.sent[k.size + i].bytes[0] == 0xb0);
		}

		// Single-message setters are rejected on the clone, like on the source.
		std::string call = "midi.setNote(c, 1);";
		std::string expected = std::string("midi.setNote: ") + k.error;
		EngineRun rej = run(jsOnMessage(std::string(k.js) + " let c = midi.clone(g); " + jsGuarded(call)));
		EngineRun rejLua = run(luaOnMessage(std::string(k.lua) + "; local c = midi.clone(g); " + luaGuarded(call)));
		REQUIRE(rej.log.find(expected) != std::string::npos);
		REQUIRE(rejLua.log.find(expected) != std::string::npos);
	}

	// Re-setting the source after the clone leaves the clone as it was.
	EngineRun js = run(jsOnMessage("let g = midi.createNRPN(); midi.setNRPN(g, 1, 300, 1000); let c = midi.clone(g); midi.setNRPN(g, 2, 5, 6); midiOut.send(c);"));
	EngineRun lua = run(luaOnMessage("local g = midi.createNRPN(); midi.setNRPN(g, 1, 300, 1000); local c = midi.clone(g); midi.setNRPN(g, 2, 5, 6); midiOut.send(c)"));
	EngineRun base = run(jsOnMessage("let g = midi.createNRPN(); midi.setNRPN(g, 1, 300, 1000); midiOut.send(g);"));
	REQUIRE(js.sent == base.sent);
	REQUIRE(lua.sent == base.sent);
}

TEST_CASE("midi.clone of a group needs room for the whole group", "[MidiKit][CrossEngine]") {
	// 32 slots: the incoming message (1), an NRPN (4) and 26 plain handles leave one free. The plain
	// message still clones, the group does not, and the error names the store.
	std::string jsFill = "let g = midi.createNRPN(); midi.setNRPN(g, 1, 300, 1000); let p = midi.create(); midi.setNoteOn(p, 1, 60, 100); for (let i = 0; i < 25; i++) midi.create();";
	std::string luaFill = "local g = midi.createNRPN(); midi.setNRPN(g, 1, 300, 1000); local p = midi.create(); midi.setNoteOn(p, 1, 60, 100); for i = 1, 25 do midi.create() end";

	EngineRun js = run(jsOnMessage(jsFill + " midiOut.send(midi.clone(p)); " + jsGuarded("midi.clone(g);")));
	EngineRun lua = run(luaOnMessage(luaFill + " midiOut.send(midi.clone(p)); " + luaGuarded("midi.clone(g)")));
	CATCH_INFO(js.log);
	CATCH_INFO(lua.log);
	REQUIRE(js.sent.size() == 1);
	REQUIRE(js.sent == lua.sent);
	REQUIRE(js.log.find("message store full") != std::string::npos);
	REQUIRE(lua.log.find("message store full") != std::string::npos);
}


// A created group answers the type-aware accessors like a received one: its
// type is set by the constructor, its number and value by the setter.

TEST_CASE("A created group handle answers getType, getControl and getValue", "[MidiKit][CrossEngine]") {
	struct Case { const char* name; const char* js; const char* lua; const char* unset; const char* set; };
	const Case cases[] = {
		{ "NRPN", "let g = midi.createNRPN();", "local g = midi.createNRPN()", "false false false -1 -1",
		  "midi.setNRPN(g, 2, 300, 1000);" },
		{ "RPN", "let g = midi.createRPN();", "local g = midi.createRPN()", "false false false -1 -1",
		  "midi.setRPN(g, 2, 300, 1000);" },
		{ "14-bit CC", "let g = midi.createCc14bit();", "local g = midi.createCc14bit()", "false false false -1 -1",
		  "midi.setCc14bit(g, 2, 300, 12864);" },
	};
	const char* setExpected[] = { "true false false 300 1000 2", "false true false 300 1000 2", "false false true 31 12864 2" };
	// 14-bit CC: cc 300 clamps to 31, value 100.5 -> MSB 100, LSB 64 -> 100 * 128 + 64.
	const char* jsLog = "rack.log([(midi.getType(g) === midi.NRPN), (midi.getType(g) === midi.RPN), (midi.getType(g) === midi.CC14BIT), midi.getControl(g), midi.getValue(g), midi.getChannel(g)].join(' '));";
	const char* luaLog = "rack.log(table.concat({tostring(midi.getType(g) == midi.NRPN), tostring(midi.getType(g) == midi.RPN), tostring(midi.getType(g) == midi.CC14BIT), midi.getControl(g), midi.getValue(g), midi.getChannel(g)}, ' '))";
	size_t i = 0;
	for (const Case& c : cases) {
		CATCH_INFO(c.name);
		// Fresh handle: type none until the setter has run, and no number or value yet.
		EngineRun js = run(jsOnMessage(std::string(c.js) + " " + jsLog));
		EngineRun lua = run(luaOnMessage(std::string(c.lua) + "; " + luaLog));
		CATCH_INFO(js.log);
		CATCH_INFO(lua.log);
		REQUIRE(js.log.find(c.unset) != std::string::npos);
		REQUIRE(lua.log.find(c.unset) != std::string::npos);

		// Set: the number, the combined value and the channel.
		EngineRun jsSet = run(jsOnMessage(std::string(c.js) + " " + c.set + " " + jsLog));
		EngineRun luaSet = run(luaOnMessage(std::string(c.lua) + "; " + c.set + " " + luaLog));
		CATCH_INFO(jsSet.log);
		CATCH_INFO(luaSet.log);
		REQUIRE(jsSet.log.find(setExpected[i]) != std::string::npos);
		REQUIRE(luaSet.log.find(setExpected[i]) != std::string::npos);
		i++;
	}
}

TEST_CASE("setValue on a group sets the combined 14-bit value and keeps number and channel", "[MidiKit][CrossEngine]") {
	// 5000 = 39 * 128 + 8. NRPN/RPN: data entry CC 6 and CC 38. 14-bit CC (MSB 5): CC 5 and CC 37.
	struct Case { const char* name; const char* js; const char* lua; std::vector<std::vector<uint8_t>> bytes; };
	const Case cases[] = {
		{ "NRPN", "let g = midi.createNRPN(); midi.setNRPN(g, 1, 300, 1000);", "local g = midi.createNRPN(); midi.setNRPN(g, 1, 300, 1000)",
		  { {0xb4, 99, 2}, {0xb4, 98, 44}, {0xb4, 6, 39}, {0xb4, 38, 8} } },
		{ "RPN", "let g = midi.createRPN(); midi.setRPN(g, 1, 300, 1000);", "local g = midi.createRPN(); midi.setRPN(g, 1, 300, 1000)",
		  { {0xb4, 101, 2}, {0xb4, 100, 44}, {0xb4, 6, 39}, {0xb4, 38, 8} } },
		{ "14-bit CC", "let g = midi.createCc14bit(); midi.setCc14bit(g, 1, 5, 12864);", "local g = midi.createCc14bit(); midi.setCc14bit(g, 1, 5, 12864)",
		  { {0xb4, 5, 39}, {0xb4, 37, 8} } },
	};
	for (const Case& c : cases) {
		CATCH_INFO(c.name);
		// The channel is set between the group setter and setValue: it stays.
		EngineRun js = run(jsOnMessage(std::string(c.js) + " midi.setChannel(g, 5); midi.setValue(g, 5000); midiOut.send(g); rack.log(midi.getValue(g));"));
		EngineRun lua = run(luaOnMessage(std::string(c.lua) + "; midi.setChannel(g, 5); midi.setValue(g, 5000); midiOut.send(g); rack.log(midi.getValue(g))"));
		CATCH_INFO(js.log);
		CATCH_INFO(lua.log);
		REQUIRE(js.sent.size() == c.bytes.size());
		REQUIRE(lua.sent.size() == c.bytes.size());
		for (size_t i = 0; i < c.bytes.size(); i++) {
			REQUIRE(js.sent[i].bytes == c.bytes[i]);
			REQUIRE(lua.sent[i].bytes == c.bytes[i]);
		}
		REQUIRE(js.log.find("5000") != std::string::npos);
		REQUIRE(lua.log.find("5000") != std::string::npos);

		// Clamped to the 14-bit range, like the group setters.
		EngineRun big = run(jsOnMessage(std::string(c.js) + " midi.setValue(g, 99999); rack.log(midi.getValue(g));"));
		EngineRun bigLua = run(luaOnMessage(std::string(c.lua) + "; midi.setValue(g, 99999); rack.log(midi.getValue(g))"));
		REQUIRE(big.log.find("16383") != std::string::npos);
		REQUIRE(bigLua.log.find("16383") != std::string::npos);

		// A group whose setter has not run has no number to keep: an error.
		std::string create = c.js; create = create.substr(0, create.find(" midi.set"));
		std::string createLua = c.lua; createLua = createLua.substr(0, createLua.find("; midi.set"));
		EngineRun unset = run(jsOnMessage(create + " " + jsGuarded("midi.setValue(g, 5);")));
		EngineRun unsetLua = run(luaOnMessage(createLua + "; " + luaGuarded("midi.setValue(g, 5)")));
		CATCH_INFO(unset.log);
		CATCH_INFO(unsetLua.log);
		REQUIRE(unset.log.find("midi.setValue: message is a") != std::string::npos);
		REQUIRE(unsetLua.log.find("midi.setValue: message is a") != std::string::npos);
	}

	// A plain handle still takes the 7-bit data byte.
	EngineRun plain = run(jsOnMessage("let m = midi.create(); midi.setNoteOn(m, 1, 60, 100); midi.setValue(m, 300); midiOut.send(m);"));
	REQUIRE(plain.sent.size() == 1);
	REQUIRE(plain.sent[0].bytes[2] == 127);
}

TEST_CASE("A clone of a set group keeps its number and value, and is independent", "[MidiKit][CrossEngine]") {
	const char* js = "let g = midi.createNRPN(); midi.setNRPN(g, 1, 300, 1000); let c = midi.clone(g); midi.setValue(c, 7); rack.log([(midi.getType(c) === midi.NRPN), midi.getControl(c), midi.getValue(c), midi.getValue(g)].join(' '));";
	const char* lua = "local g = midi.createNRPN(); midi.setNRPN(g, 1, 300, 1000); local c = midi.clone(g); midi.setValue(c, 7); rack.log(table.concat({tostring(midi.getType(c) == midi.NRPN), midi.getControl(c), midi.getValue(c), midi.getValue(g)}, ' '))";
	EngineRun rjs = run(jsOnMessage(js));
	EngineRun rlua = run(luaOnMessage(lua));
	CATCH_INFO(rjs.log);
	CATCH_INFO(rlua.log);
	REQUIRE(rjs.log.find("true 300 7 1000") != std::string::npos);
	REQUIRE(rlua.log.find("true 300 7 1000") != std::string::npos);
}
// 14-bit CC and NRPN message construction and send order, the midi.is* predicates and midi.getChannel.
//
// Part of the cross-engine suite: the shared helpers (run, requireEquivalent, EngineRun, ...)
// are in MidiKit.test.hpp.


// setCc14bit on a createCc14bit() pair (atomic 2-message send)
// The two-handle form above sends two independent messages. A
// createCc14bit() pair is the atomic alternative: midiOut.send(cc14) sends
// both underlying CC messages in order when passed the first handle of the
// pair (per SCRIPTING.md), so sending it is what exercises the pair end to
// end.

static const char* JS_CC_14BIT_PAIR = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let cc14 = midi.createCc14bit();
    midi.setCc14bit(cc14, 8, 1, 12864);
    midiOut.send(cc14);
};
)";

static const char* LUA_CC_14BIT_PAIR = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local cc14 = midi.createCc14bit()
    midi.setCc14bit(cc14, 8, 1, 12864)
    midiOut.send(cc14)
end
)";

// Pinned wire bytes, since both engines agreeing could still both be wrong (the NRPN quad once
// went out MSB-after-LSB in both). 14-bit CC convention: CC 1 (value MSB), then CC 33 (value LSB).
// Channel 8 -> status/channel byte 0xb7; value 12864 -> MSB 100, LSB 64.
TEST_CASE("createCc14bit pair sends MSB before LSB, identically in both engines", "[MidiKit][CrossEngine]") {
	requireBytes(runBoth(Pair{JS_CC_14BIT_PAIR, LUA_CC_14BIT_PAIR}), {{0xb7, 1, 100}, {0xb7, 33, 64}});
}


// 14-bit CC pair send() order
// A 14-bit CC pair is sent as a unit when the group leader is sent. This
// verifies the send-order fix also applies across pairs: two pairs are
// created (p1, then p2) but sent in the opposite order (p2, then p1), and
// the wire must carry p2's whole pair before p1's whole pair — the pairs
// are ordered by send() call, not by handle-creation order, and never
// interleaved.

static const char* JS_CC_14BIT_SEND_ORDER = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let p1 = midi.createCc14bit();
    midi.setCc14bit(p1, 9, 1, 12864);
    let p2 = midi.createCc14bit();
    midi.setCc14bit(p2, 9, 2, 448);
    midiOut.send(p2);   // created second, sent first
    midiOut.send(p1);   // created first, sent last
};
)";

static const char* LUA_CC_14BIT_SEND_ORDER = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local p1 = midi.createCc14bit()
    midi.setCc14bit(p1, 9, 1, 12864)
    local p2 = midi.createCc14bit()
    midi.setCc14bit(p2, 9, 2, 448)
    midiOut.send(p2)
    midiOut.send(p1)
end
)";

TEST_CASE("14-bit CC pairs are sent in send() order, not handle-creation order, in both engines", "[MidiKit][CrossEngine]") {
	EngineRun js = run(JS_CC_14BIT_SEND_ORDER);
	EngineRun lua = run(LUA_CC_14BIT_SEND_ORDER);

	// channel 9 -> status/channel byte 0xb8; p1 (cc=1, value=100.5) -> CC1=100, CC33=64
	std::vector<uint8_t> p1m0 = {0xb8, 1, 100};
	std::vector<uint8_t> p1m1 = {0xb8, 33, 64};
	// p2 (cc=2, value=3.5) -> CC2=3, CC34=64
	std::vector<uint8_t> p2m0 = {0xb8, 2, 3};
	std::vector<uint8_t> p2m1 = {0xb8, 34, 64};

	// Handle order would be p1's pair then p2's; send() order is p2 then p1.
	std::vector<std::vector<uint8_t>> expect = {p2m0, p2m1, p1m0, p1m1};

	REQUIRE(js.sent.size() == 4);
	for (size_t i = 0; i < expect.size(); i++) {
		REQUIRE(js.sent[i].bytes == expect[i]);
	}

	REQUIRE(lua.sent.size() == 4);
	for (size_t i = 0; i < expect.size(); i++) {
		REQUIRE(lua.sent[i].bytes == expect[i]);
	}
}



// NRPN send() order
// An NRPN is a quad of 4 CC messages that are sent as a unit when the group
// leader is sent. This verifies that the send-order fix also applies across
// NRPN groups: two NRPNs are created (n1, then n2) but sent in the opposite
// order (n2, then n1), and the wire must carry n2's whole quad before n1's
// whole quad — i.e. the groups are ordered by send() call, not by
// handle-creation order.

static const char* JS_NRPN_SEND_ORDER = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let n1 = midi.createNRPN();
    midi.setNRPN(n1, 9, 1234, 5678);
    let n2 = midi.createNRPN();
    midi.setNRPN(n2, 9, 100, 200);
    midiOut.send(n2);   // created second, sent first
    midiOut.send(n1);   // created first, sent last
};
)";

static const char* LUA_NRPN_SEND_ORDER = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local n1 = midi.createNRPN()
    midi.setNRPN(n1, 9, 1234, 5678)
    local n2 = midi.createNRPN()
    midi.setNRPN(n2, 9, 100, 200)
    midiOut.send(n2)
    midiOut.send(n1)
end
)";

TEST_CASE("NRPN quads are sent in send() order, not handle-creation order, in both engines", "[MidiKit][CrossEngine]") {
	EngineRun js = run(JS_NRPN_SEND_ORDER);
	EngineRun lua = run(LUA_NRPN_SEND_ORDER);

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


// is* type predicates
// One message per status byte, each checked against every is* predicate and
// the results logged as a single ordered bit string — this is the shape of
// the per-engine "API midi.is* type check" case, just funneled through the
// same PROBE_PREFIX log channel used for the number.* tests above instead of
// engine-internal js_eval/lua_getglobal readbacks.

// ── midi.getType and the type constants ─────────────────────────────────────
// One table for both engines: each case builds a message, logs "T:<type>:<value>"
// (getType and getValue) and the test looks for the expected text. A case with no
// value in `expected` ends at the colon, for messages whose data byte is not defined.

TEST_CASE("midi.getType and getValue answer for every message type", "[MidiKit][CrossEngine]") {
	struct Case { const char* js; const char* lua; const char* expected; };
	const Case cases[] = {
		{"let m = midi.create(); midi.setRaw(m, \"903c64\");", "local m = midi.create() midi.setRaw(m, \"903c64\")", "T:noteOn:100"},
		{"let m = midi.create(); midi.setRaw(m, \"903c00\");", "local m = midi.create() midi.setRaw(m, \"903c00\")", "T:noteOff:0"},
		{"let m = midi.create(); midi.setRaw(m, \"803c40\");", "local m = midi.create() midi.setRaw(m, \"803c40\")", "T:noteOff:64"},
		{"let m = midi.create(); midi.setRaw(m, \"903c\");", "local m = midi.create() midi.setRaw(m, \"903c\")", "T:noteOn:"},
		{"let m = midi.create(); midi.setRaw(m, \"a03c28\");", "local m = midi.create() midi.setRaw(m, \"a03c28\")", "T:keyPressure:40"},
		{"let m = midi.create(); midi.setCc(m, 1, 7, 100);", "local m = midi.create() midi.setCc(m, 1, 7, 100)", "T:cc:100"},
		{"let m = midi.create(); midi.setProgramChange(m, 1, 5);", "local m = midi.create() midi.setProgramChange(m, 1, 5)", "T:programChange:"},
		{"let m = midi.create(); midi.setChanPressure(m, 1, 64);", "local m = midi.create() midi.setChanPressure(m, 1, 64)", "T:chanPressure:"},
		{"let m = midi.create(); midi.setPitchWheel(m, 1, 8192);", "local m = midi.create() midi.setPitchWheel(m, 1, 8192)", "T:pitchWheel:"},
		{"let m = midi.create(); midi.setSysEx(m, \"43104c0000\");", "local m = midi.create() midi.setSysEx(m, \"43104c0000\")", "T:sysEx:"},
		// System common: F1 and F3 answer their data byte, F2 the 14-bit position (LSB first on the wire).
		{"let m = midi.create(); midi.setRaw(m, \"f137\");", "local m = midi.create() midi.setRaw(m, \"f137\")", "T:mtcQuarterFrame:55"},
		{"let m = midi.create(); midi.setRaw(m, \"f20102\");", "local m = midi.create() midi.setRaw(m, \"f20102\")", "T:songPosition:257"},
		{"let m = midi.create(); midi.setRaw(m, \"f27f7f\");", "local m = midi.create() midi.setRaw(m, \"f27f7f\")", "T:songPosition:16383"},
		{"let m = midi.create(); midi.setRaw(m, \"f303\");", "local m = midi.create() midi.setRaw(m, \"f303\")", "T:songSelect:3"},
		{"let m = midi.create(); midi.setRaw(m, \"f6\");", "local m = midi.create() midi.setRaw(m, \"f6\")", "T:tuneRequest:"},
		{"let m = midi.create(); midi.setRaw(m, \"f8\");", "local m = midi.create() midi.setRaw(m, \"f8\")", "T:clock:"},
		{"let m = midi.create(); midi.setRaw(m, \"fa\");", "local m = midi.create() midi.setRaw(m, \"fa\")", "T:start:"},
		{"let m = midi.create(); midi.setRaw(m, \"fb\");", "local m = midi.create() midi.setRaw(m, \"fb\")", "T:continue:"},
		{"let m = midi.create(); midi.setRaw(m, \"fc\");", "local m = midi.create() midi.setRaw(m, \"fc\")", "T:stop:"},
		{"let m = midi.create(); midi.setRaw(m, \"fe\");", "local m = midi.create() midi.setRaw(m, \"fe\")", "T:activeSensing:"},
		{"let m = midi.create(); midi.setRaw(m, \"ff\");", "local m = midi.create() midi.setRaw(m, \"ff\")", "T:reset:"},
		{"let m = midi.create(); midi.setRaw(m, \"f4\");", "local m = midi.create() midi.setRaw(m, \"f4\")", "T:unknown:"},
		{"let m = midi.create(); midi.setRaw(m, \"fd\");", "local m = midi.create() midi.setRaw(m, \"fd\")", "T:unknown:"},
		// Nothing set yet.
		{"let m = midi.create();", "local m = midi.create()", "T:none:"},
		// Groups answer for the group, never as CC.
		{"let m = midi.createNRPN(); midi.setNRPN(m, 1, 300, 1000);", "local m = midi.createNRPN() midi.setNRPN(m, 1, 300, 1000)", "T:nrpn:1000"},
		{"let m = midi.createRPN(); midi.setRPN(m, 1, 0, 256);", "local m = midi.createRPN() midi.setRPN(m, 1, 0, 256)", "T:rpn:256"},
		{"let m = midi.createCc14bit(); midi.setCc14bit(m, 1, 7, 12345);", "local m = midi.createCc14bit() midi.setCc14bit(m, 1, 7, 12345)", "T:cc14bit:12345"},
		{"let m = midi.createNRPN();", "local m = midi.createNRPN()", "T:none:"},
	};
	for (const Case& c : cases) {
		for (bool lua : {false, true}) {
			Kit<> kit;
			MidiKitModule* m = kit.m;
			std::string body = lua ? c.lua : c.js;
			m->loadScript(lua
				? "--[[\n@engine minilua@v1\n--]]\nrack.onLoad = function()\n" + body + "\nrack.log('T:' .. midi.getType(m) .. ':' .. midi.getValue(m))\nend\n"
				: "/**\n * @engine QuickJs@v1\n */\nrack.onLoad = function() {\n" + body + "\nrack.log('T:' + midi.getType(m) + ':' + midi.getValue(m));\n};\n");
			std::string log = drainLog(m);
			CATCH_INFO(std::string(lua ? "Lua: " : "JS: ") + c.expected + "\n" + log);
			REQUIRE(log.find(c.expected) != std::string::npos);
		}
	}
}

// A Note-On with velocity 0 is a noteOff but its bytes do not change, so forwarding
// it still sends 9n nn 00.
static const char* JS_TYPE_CONSTANTS = R"(/**
 * @engine QuickJs@v1
 */
rack.onLoad = function() {
    let m = midi.create(); midi.setNoteOn(m, 1, 60, 0);
    rack.log("PROBE:" + (midi.getType(m) === midi.NOTE_OFF) + ":" + midi.getRaw(m));
    midi.CC = "changed";
    rack.log("PROBE:" + midi.CC + ":" + midi.NOTE_ON + ":" + midi.CC14BIT + ":" + midi.ACTIVE_SENSING + ":" + midi.NONE);
    rack.log("PROBE:" + (midi.isNoteOn === undefined) + ":" + (midi.isNoteRelease === undefined) + ":" + (midi.isCc === undefined) + ":" + (midi.isClock === undefined));
};
)";

static const char* LUA_TYPE_CONSTANTS = R"(--[[
@engine minilua@v1
--]]
rack.onLoad = function()
    local m = midi.create(); midi.setNoteOn(m, 1, 60, 0)
    rack.log("PROBE:" .. tostring(midi.getType(m) == midi.NOTE_OFF) .. ":" .. midi.getRaw(m))
    rack.log("PROBE:" .. midi.CC .. ":" .. midi.NOTE_ON .. ":" .. midi.CC14BIT .. ":" .. midi.ACTIVE_SENSING .. ":" .. midi.NONE)
    rack.log("PROBE:" .. tostring(midi.isNoteOn == nil) .. ":" .. tostring(midi.isNoteRelease == nil) .. ":" .. tostring(midi.isCc == nil) .. ":" .. tostring(midi.isClock == nil))
end
)";

TEST_CASE("midi type constants are strings, JS ones are read-only, the is* predicates are gone", "[MidiKit][CrossEngine]") {
	requireLoggedValues(JS_TYPE_CONSTANTS, LUA_TYPE_CONSTANTS, {"true:903c00", "cc:noteOn:cc14bit:activeSensing:none", "true:true:true:true"});
}

static const char* JS_GET_TYPE_BAD = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) { midi.getType(); };
)";

static const char* LUA_GET_TYPE_BAD = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(port, msg) midi.getType() end
)";

TEST_CASE("midi.getType rejects a missing message", "[MidiKit][CrossEngine]") {
	requireEquivalentLog(JS_GET_TYPE_BAD, LUA_GET_TYPE_BAD, "getType", true);
}

// midi.onMessage gets the type of the incoming message as its third argument.
TEST_CASE("midi.onMessage passes the message type as third argument", "[MidiKit][CrossEngine]") {
	struct Case { std::vector<uint8_t> bytes; const char* expected; };
	const Case cases[] = {
		{ {0x90, 60, 100}, "T:noteOn:noteOn" },
		{ {0x90, 60, 0}, "T:noteOff:noteOff" },
		{ {0xb0, 7, 64}, "T:cc:cc" },
		{ {0xfe}, "T:activeSensing:activeSensing" },
	};
	for (const Case& c : cases) {
		for (bool lua : {false, true}) {
			Kit<> kit;
			MidiKitModule* m = kit.m;
			m->loadScript(lua
				? "--[[\n@engine minilua@v1\n--]]\nmidi.onMessage = function(port, msg, msgType)\nrack.log('T:' .. msgType .. ':' .. midi.getType(msg))\nend\n"
				: "/**\n * @engine QuickJs@v1\n */\nmidi.onMessage = function(port, msg, msgType) {\nrack.log('T:' + msgType + ':' + midi.getType(msg));\n};\n");
			midi::Message msg;
			msg.bytes = c.bytes;
			m->host.getActiveEngine()->processInMessage(0, QueuedMessage(msg));
			m->host.getActiveEngine()->process();
			std::string log = drainLog(m);
			CATCH_INFO(std::string(lua ? "Lua: " : "JS: ") + c.expected + "\n" + log);
			REQUIRE(log.find(c.expected) != std::string::npos);
		}
	}
}


// midi.getChannel on a realtime/SysEx message
// Status 0xf (clock, start/stop/continue, SysEx) has no channel. getChannel()
// used to return the low status nibble + 1 — a plausible-looking but
// meaningless number, since that nibble is a sub-type selector, not a
// channel — which made e.g. a clock tick misread as "channel 9". It now
// returns -1 for that family, and the real 1-16 channel otherwise.

static const char* JS_GET_CHANNEL_SENTINEL = R"(/**
 * @engine QuickJs@v1
 */
rack.onLoad = function() {
    let note = midi.create();
    midi.setNoteOn(note, 5, 60, 100);
    rack.log("PROBE:" + number.toString(midi.getChannel(note)));

    let clock = midi.create();
    midi.setSysEx(clock, "");
    rack.log("PROBE:" + number.toString(midi.getChannel(clock)));
};
)";

static const char* LUA_GET_CHANNEL_SENTINEL = R"(--[[
@engine minilua@v1
--]]
rack.onLoad = function()
    local note = midi.create()
    midi.setNoteOn(note, 5, 60, 100)
    rack.log("PROBE:" .. number.toString(midi.getChannel(note)))

    local clock = midi.create()
    midi.setSysEx(clock, "")
    rack.log("PROBE:" .. number.toString(midi.getChannel(clock)))
end
)";

TEST_CASE("midi.getChannel returns -1 on realtime/SysEx, the real channel otherwise", "[MidiKit][CrossEngine]") {
	requireLoggedValues(JS_GET_CHANNEL_SENTINEL, LUA_GET_CHANNEL_SENTINEL, {"5", "-1"});
}


// Hex-string setters with an invalid string long enough to need heap storage:
// the error must be raised cleanly in both engines, and the valid call after it
// still works.
TEST_CASE("setRaw and setSysEx reject long invalid hex strings, and a valid one still works", "[MidiKit][CrossEngine]") {
	FOR_EACH_LANG;
	const char* oddLong = "43104c000043104";     // odd length
	const char* nonHex = "43104c00004310zz43104c";
	const char* tooWide = "43104c0000ff43104c0000";   // 0xff is not a 7-bit byte
	std::string calls[] = { "setSysEx", "setRaw" };
	std::string body;
	for (const std::string& call : calls) {
		for (const char* bad : { oddLong, nonHex }) {
			if (lang == Lang::Js) body += "    t('" + call + "', function() { midi." + call + "(m, '" + bad + "'); });\n";
			else body += "    t('" + call + "', function() midi." + call + "(m, '" + bad + "') end)\n";
		}
	}
	if (lang == Lang::Js) body += std::string("    t('wide', function() { midi.setSysEx(m, '") + tooWide + "'); });\n";
	else body += std::string("    t('wide', function() midi.setSysEx(m, '") + tooWide + "') end)\n";
	body += lang == Lang::Js ? "    midi.setSysEx(m, '43104c0000'); midiOut.send(m);\n" : "    midi.setSysEx(m, '43104c0000') midiOut.send(m)\n";

	std::string src = lang == Lang::Js
		? script(lang, "function t(name, f) { try { f(); rack.log('ok ' + name); } catch (e) { rack.log('err ' + name); } }\n"
			"midi.onMessage = function(port, msg) {\n    let m = midi.create();\n" + body + "};")
		: script(lang, "local function t(name, f) local ok = pcall(f) rack.log((ok and 'ok ' or 'err ') .. name) end\n"
			"midi.onMessage = function(port, msg)\n    local m = midi.create()\n" + body + "end");

	Kit<> kit;
	kit.loadRaw(src);
	std::vector<Out> sent = kit.dispatch(msg::noteOn(1, 60, 100));
	std::string log = kit.log();
	CATCH_INFO("log:\n" << log);
	REQUIRE(countOf(log, "err setSysEx") == 2);
	REQUIRE(countOf(log, "err setRaw") == 2);
	REQUIRE(countOf(log, "err wide") == 1);
	REQUIRE(log.find("ok ") == std::string::npos);
	REQUIRE(sent.size() == 1);
	REQUIRE(sent[0].bytes == std::vector<uint8_t>{0xf0, 0x43, 0x10, 0x4c, 0x00, 0x00, 0xf7});
}

// ── midi.toString ────────────────────────────────────────────────────────────
// One line in the wording of MIDI-MON (MidiText), the same for both engines. The
// script builds each message, logs toString() and the test compares the lines.

TEST_CASE("midi.toString uses the MIDI-MON wording", "[MidiKit][CrossEngine]") {
	struct Case { const char* js; const char* lua; const char* expected; };
	const Case cases[] = {
		{"let m = midi.create(); midi.setNoteOn(m, 1, 60, 100); rack.log(midi.toString(m));",
		 "local m = midi.create() midi.setNoteOn(m, 1, 60, 100) rack.log(midi.toString(m))", "ch01 note on  60 vel 100"},
		{"let m = midi.create(); midi.setNoteOn(m, 1, 60, 0); rack.log(midi.toString(m));",
		 "local m = midi.create() midi.setNoteOn(m, 1, 60, 0) rack.log(midi.toString(m))", "ch01 note off 60 vel 0"},
		{"let m = midi.create(); midi.setCc(m, 2, 7, 100); rack.log(midi.toString(m));",
		 "local m = midi.create() midi.setCc(m, 2, 7, 100) rack.log(midi.toString(m))", "ch02 cc7=100"},
		{"let m = midi.create(); midi.setRaw(m, \"fe\"); rack.log(midi.toString(m));",
		 "local m = midi.create() midi.setRaw(m, \"fe\") rack.log(midi.toString(m))", "active sensing"},
		{"let m = midi.create(); midi.setRaw(m, \"f6\"); rack.log(midi.toString(m));",
		 "local m = midi.create() midi.setRaw(m, \"f6\") rack.log(midi.toString(m))", "tune request"},
		{"let m = midi.create(); midi.setRaw(m, \"f137\"); rack.log(midi.toString(m));",
		 "local m = midi.create() midi.setRaw(m, \"f137\") rack.log(midi.toString(m))", "mtc quarter frame piece=3 value=7"},
		{"let m = midi.create(); midi.setRaw(m, \"f8\"); rack.log(midi.toString(m));",
		 "local m = midi.create() midi.setRaw(m, \"f8\") rack.log(midi.toString(m))", "clock tick"},
		{"let m = midi.create(); midi.setRaw(m, \"f0010203f7\"); rack.log(midi.toString(m));",
		 "local m = midi.create() midi.setRaw(m, \"f0010203\" .. \"f7\") rack.log(midi.toString(m))", "sysex (3 data bytes) 01 02 03"},
		{"let m = midi.create(); midi.setRaw(m, \"f0\" + \"01\".repeat(33) + \"f7\"); rack.log(midi.toString(m));",
		 "local m = midi.create() midi.setRaw(m, \"f0\" .. string.rep(\"01\", 33) .. \"f7\") rack.log(midi.toString(m))",
		 "sysex (33 data bytes) 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 \xe2\x80\xa6"},
		{"let m = midi.createRPN(); rack.log(midi.toString(m));",
		 "local m = midi.createRPN() rack.log(midi.toString(m))", "(empty)"},
		{"let m = midi.createNRPN(); rack.log(midi.toString(m));",
		 "local m = midi.createNRPN() rack.log(midi.toString(m))", "(empty)"},
		{"let m = midi.createNRPN(); midi.setNRPN(m, 1, 1234, 16383); rack.log(midi.toString(m));",
		 "local m = midi.createNRPN() midi.setNRPN(m, 1, 1234, 16383) rack.log(midi.toString(m))", "ch01 nrpn param=1234 value=16383"},
		{"let m = midi.createRPN(); midi.setRPN(m, 1, 0, 256); rack.log(midi.toString(m));",
		 "local m = midi.createRPN() midi.setRPN(m, 1, 0, 256) rack.log(midi.toString(m))", "ch01 rpn param=0 value=256"},
		{"let m = midi.createCc14bit(); midi.setCc14bit(m, 1, 7, 12345); rack.log(midi.toString(m));",
		 "local m = midi.createCc14bit() midi.setCc14bit(m, 1, 7, 12345) rack.log(midi.toString(m))", "ch01 14-bit cc7=12345"},
	};
	for (const Case& c : cases) {
		for (bool lua : {false, true}) {
			Kit<> kit;
			MidiKitModule* m = kit.m;
			std::string body = lua ? c.lua : c.js;
			m->loadScript(lua
				? "--[[\n@engine minilua@v1\n--]]\nrack.onLoad = function()\n" + body + "\nend\n"
				: "/**\n * @engine QuickJs@v1\n */\nrack.onLoad = function() {\n" + body + "\n};\n");
			std::string log = drainLog(m);
			REQUIRE(log.find(c.expected) != std::string::npos);
		}
	}
}
