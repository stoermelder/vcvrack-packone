// midiOut.cancel(): the address rules (MidiScriptTypes.hpp), no module involved.
// The module side is further down this file ("Cancel: ...") and in MidiKit.test.lifecycle.hpp (script swap).
//
// Run alone: ./build/test/MidiKit.test "[Cancel]"

// A message with `size` bytes: status byte (type nibble | channel), then the data.
static midi::Message rawMsg(uint8_t status, int d1, int d2, size_t size = 3) {
	midi::Message m;
	m.bytes.assign(size, 0);
	if (size > 0) m.bytes[0] = status;
	if (size > 1) m.bytes[1] = uint8_t(d1);
	if (size > 2) m.bytes[2] = uint8_t(d2);
	return m;
}

static midi::Message ccMsg(int ch, int cc, int value) { return rawMsg(uint8_t(0xB0 | ch), cc, value); }

static bool same(const midi::Message& p, const midi::Message& m) { return sameAddress(p, m); }

TEST_CASE("Cancel: Note-On matches Note-On on the same channel and note, any velocity", "[MidiKit][Cancel]") {
	REQUIRE(same(noteOn(0, 60, 100), noteOn(0, 60, 1)));
	REQUIRE_FALSE(same(noteOn(0, 60, 100), noteOn(1, 60, 100)));
	REQUIRE_FALSE(same(noteOn(0, 60, 100), noteOn(0, 61, 100)));
}

TEST_CASE("Cancel: Note-Off matches Note-Off, in either encoding", "[MidiKit][Cancel]") {
	midi::Message vel0 = noteOn(0, 60, 0);
	REQUIRE(same(noteOff(0, 60), noteOff(0, 60)));
	REQUIRE(same(noteOff(0, 60), vel0));
	REQUIRE(same(vel0, noteOff(0, 60)));
	REQUIRE(same(vel0, noteOn(0, 60, 0)));
	// A different release velocity still addresses the same note.
	REQUIRE(same(noteOff(0, 60), rawMsg(0x80, 60, 64)));
	REQUIRE_FALSE(same(noteOff(0, 60), noteOff(1, 60)));
	REQUIRE_FALSE(same(noteOff(0, 60), noteOff(0, 61)));
}

TEST_CASE("Cancel: Note-On and Note-Off are separate addresses", "[MidiKit][Cancel]") {
	REQUIRE_FALSE(same(noteOn(0, 60, 100), noteOff(0, 60)));
	REQUIRE_FALSE(same(noteOff(0, 60), noteOn(0, 60, 100)));
	REQUIRE_FALSE(same(noteOn(0, 60, 100), noteOn(0, 60, 0)));
	REQUIRE_FALSE(same(noteOn(0, 60, 0), noteOn(0, 60, 100)));
}

TEST_CASE("Cancel: poly aftertouch and control change address a data byte, not the value", "[MidiKit][Cancel]") {
	REQUIRE(same(rawMsg(0xA0, 60, 10), rawMsg(0xA0, 60, 99)));
	REQUIRE_FALSE(same(rawMsg(0xA0, 60, 10), rawMsg(0xA0, 61, 10)));
	REQUIRE_FALSE(same(rawMsg(0xA0, 60, 10), rawMsg(0xA1, 60, 10)));

	REQUIRE(same(ccMsg(0, 7, 10), ccMsg(0, 7, 99)));
	REQUIRE_FALSE(same(ccMsg(0, 7, 10), ccMsg(0, 8, 10)));
	REQUIRE_FALSE(same(ccMsg(0, 7, 10), ccMsg(1, 7, 10)));
	// Different types never match, even with equal data bytes.
	REQUIRE_FALSE(same(ccMsg(0, 60, 10), rawMsg(0xA0, 60, 10)));
	REQUIRE_FALSE(same(ccMsg(0, 60, 10), noteOn(0, 60, 10)));
}

