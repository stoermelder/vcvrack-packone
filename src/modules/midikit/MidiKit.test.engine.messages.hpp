// Wire bytes of the simple message setters (note, CC, pitch wheel, program change, pressure, SysEx, raw), clone, and the sendAfterTrigger argument forms.
//
// Part of the cross-engine suite: included into the __engine namespace by
// MidiKit.test.cpp after MidiKit.test.engine.hpp, which defines the shared helpers.

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
	requireEquivalent(JS_NOTE_ON, LUA_NOTE_ON);
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
    if (midi.isCc(msg)) {
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
	requireEquivalent(JS_CC, LUA_CC);
}


// setCc clamping (documented: value clamped to 0-127)

static const char* JS_CC_CLAMP = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let out = midi.create();
    midi.setCc(out, 1, 10, 500);
    midiOut.send(out);
};
)";

static const char* LUA_CC_CLAMP = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local out = midi.create()
    midi.setCc(out, 1, 10, 500)
    midiOut.send(out)
end
)";

TEST_CASE("setCc clamps out-of-range value identically", "[MidiKit][CrossEngine]") {
	requireEquivalent(JS_CC_CLAMP, LUA_CC_CLAMP);
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
	EngineResult js = run(jsOnMessage("trig.setGate(1, 2, 5, 9);"));
	EngineResult lua = run(luaOnMessage("trig.setGate(1, 2, 5, 9)"));
	CATCH_INFO(js.log);
	CATCH_INFO(lua.log);
	REQUIRE(js.log.find("trig.setGate") != std::string::npos);
	REQUIRE(lua.log.find("trig.setGate") != std::string::npos);
}

TEST_CASE("QuickJS trig.setHigh/setLow/setTrigger errors name their own function", "[MidiKit]") {
	for (const char* fn : {"setHigh", "setLow", "setTrigger"}) {
		EngineResult js = run(jsOnMessage(std::string("trig.") + fn + "(\"x\");"));
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
	requireEquivalent(JS_SYSEX, LUA_SYSEX);
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


// sendAfterTrigger (finding #7: 3-arg form)
// Regression coverage for the actual bug in #7: Lua used to misread the
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
	requireEquivalent(JS_SEND_AFTER_TRIGGER, LUA_SEND_AFTER_TRIGGER);
}


// header-tag-only script
// A script whose header carries only @engine and nothing else must load in
// both engines — #13 was QuickJs-only failing on this exact shape.

static const char* JS_HEADER_ONLY = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let out = midi.create();
    midi.setNoteOn(out, 1, 60, 100);
    midiOut.send(out);
};
)";

static const char* LUA_HEADER_ONLY = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local out = midi.create()
    midi.setNoteOn(out, 1, 60, 100)
    midiOut.send(out)
end
)";

TEST_CASE("@engine-only header loads identically in both engines", "[MidiKit][CrossEngine]") {
	requireEquivalent(JS_HEADER_ONLY, LUA_HEADER_ONLY);
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
	requireEquivalent(JS_RAW, LUA_RAW);
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
	requireEquivalent(JS_PITCH_WHEEL, LUA_PITCH_WHEEL);
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
	requireEquivalent(JS_PROGRAM_CHANGE, LUA_PROGRAM_CHANGE);

	// Literal bytes too: equivalence alone passes with both engines wrong.
	for (const char* script : {JS_PROGRAM_CHANGE, LUA_PROGRAM_CHANGE}) {
		CATCH_INFO(script);
		EngineResult r = run(script);
		REQUIRE(r.sent.size() == 1);
		REQUIRE(r.sent[0].bytes == std::vector<uint8_t>{0xc3, 10});
	}
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
	requireEquivalent(JS_CHAN_PRESSURE, LUA_CHAN_PRESSURE);

	for (const char* script : {JS_CHAN_PRESSURE, LUA_CHAN_PRESSURE}) {
		CATCH_INFO(script);
		EngineResult r = run(script);
		REQUIRE(r.sent.size() == 1);
		REQUIRE(r.sent[0].bytes == std::vector<uint8_t>{0xd4, 80});
	}
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
	requireEquivalent(JS_KEY_PRESSURE, LUA_KEY_PRESSURE);
}


// setKeyPressure clamping

static const char* JS_KEY_PRESSURE_CLAMP = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let outHigh = midi.create();
    midi.setKeyPressure(outHigh, 6, 64, 200);
    midiOut.send(outHigh);
    let outLow = midi.create();
    midi.setKeyPressure(outLow, 6, 64, -1);
    midiOut.send(outLow);
};
)";

static const char* LUA_KEY_PRESSURE_CLAMP = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
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


// setNoteOn clamping

static const char* JS_NOTE_ON_CLAMP = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let outHigh = midi.create();
    midi.setNoteOn(outHigh, 1, 60, 200);
    midiOut.send(outHigh);
    let outLow = midi.create();
    midi.setNoteOn(outLow, 1, 60, -1);
    midiOut.send(outLow);
};
)";