TEST_CASE("Cancel: program change, channel pressure and pitch bend address the channel only", "[MidiKit][Cancel]") {
	REQUIRE(same(rawMsg(0xC0, 5, 0, 2), rawMsg(0xC0, 99, 0, 2)));
	REQUIRE_FALSE(same(rawMsg(0xC0, 5, 0, 2), rawMsg(0xC1, 5, 0, 2)));
	REQUIRE(same(rawMsg(0xD2, 5, 0, 2), rawMsg(0xD2, 99, 0, 2)));
	REQUIRE(same(rawMsg(0xE3, 1, 2), rawMsg(0xE3, 100, 90)));
	REQUIRE_FALSE(same(rawMsg(0xE3, 1, 2), rawMsg(0xE4, 1, 2)));
	REQUIRE_FALSE(same(rawMsg(0xC0, 5, 0, 2), rawMsg(0xD0, 5, 0, 2)));
}

TEST_CASE("Cancel: any SysEx matches any SysEx, other system messages their status byte", "[MidiKit][Cancel]") {
	midi::Message a = rawMsg(0xF0, 1, 2, 6);
	a.bytes[5] = 0xF7;
	midi::Message b = rawMsg(0xF0, 9, 9, 3);
	REQUIRE(same(a, b));
	REQUIRE_FALSE(same(a, rawMsg(0xF8, 0, 0, 1)));

	REQUIRE(same(rawMsg(0xF8, 0, 0, 1), rawMsg(0xF8, 0, 0, 1)));
	REQUIRE_FALSE(same(rawMsg(0xF8, 0, 0, 1), rawMsg(0xFA, 0, 0, 1)));
	REQUIRE_FALSE(same(rawMsg(0xFA, 0, 0, 1), rawMsg(0xFC, 0, 0, 1)));
	// MTC quarter frame: the whole status byte, not its data.
	REQUIRE(same(rawMsg(0xF1, 0x10, 0, 2), rawMsg(0xF1, 0x20, 0, 2)));
	REQUIRE_FALSE(same(rawMsg(0xF1, 0x10, 0, 2), rawMsg(0xF2, 0x10, 0)));
	// A system message is no channel message of channel 0.
	REQUIRE_FALSE(same(rawMsg(0xF8, 0, 0, 1), noteOn(8, 0, 0)));
}

TEST_CASE("Cancel: a message without a status byte matches nothing", "[MidiKit][Cancel]") {
	midi::Message empty;   // a default midi::Message: three zero bytes
	midi::Message none;
	none.bytes.clear();
	REQUIRE_FALSE(same(empty, empty));
	REQUIRE_FALSE(same(empty, noteOn(0, 60, 100)));
	REQUIRE_FALSE(same(noteOn(0, 60, 100), empty));
	REQUIRE_FALSE(same(none, noteOn(0, 60, 100)));
	REQUIRE_FALSE(same(noteOn(0, 60, 100), none));
}

TEST_CASE("Cancel: short messages are compared on what they carry", "[MidiKit][Cancel]") {
	// A 2-byte Note-On has no velocity byte: it stays a Note-On, not a Note-Off.
	REQUIRE(same(rawMsg(0x90, 60, 0, 2), noteOn(0, 60, 100)));
	REQUIRE_FALSE(same(rawMsg(0x90, 60, 0, 2), noteOff(0, 60)));
	// A note message without its note byte has no address.
	REQUIRE_FALSE(same(rawMsg(0x90, 0, 0, 1), noteOn(0, 60, 100)));
	REQUIRE_FALSE(same(noteOn(0, 60, 100), rawMsg(0x90, 0, 0, 1)));
	REQUIRE_FALSE(same(rawMsg(0xB0, 0, 0, 1), rawMsg(0xB0, 0, 0, 1)));
}

TEST_CASE("isNoteRelease: a Note-Off or a velocity-0 Note-On, nothing else", "[MidiKit][Cancel][isNoteRelease]") {
	REQUIRE(isNoteRelease(noteOff(0, 60)));
	REQUIRE(isNoteRelease(rawMsg(0x85, 60, 64)));
	REQUIRE(isNoteRelease(noteOn(0, 60, 0)));
	REQUIRE_FALSE(isNoteRelease(noteOn(0, 60, 1)));
	REQUIRE_FALSE(isNoteRelease(noteOn(0, 60, 127)));
	// A 2-byte Note-On has no velocity and stays a Note-On.
	REQUIRE_FALSE(isNoteRelease(rawMsg(0x90, 60, 0, 2)));
	REQUIRE_FALSE(isNoteRelease(ccMsg(0, 60, 0)));
	REQUIRE_FALSE(isNoteRelease(rawMsg(0xA0, 60, 0)));
	REQUIRE_FALSE(isNoteRelease(rawMsg(0xF8, 0, 0, 1)));
	// No status byte, or no bytes at all.
	midi::Message empty;
	midi::Message none;
	none.bytes.clear();
	REQUIRE_FALSE(isNoteRelease(empty));
	REQUIRE_FALSE(isNoteRelease(none));
}

TEST_CASE("Cancel: cancelMatches by mode, groups and plain messages", "[MidiKit][Cancel]") {
	OutGroup none;
	OutGroup nrpn;
	nrpn.kind = OutGroup::NRPN;
	nrpn.channel = 2;
	nrpn.param = 300;
	OutGroup otherNumber = nrpn;
	otherNumber.param = 301;
	OutGroup rpn = nrpn;
	rpn.kind = OutGroup::RPN;
	OutGroup cc14;
	cc14.kind = OutGroup::CC14;
	cc14.channel = 2;
	cc14.param = 300 & 0x7f;
	midi::Message cc6 = ccMsg(2, 6, 1);
	midi::Message empty;

	SECTION("ALL matches everything, grouped or not") {
		REQUIRE(cancelMatches(CancelMode::ALL, empty, none, noteOn(0, 60, 1), none));
		REQUIRE(cancelMatches(CancelMode::ALL, empty, none, cc6, nrpn));
	}
	SECTION("MESSAGE never matches a group member") {
		REQUIRE(cancelMatches(CancelMode::MESSAGE, cc6, none, cc6, none));
		REQUIRE_FALSE(cancelMatches(CancelMode::MESSAGE, cc6, none, cc6, nrpn));
		REQUIRE_FALSE(cancelMatches(CancelMode::MESSAGE, cc6, none, cc6, cc14));
	}
	SECTION("GROUP matches only an equal group") {
		REQUIRE(cancelMatches(CancelMode::GROUP, empty, nrpn, cc6, nrpn));
		REQUIRE_FALSE(cancelMatches(CancelMode::GROUP, empty, nrpn, cc6, otherNumber));
		REQUIRE_FALSE(cancelMatches(CancelMode::GROUP, empty, nrpn, cc6, rpn));
		REQUIRE_FALSE(cancelMatches(CancelMode::GROUP, empty, nrpn, cc6, cc14));
		REQUIRE_FALSE(cancelMatches(CancelMode::GROUP, empty, nrpn, cc6, none));
		OutGroup otherChannel = nrpn;
		otherChannel.channel = 3;
		REQUIRE_FALSE(cancelMatches(CancelMode::GROUP, empty, nrpn, cc6, otherChannel));
	}
}


// ── midiOut.cancel() ────────────────────────────────────────────────────────
// Matching rules are in MidiKit.test.cancel.hpp; these go through the module:
// worker -> ring -> port queues -> recorder. Every case runs in both engines.

static const char* JS_CANCEL_HEAD = R"(/**
 * @engine QuickJs@v1
 */
function off(n) { let m = midi.create(); midi.setNoteOff(m, 1, n, 0); return m; }
)";

static const char* LUA_CANCEL_HEAD = R"(--[[
@engine minilua@v1
--]]
local function off(n) local m = midi.create(); midi.setNoteOff(m, 1, n, 0); return m end
)";

struct CancelScripts {
	std::string js;
	std::string lua;
	std::string get(bool isLua) const { return isLua ? lua : js; }
};

static CancelScripts cancelScripts(const std::string& js, const std::string& lua) {
	return { std::string(JS_CANCEL_HEAD) + js, std::string(LUA_CANCEL_HEAD) + lua };
}

TEST_CASE("Cancel: a message handle drops only the scheduled messages with its address", "[MidiKit][Cancel][Timing]") {
	CancelScripts s = cancelScripts(R"(
midi.onMessage = function(port, msg) {
    midiOut.sendAfterMs(off(60), 10);
    midiOut.sendAfterMs(off(61), 10);
    midiOut.cancel(off(60));
};
)", R"(
midi.onMessage = function(port, msg)
    midiOut.sendAfterMs(off(60), 10)
    midiOut.sendAfterMs(off(61), 10)
    midiOut.cancel(off(60))
end
)");
	FOR_EACH_LANG;
	bool lua = lang == Lang::Lua;
	for (bool timing : { false, true }) {
		CATCH_INFO(std::string(lua ? "lua" : "js") + (timing ? " timing" : " legacy"));
		std::string script = timing ? withTiming(s.get(lua).c_str()) : s.get(lua);
		TimingRig rig(script.c_str());
		rig.inject(noteOn(0, 60, 100), 8);
		rig.run(cancelRunUntil());

		REQUIRE(sentNotes(rig.rec) == std::vector<int>{61});
		REQUIRE(rig.rec.sent[0].status == 0x8);
	}
}