static const char* LUA_NOTE_ON_CLAMP = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
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


// Setter arguments: rounded to an integer and clamped, never wrapped (review 1.3).
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
		EngineResult js = run(jsOnMessage("let m = midi.create(); " + body));
		EngineResult lua = run(luaOnMessage("local m = midi.create(); " + body));
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
		EngineResult js = run(jsOnMessage(std::string("let m = midi.createCc14bit(); midi.setCc14bit(m, 1, ") + c.cc + ", 64.5); midiOut.send(m);"));
		EngineResult lua = run(luaOnMessage(std::string("local m = midi.createCc14bit(); midi.setCc14bit(m, 1, ") + c.cc + ", 64.5); midiOut.send(m)"));
		CATCH_INFO(js.log);
		CATCH_INFO(lua.log);
		REQUIRE(js.sent.size() == 2);
		REQUIRE(lua.sent.size() == 2);
		for (EngineResult* r : {&js, &lua}) {
			REQUIRE(r->sent[0].bytes == msbBytes);
			REQUIRE(r->sent[1].bytes == lsbBytes);
		}

		// Two independent handles.
		EngineResult js2 = run(jsOnMessage(std::string("let a = midi.create(); let b = midi.create(); midi.setCc14bit(a, b, 1, ") + c.cc + ", 64.5); midiOut.send(a); midiOut.send(b);"));
		EngineResult lua2 = run(luaOnMessage(std::string("local a = midi.create(); local b = midi.create(); midi.setCc14bit(a, b, 1, ") + c.cc + ", 64.5); midiOut.send(a); midiOut.send(b)"));
		CATCH_INFO(js2.log);
		CATCH_INFO(lua2.log);
		REQUIRE(js2.sent.size() == 2);
		REQUIRE(lua2.sent.size() == 2);
		for (EngineResult* r : {&js2, &lua2}) {
			REQUIRE(r->sent[0].bytes == msbBytes);
			REQUIRE(r->sent[1].bytes == lsbBytes);
		}
	}
}