TEST_CASE("Cancel: a trigger-scheduled message is dropped before its edge", "[MidiKit][Cancel][Timing]") {
	CancelScripts s = cancelScripts(R"(
trig.enableIn(1);
midi.onMessage = function(port, msg) {
    midiOut.sendAfterTrigger(off(60), 1);
    midiOut.sendAfterTrigger(off(61), 1);
    midiOut.cancel(off(60));
};
)", R"(
trig.enableIn(1)
midi.onMessage = function(port, msg)
    midiOut.sendAfterTrigger(off(60), 1)
    midiOut.sendAfterTrigger(off(61), 1)
    midiOut.cancel(off(60))
end
)");
	FOR_EACH_LANG;
	bool lua = lang == Lang::Lua;
	CATCH_INFO(std::string(lua ? "lua" : "js"));
	TimingRig rig(s.get(lua).c_str());
	rig.m->inputs[MidiKitModule::INPUT_TRIG].channels = 1;

	rig.run(8);
	rig.inject(noteOn(0, 60, 100), 8);
	rig.run(45);
	REQUIRE(rig.rec.sent.empty());

	rig.m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(10.f);
	rig.step();
	rig.m->inputs[MidiKitModule::INPUT_TRIG].setVoltage(0.f);
	rig.run(80);

	REQUIRE(sentNotes(rig.rec) == std::vector<int>{61});
}

TEST_CASE("Cancel: without an argument it clears the selected port only", "[MidiKit][Cancel][Timing]") {
	CancelScripts s = cancelScripts(R"(
midiOut.enablePorts(2);
midi.onMessage = function(port, msg) {
    midiOut.selectPort(1);
    midiOut.sendAfterMs(off(60), 10);
    midiOut.sendAfterMs(off(61), 10);
    midiOut.selectPort(2);
    midiOut.sendAfterMs(off(62), 10);
    midiOut.selectPort(1);
    midiOut.cancel();
};
)", R"(
midiOut.enablePorts(2)
midi.onMessage = function(port, msg)
    midiOut.selectPort(1)
    midiOut.sendAfterMs(off(60), 10)
    midiOut.sendAfterMs(off(61), 10)
    midiOut.selectPort(2)
    midiOut.sendAfterMs(off(62), 10)
    midiOut.selectPort(1)
    midiOut.cancel()
end
)");
	FOR_EACH_LANG;
	bool lua = lang == Lang::Lua;
	CATCH_INFO(std::string(lua ? "lua" : "js"));
	// Declared first: it must outlive the rig, whose teardown flushes through it.
	TimingRecorder rec2;
	TimingRig rig(s.get(lua).c_str());
	rig.m->midiOuts.ports[1].outputDevice = &rec2;
	rig.m->midiOuts.ports[1].channel = -1;
	rig.inject(noteOn(0, 60, 100), 8);
	rec2.now = 0;
	for (int64_t f = rig.frame; f < cancelRunUntil(); f++) {
		rec2.now = f;
		rig.step();
	}

	REQUIRE(rig.rec.sent.empty());
	REQUIRE(sentNotes(rec2) == std::vector<int>{62});
	rig.m->midiOuts.ports[1].outputDevice = nullptr;
}

TEST_CASE("Cancel: a timing-mode send() in the same callback is not cancelled", "[MidiKit][Cancel][Timing]") {
	// In timing mode send() waits in the frame queue until the end of the pump,
	// next to the scheduled messages: only the `scheduled` flag tells them apart.
	CancelScripts s = cancelScripts(R"(
midi.onMessage = function(port, msg) {
    midiOut.send(off(60));
    midiOut.cancel();
};
)", R"(
midi.onMessage = function(port, msg)
    midiOut.send(off(60))
    midiOut.cancel()
end
)");
	FOR_EACH_LANG;
	bool lua = lang == Lang::Lua;
	for (bool timing : { false, true }) {
		CATCH_INFO(std::string(lua ? "lua" : "js") + (timing ? " timing" : " legacy"));
		std::string script = timing ? withTiming(s.get(lua).c_str()) : s.get(lua);
		TimingRig rig(script.c_str());
		rig.inject(noteOn(0, 60, 100), 8);
		rig.run(60);

		REQUIRE(sentNotes(rig.rec) == std::vector<int>{60});
	}
}

TEST_CASE("Cancel: sendAtFrame with a negative frame is a plain send", "[MidiKit][Cancel][Timing]") {
	CancelScripts s = cancelScripts(R"(
midi.onMessage = function(port, msg) {
    midiOut.sendAtFrame(off(60), -1);
    midiOut.cancel();
};
)", R"(
midi.onMessage = function(port, msg)
    midiOut.sendAtFrame(off(60), -1)
    midiOut.cancel()
end
)");
	FOR_EACH_LANG;
	bool lua = lang == Lang::Lua;
	for (bool timing : { false, true }) {
		CATCH_INFO(std::string(lua ? "lua" : "js") + (timing ? " timing" : " legacy"));
		std::string script = timing ? withTiming(s.get(lua).c_str()) : s.get(lua);
		TimingRig rig(script.c_str());
		rig.inject(noteOn(0, 60, 100), 8);
		rig.run(60);

		REQUIRE(sentNotes(rig.rec) == std::vector<int>{60});
	}
}

TEST_CASE("Cancel: it applies in call order", "[MidiKit][Cancel][Timing]") {
	CancelScripts s = cancelScripts(R"(
midi.onMessage = function(port, msg) {
    midiOut.sendAfterMs(off(60), 10);
    midiOut.cancel();
    midiOut.sendAfterMs(off(61), 10);
};
)", R"(
midi.onMessage = function(port, msg)
    midiOut.sendAfterMs(off(60), 10)
    midiOut.cancel()
    midiOut.sendAfterMs(off(61), 10)
end
)");
	FOR_EACH_LANG;
	bool lua = lang == Lang::Lua;
	CATCH_INFO(std::string(lua ? "lua" : "js"));
	TimingRig rig(s.get(lua).c_str());
	rig.inject(noteOn(0, 60, 100), 8);
	rig.run(cancelRunUntil());

	REQUIRE(sentNotes(rig.rec) == std::vector<int>{61});
}

TEST_CASE("Cancel: a group is taken whole by its handle and never split by a member", "[MidiKit][Cancel][Timing]") {
	// The incoming note picks the cancel: 1 = a plain CC 99 (a member of the
	// scheduled NRPN), 2 = the NRPN handle itself, 3 = another NRPN number.
	CancelScripts s = cancelScripts(R"(
midi.onMessage = function(port, msg) {
    let n = midi.createNRPN();
    midi.setNRPN(n, 1, 300, 1000);
    midiOut.sendAfterMs(n, 10);
    let k = midi.getNote(msg);
    if (k == 1) { let c = midi.create(); midi.setCc(c, 1, 99, 2); midiOut.cancel(c); }
    else if (k == 2) { midiOut.cancel(n); }
    else if (k == 3) { let o = midi.createNRPN(); midi.setNRPN(o, 1, 301, 0); midiOut.cancel(o); }
};
)", R"(
midi.onMessage = function(port, msg)
    local n = midi.createNRPN()
    midi.setNRPN(n, 1, 300, 1000)
    midiOut.sendAfterMs(n, 10)
    local k = midi.getNote(msg)
    if k == 1 then local c = midi.create(); midi.setCc(c, 1, 99, 2); midiOut.cancel(c)
    elseif k == 2 then midiOut.cancel(n)
    elseif k == 3 then local o = midi.createNRPN(); midi.setNRPN(o, 1, 301, 0); midiOut.cancel(o)
    end
end
)");
	struct Case { int note; std::vector<int> controllers; };
	// Number 300 = 2 * 128 + 44: CC 99 and 98 select it, CC 6 and 38 carry the value.
	std::vector<Case> cases = {
		{ 1, { 99, 98, 6, 38 } },
		{ 2, { } },
		{ 3, { 99, 98, 6, 38 } },
	};
	FOR_EACH_LANG;
	bool lua = lang == Lang::Lua;
	for (const Case& c : cases) {
		CATCH_INFO(std::string(lua ? "lua" : "js") + " note " + std::to_string(c.note));
		TimingRig rig(s.get(lua).c_str());
		rig.inject(noteOn(0, c.note, 100), 8);
		rig.run(cancelRunUntil());

		REQUIRE(sentNotes(rig.rec) == c.controllers);
	}
}