TEST_CASE("setCc14bit clamps its value to 7-bit data bytes", "[MidiKit][CrossEngine]") {
	for (const char* v : {"1000", "-5"}) {
		std::string args = std::string("midi.setCc14bit(m, 1, 1, ") + v + ");";
		EngineResult js = run(jsOnMessage("let m = midi.createCc14bit(); " + args + " midiOut.send(m);"));
		EngineResult lua = run(luaOnMessage("local m = midi.createCc14bit(); " + args + " midiOut.send(m)"));
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
	EngineResult js = run(jsOnMessage("let m = midi.createRPN(); " + call));
	EngineResult lua = run(luaOnMessage("local m = midi.createRPN(); " + call));
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

TEST_CASE("setNRPN and setRPN reject each other's handles", "[MidiKit][CrossEngine]") {
	struct Case { const char* create; const char* set; const char* error; };
	const Case cases[] = {
		{"midi.createRPN()", "midi.setNRPN", "not an NRPN"},
		{"midi.createNRPN()", "midi.setRPN", "not an RPN"},
	};
	for (const Case& c : cases) {
		std::string call = std::string(c.set) + "(m, 1, 0, 0);";
		EngineResult lua = run(luaOnMessage(std::string("local m = ") + c.create + "; " + call));
		CATCH_INFO(lua.log);
		REQUIRE(lua.log.find(c.error) != std::string::npos);
		// JS names the same misuse "invalid nrpn/rpn message".
		EngineResult js = run(jsOnMessage(std::string("let m = ") + c.create + "; " + call));
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
		EngineResult js = run(jsOnMessage(std::string("rack.log(") + c.expr + ");"));
		EngineResult lua = run(luaOnMessage(std::string("rack.log(") + c.expr + ")"));
		CATCH_INFO(js.log);
		CATCH_INFO(lua.log);
		REQUIRE(js.log.find(c.expected) != std::string::npos);
		REQUIRE(lua.log.find(c.expected) != std::string::npos);
	}

	// Usable with sendAtFrame: 10 ms after the event is still the event's frame
	// plus 441, so the message is sent (identically in both engines).
	requireEquivalent(
		jsOnMessage("let m = midi.create(); midi.setNoteOn(m, 1, 60, 100); midiOut.sendAtFrame(m, rack.getEventFrame() + rack.msToFrames(10));"),
		luaOnMessage("local m = midi.create(); midi.setNoteOn(m, 1, 60, 100); midiOut.sendAtFrame(m, rack.getEventFrame() + rack.msToFrames(10))"));

	// Non-numbers are rejected rather than treated as 0.
	for (const char* fn : {"msToFrames", "framesToMs"}) {
		EngineResult js = run(jsOnMessage(std::string("rack.") + fn + "(\"x\");"));
		EngineResult lua = run(luaOnMessage(std::string("rack.") + fn + "(\"x\")"));
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
// cloning the incoming message (handle 0) is the review's D8 idiom — "send
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
	requireEquivalent(JS_MIDI_CLONE, LUA_MIDI_CLONE);
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
	requireEquivalent(JS_MIDI_CLONE_INCOMING, LUA_MIDI_CLONE_INCOMING);
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
	requireEquivalent(JS_NOTE_OFF, LUA_NOTE_OFF);
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

TEST_CASE("setNoteOff with velocity produces identical wire bytes", "[MidiKit][CrossEngine]") {
	requireEquivalent(JS_NOTE_OFF_VEL, LUA_NOTE_OFF_VEL);
}

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

TEST_CASE("setNoteOff velocity round-trips via getValue and clamps", "[MidiKit][CrossEngine]") {
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
	{ "14-bit CC", "let g = midi.createCc14bit(); midi.setCc14bit(g, 1, 5, 100.5);",
	               "local g = midi.createCc14bit(); midi.setCc14bit(g, 1, 5, 100.5)",
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
		EngineResult base = run(jsOnMessage(std::string(k.js) + " midiOut.send(g);"));
		EngineResult js = run(jsOnMessage(std::string(k.js) + " midi.setChannel(g, 5); midiOut.send(g);"));
		EngineResult lua = run(luaOnMessage(std::string(k.lua) + "; midi.setChannel(g, 5); midiOut.send(g)"));
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

		EngineResult js = run(jsOnMessage(head + " midi.setChannel(g, 5);" + setter + " midiOut.send(g);"));
		EngineResult lua = run(luaOnMessage(luaHead + "; midi.setChannel(g, 5)" + luaSetter + "; midiOut.send(g)"));
		CATCH_INFO(js.log);
		CATCH_INFO(lua.log);
		REQUIRE(js.log.empty());
		REQUIRE(lua.log.empty());
		REQUIRE(js.sent.size() == k.size);
		REQUIRE(js.sent == lua.sent);
		for (const auto& m : js.sent) REQUIRE(m.bytes[0] == 0xb0);

		// On a group that was never set, it raises nothing.
		EngineResult unsetJs = run(jsOnMessage(head + " midi.setChannel(g, 5);"));
		EngineResult unsetLua = run(luaOnMessage(luaHead + "; midi.setChannel(g, 5)"));
		REQUIRE(unsetJs.log.empty());
		REQUIRE(unsetLua.log.empty());
	}
}

TEST_CASE("Single-message setters raise an error on a group handle and leave it unchanged", "[MidiKit][CrossEngine]") {
	// Each call is valid on a plain handle. `p` is a plain handle.
	struct Setter { const char* fn; const char* args; };
	const Setter setters[] = {
		{ "setValue", "g, 1" },
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
		EngineResult base = run(jsOnMessage(std::string(k.js) + " midiOut.send(g);"));
		REQUIRE(base.sent.size() == k.size);
		for (const Setter& s : setters) {
			CATCH_INFO(std::string(k.name) + " " + s.fn + "(" + s.args + ")");
			std::string call = std::string("midi.") + s.fn + "(" + s.args + ");";
			std::string expected = std::string("midi.") + s.fn + ": " + k.error;

			EngineResult js = run(jsOnMessage(std::string(k.js) + " let p = midi.create(); " + jsGuarded(call) + " midiOut.send(g);"));
			EngineResult lua = run(luaOnMessage(std::string(k.lua) + "; local p = midi.create(); " + luaGuarded(call) + "; midiOut.send(g)"));
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
	EngineResult js = run(jsOnMessage("midi.setChannel(msg, 3); midi.setValue(msg, 9); midi.setNote(msg, 61); midiOut.send(msg);"));
	EngineResult lua = run(luaOnMessage("midi.setChannel(msg, 3); midi.setValue(msg, 9); midi.setNote(msg, 61); midiOut.send(msg)"));
	CATCH_INFO(js.log);
	CATCH_INFO(lua.log);
	REQUIRE(js.log.empty());
	REQUIRE(lua.log.empty());
	REQUIRE(js.sent.size() == 1);
	REQUIRE(js.sent == lua.sent);
	REQUIRE((js.sent[0].bytes[0] & 0x0f) == 2);
	REQUIRE(js.sent[0].bytes[1] == 61);
	REQUIRE(js.sent[0].bytes[2] == 9);

	EngineResult plainJs = run(jsOnMessage("let m = midi.create(); midi.setNoteOn(m, 1, 60, 100); midi.setChannel(m, 4); midi.setNote(m, 62); midi.setValue(m, 7); midiOut.send(m);"));
	EngineResult plainLua = run(luaOnMessage("local m = midi.create(); midi.setNoteOn(m, 1, 60, 100); midi.setChannel(m, 4); midi.setNote(m, 62); midi.setValue(m, 7); midiOut.send(m)"));
	REQUIRE(plainJs.log.empty());
	REQUIRE(plainJs.sent == plainLua.sent);
	REQUIRE(plainJs.sent.size() == 1);
	REQUIRE(plainJs.sent[0].bytes == std::vector<uint8_t>({0x93, 62, 7}));
}