TEST_CASE("Cancel: a message already past its frame is not recalled", "[MidiKit][Cancel][Timing]") {
	// Note 60 schedules for 1 ms; note 61, long after, cancels what it was.
	CancelScripts s = cancelScripts(R"(
midi.onMessage = function(port, msg) {
    if (midi.getNote(msg) == 60) midiOut.sendAfterMs(off(60), 1);
    else midiOut.cancel(off(60));
};
)", R"(
midi.onMessage = function(port, msg)
    if midi.getNote(msg) == 60 then midiOut.sendAfterMs(off(60), 1)
    else midiOut.cancel(off(60)) end
end
)");
	FOR_EACH_LANG;
	bool lua = lang == Lang::Lua;
	CATCH_INFO(std::string(lua ? "lua" : "js"));
	TimingRig rig(s.get(lua).c_str());
	rig.inject(noteOn(0, 60, 100), 8);
	rig.run(300);
	REQUIRE(sentNotes(rig.rec) == std::vector<int>{60});

	rig.inject(noteOn(0, 61, 100), 320);
	rig.run(400);
	REQUIRE(sentNotes(rig.rec) == std::vector<int>{60});
}

TEST_CASE("Cancel: 40 cancels in one callback all apply, over several drains", "[MidiKit][Cancel][Timing]") {
	// A cancel weighs 8 of the drain's 128 units, so 40 of them (and the 41 sends
	// before them) need more than one drain. One handle is reused: a callback has
	// only a few message slots.
	CancelScripts s = cancelScripts(R"(
midi.onMessage = function(port, msg) {
    let m = midi.create();
    for (let i = 0; i < 40; i++) { midi.setNoteOff(m, 1, i, 0); midiOut.sendAfterMs(m, 10); }
    midi.setNoteOff(m, 1, 100, 0);
    midiOut.sendAfterMs(m, 10);
    for (let i = 0; i < 40; i++) { midi.setNoteOff(m, 1, i, 0); midiOut.cancel(m); }
};
)", R"(
midi.onMessage = function(port, msg)
    local m = midi.create()
    for i = 0, 39 do midi.setNoteOff(m, 1, i, 0); midiOut.sendAfterMs(m, 10) end
    midi.setNoteOff(m, 1, 100, 0)
    midiOut.sendAfterMs(m, 10)
    for i = 0, 39 do midi.setNoteOff(m, 1, i, 0); midiOut.cancel(m) end
end
)");
	FOR_EACH_LANG;
	bool lua = lang == Lang::Lua;
	CATCH_INFO(std::string(lua ? "lua" : "js"));
	TimingRig rig(s.get(lua).c_str());
	rig.inject(noteOn(0, 60, 100), 8);
	rig.run(cancelRunUntil());

	REQUIRE(sentNotes(rig.rec) == std::vector<int>{100});
}

TEST_CASE("Cancel: bad arguments raise a script error and cancel nothing", "[MidiKit][Cancel][Timing]") {
	CancelScripts s = cancelScripts(R"(
function t(name, f) {
    try { f(); rack.log("ok " + name); }
    catch (e) { rack.log("err " + name); }
}
midi.onMessage = function(port, msg) {
    midiOut.sendAfterMs(off(60), 10);
    t("empty", function() { midiOut.cancel(midi.create()); });
    t("unsetNrpn", function() { midiOut.cancel(midi.createNRPN()); });
    t("twoArgs", function() { midiOut.cancel(off(60), off(61)); });
    t("notHandle", function() { midiOut.cancel("x"); });
    t("staleHandle", function() { midiOut.cancel(123456789); });
};
)", R"(
local function t(name, f)
    local ok = pcall(f)
    if ok then rack.log("ok " .. name) else rack.log("err " .. name) end
end
midi.onMessage = function(port, msg)
    midiOut.sendAfterMs(off(60), 10)
    t("empty", function() midiOut.cancel(midi.create()) end)
    t("unsetNrpn", function() midiOut.cancel(midi.createNRPN()) end)
    t("twoArgs", function() midiOut.cancel(off(60), off(61)) end)
    t("notHandle", function() midiOut.cancel("x") end)
    t("staleHandle", function() midiOut.cancel(123456789) end)
end
)");
	FOR_EACH_LANG;
	bool lua = lang == Lang::Lua;
	CATCH_INFO(std::string(lua ? "lua" : "js"));
	TimingRig rig(s.get(lua).c_str());
	drainLog(rig.m);
	rig.inject(noteOn(0, 60, 100), 8);
	rig.run(cancelRunUntil());

	std::string log = drainLog(rig.m);
	CATCH_INFO("log: " << log);
	for (const char* name : { "empty", "unsetNrpn", "twoArgs", "notHandle", "staleHandle" }) {
		CATCH_INFO(name);
		REQUIRE(log.find(std::string("err ") + name) != std::string::npos);
		REQUIRE(log.find(std::string("ok ") + name) == std::string::npos);
	}
	// The scheduled note was not touched by any of the failed calls.
	REQUIRE(sentNotes(rig.rec) == std::vector<int>{60});
}


TEST_CASE("Cancel: without an argument it clears every tick queue", "[MidiKit][Cancel][Timing]") {
	// Messages on trigger input 1 and on input 2, channels 1 and 3. Note 2 skips
	// the cancel: the control that shows all three really are released.
	CancelScripts s = cancelScripts(R"(
trig.enableIn(1, 1);
trig.enableIn(2, 1);
trig.enableIn(2, 3);
midi.onMessage = function(port, msg) {
    midiOut.sendAfterTrigger(off(60), 1, 1, 1);
    midiOut.sendAfterTrigger(off(61), 1, 2, 3);
    midiOut.sendAfterTrigger(off(62), 1, 2, 1);
    if (midi.getNote(msg) == 1) midiOut.cancel();
};
)", R"(
trig.enableIn(1, 1)
trig.enableIn(2, 1)
trig.enableIn(2, 3)
midi.onMessage = function(port, msg)
    midiOut.sendAfterTrigger(off(60), 1, 1, 1)
    midiOut.sendAfterTrigger(off(61), 1, 2, 3)
    midiOut.sendAfterTrigger(off(62), 1, 2, 1)
    if midi.getNote(msg) == 1 then midiOut.cancel() end
end
)");
	struct Case { int note; std::vector<int> sent; };
	std::vector<Case> cases = { { 1, { } }, { 2, { 60, 61, 62 } } };
	FOR_EACH_LANG;
	bool lua = lang == Lang::Lua;
	for (const Case& c : cases) {
		CATCH_INFO(std::string(lua ? "lua" : "js") + " note " + std::to_string(c.note));
		TimingRig rig(s.get(lua).c_str());
		rig.m->inputs[MidiKitModule::INPUT_TRIG + 0].channels = 1;
		rig.m->inputs[MidiKitModule::INPUT_TRIG + 1].channels = 3;

		rig.run(8);
		rig.inject(noteOn(0, c.note, 100), 8);
		rig.run(45);
		REQUIRE(rig.rec.sent.empty());

		rig.m->inputs[MidiKitModule::INPUT_TRIG + 0].setVoltage(10.f, 0);
		for (int ch = 0; ch < 3; ch++) rig.m->inputs[MidiKitModule::INPUT_TRIG + 1].setVoltage(10.f, ch);
		rig.step();
		rig.m->inputs[MidiKitModule::INPUT_TRIG + 0].setVoltage(0.f, 0);
		for (int ch = 0; ch < 3; ch++) rig.m->inputs[MidiKitModule::INPUT_TRIG + 1].setVoltage(0.f, ch);
		rig.run(80);

		std::vector<int> got = sentNotes(rig.rec);
		std::sort(got.begin(), got.end());
		REQUIRE(got == c.sent);
	}
}

TEST_CASE("Cancel: 14-bit CC and RPN handles go through the bindings as groups", "[MidiKit][Cancel][Timing]") {
	// The incoming note picks the scenario (see `cases`). A scheduled message
	// is a 14-bit CC (MSB 5, so CC 5 and 37) or an RPN 300 (CC 101, 100, 6, 38).
	CancelScripts s = cancelScripts(R"(
midi.onMessage = function(port, msg) {
    let k = midi.getNote(msg);
    if (k <= 3) {
        let h = midi.createCc14bit(); midi.setCc14bit(h, 1, 5, 12864);
        midiOut.sendAfterMs(h, 10);
        if (k == 1) midiOut.cancel(h);
        else if (k == 2) { let c = midi.create(); midi.setCc(c, 1, 5, 1); midiOut.cancel(c); }
        else { let o = midi.createCc14bit(); midi.setCc14bit(o, 1, 6, 12864); midiOut.cancel(o); }
    } else {
        let h = midi.createRPN(); midi.setRPN(h, 1, 300, 1000);
        midiOut.sendAfterMs(h, 10);
        if (k == 4) { let n = midi.createNRPN(); midi.setNRPN(n, 1, 300, 1000); midiOut.cancel(n); }
        else { midiOut.cancel(h); }
    }
};
)", R"(
midi.onMessage = function(port, msg)
    local k = midi.getNote(msg)
    if k <= 3 then
        local h = midi.createCc14bit(); midi.setCc14bit(h, 1, 5, 12864)
        midiOut.sendAfterMs(h, 10)
        if k == 1 then midiOut.cancel(h)
        elseif k == 2 then local c = midi.create(); midi.setCc(c, 1, 5, 1); midiOut.cancel(c)
        else local o = midi.createCc14bit(); midi.setCc14bit(o, 1, 6, 12864); midiOut.cancel(o) end
    else
        local h = midi.createRPN(); midi.setRPN(h, 1, 300, 1000)
        midiOut.sendAfterMs(h, 10)
        if k == 4 then local n = midi.createNRPN(); midi.setNRPN(n, 1, 300, 1000); midiOut.cancel(n)
        else midiOut.cancel(h) end
    end
end
)");
	struct Case { int note; std::vector<int> controllers; };
	std::vector<Case> cases = {
		{ 1, { } },                      // the 14-bit handle removes the pair
		{ 2, { 5, 37 } },                // a plain CC 5 never splits it
		{ 3, { 5, 37 } },                // another MSB controller is another group
		{ 4, { 101, 100, 6, 38 } },      // an NRPN of the same number is not the RPN
		{ 5, { } },                      // the RPN handle removes the quad
	};
	FOR_EACH_LANG;
	bool lua = lang == Lang::Lua;
	for (const Case& c : cases) {
		CATCH_INFO(std::string(lua ? "lua" : "js") + " note " + std::to_string(c.note));
		TimingRig rig(s.get(lua).c_str());
		rig.inject(noteOn(0, c.note, 100), 8);
		rig.run(cancelRunUntil());

		REQUIRE(sentNotes(rig.rec) == c.controllers);
	}
}

TEST_CASE("Cancel: on a port that is not enabled it does nothing, silently", "[MidiKit][Cancel][Timing]") {
	CancelScripts s = cancelScripts(R"(
midi.onMessage = function(port, msg) {
    midiOut.sendAfterMs(off(60), 10);
    midiOut.selectPort(2);
    midiOut.cancel();
};
)", R"(
midi.onMessage = function(port, msg)
    midiOut.sendAfterMs(off(60), 10)
    midiOut.selectPort(2)
    midiOut.cancel()
end
)");
	FOR_EACH_LANG;
	bool lua = lang == Lang::Lua;
	CATCH_INFO(std::string(lua ? "lua" : "js"));
	TimingRig rig(s.get(lua).c_str());
	drainLog(rig.m);
	rig.inject(noteOn(0, 60, 100), 8);
	rig.run(cancelRunUntil());

	std::string log = drainLog(rig.m);
	CATCH_INFO("log: " << log);
	REQUIRE(log.find("rror") == std::string::npos);
	REQUIRE(log.find("not enabled") == std::string::npos);
	// Port 1's scheduled message is untouched.
	REQUIRE(sentNotes(rig.rec) == std::vector<int>{60});
}
