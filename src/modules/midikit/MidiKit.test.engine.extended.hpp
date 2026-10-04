// 14-bit CC and NRPN message construction and send order, the midi.is* predicates and midi.getChannel.
//
// Part of the cross-engine suite: included into the __engine namespace by
// MidiKit.test.cpp after MidiKit.test.engine.hpp, which defines the shared helpers.


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
	EngineResult js = run(JS_CC_14BIT_SEND_ORDER);
	EngineResult lua = run(LUA_CC_14BIT_SEND_ORDER);

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
	EngineResult js = run(JS_NRPN_SEND_ORDER);
	EngineResult lua = run(LUA_NRPN_SEND_ORDER);

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

static const char* JS_IS_TYPES = R"(/**
 * @engine QuickJs@v1
 */
rack.onLoad = function() {
    let msgNoteOn = midi.create();
    midi.setNoteOn(msgNoteOn, 1, 60, 100);
    let msgCc = midi.create();
    midi.setCc(msgCc, 1, 10, 64);
    let msgSysEx = midi.create();
    midi.setSysEx(msgSysEx, "43104c0000");

    let bits = "" +
        (midi.isNoteOn(msgNoteOn) ? "1" : "0") +
        (midi.isNoteOff(msgNoteOn) ? "1" : "0") +
        (midi.isCc(msgNoteOn) ? "1" : "0") +
        (midi.isCc(msgCc) ? "1" : "0") +
        (midi.isSysEx(msgCc) ? "1" : "0") +
        (midi.isSysEx(msgSysEx) ? "1" : "0") +
        (midi.isClock(msgNoteOn) ? "1" : "0") +
        (midi.isStart(msgNoteOn) ? "1" : "0") +
        (midi.isStop(msgNoteOn) ? "1" : "0") +
        (midi.isContinue(msgNoteOn) ? "1" : "0");
    rack.log("PROBE:" + bits);
};
)";

static const char* LUA_IS_TYPES = R"(--[[
@engine minilua@v1
--]]
rack.onLoad = function()
    local function b(v) if v then return "1" else return "0" end end

    local msgNoteOn = midi.create()
    midi.setNoteOn(msgNoteOn, 1, 60, 100)
    local msgCc = midi.create()
    midi.setCc(msgCc, 1, 10, 64)
    local msgSysEx = midi.create()
    midi.setSysEx(msgSysEx, "43104c0000")

    local bits =
        b(midi.isNoteOn(msgNoteOn)) ..
        b(midi.isNoteOff(msgNoteOn)) ..
        b(midi.isCc(msgNoteOn)) ..
        b(midi.isCc(msgCc)) ..
        b(midi.isSysEx(msgCc)) ..
        b(midi.isSysEx(msgSysEx)) ..
        b(midi.isClock(msgNoteOn)) ..
        b(midi.isStart(msgNoteOn)) ..
        b(midi.isStop(msgNoteOn)) ..
        b(midi.isContinue(msgNoteOn))
    rack.log("PROBE:" .. bits)
end
)";

TEST_CASE("midi.is* predicates agree on every message type", "[MidiKit][CrossEngine]") {
	requireLoggedValues(JS_IS_TYPES, LUA_IS_TYPES, {"1001010000"});
}


// midi.isNoteRelease: a Note-Off, or a Note-On with velocity 0 (how most keyboards
// release a key). isNoteOn/isNoteOff keep reading the status only.
// Per message the bits are isNoteOn, isNoteOff, isNoteRelease.
static const char* JS_IS_NOTE_RELEASE = R"(/**
 * @engine QuickJs@v1
 */
rack.onLoad = function() {
    let on = midi.create(); midi.setNoteOn(on, 1, 60, 100);
    let on0 = midi.create(); midi.setNoteOn(on0, 1, 60, 0);
    let off = midi.create(); midi.setNoteOff(off, 1, 60, 64);
    let cc = midi.create(); midi.setCc(cc, 1, 10, 0);
    let short = midi.create(); midi.setRaw(short, "903c");
    let sysex = midi.create(); midi.setSysEx(sysex, "43104c0000");
    let all = [on, on0, off, cc, short, sysex];
    let bits = "";
    for (let i = 0; i < all.length; i++) {
        bits += (midi.isNoteOn(all[i]) ? "1" : "0") + (midi.isNoteOff(all[i]) ? "1" : "0") + (midi.isNoteRelease(all[i]) ? "1" : "0");
    }
    rack.log("PROBE:" + bits);
};
)";

static const char* LUA_IS_NOTE_RELEASE = R"(--[[
@engine minilua@v1
--]]
rack.onLoad = function()
    local function b(v) if v then return "1" else return "0" end end
    local on = midi.create(); midi.setNoteOn(on, 1, 60, 100)
    local on0 = midi.create(); midi.setNoteOn(on0, 1, 60, 0)
    local off = midi.create(); midi.setNoteOff(off, 1, 60, 64)
    local cc = midi.create(); midi.setCc(cc, 1, 10, 0)
    local short = midi.create(); midi.setRaw(short, "903c")
    local sysex = midi.create(); midi.setSysEx(sysex, "43104c0000")
    local all = { on, on0, off, cc, short, sysex }
    local bits = ""
    for i = 1, #all do
        bits = bits .. b(midi.isNoteOn(all[i])) .. b(midi.isNoteOff(all[i])) .. b(midi.isNoteRelease(all[i]))
    end
    rack.log("PROBE:" .. bits)
end
)";

TEST_CASE("midi.isNoteRelease folds a velocity-0 Note-On into a release, in both engines", "[MidiKit][CrossEngine]") {
	// on(100): 100, on(0): 101, off: 011, cc: 000, 2-byte Note-On: 100, sysex: 000
	requireLoggedValues(JS_IS_NOTE_RELEASE, LUA_IS_NOTE_RELEASE, {"100101011000100000"});
}

static const char* JS_IS_NOTE_RELEASE_BAD = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) { midi.isNoteRelease(); };
)";

static const char* LUA_IS_NOTE_RELEASE_BAD = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(port, msg) midi.isNoteRelease() end
)";

TEST_CASE("midi.isNoteRelease rejects a missing message", "[MidiKit][CrossEngine]") {
	requireEquivalentLog(JS_IS_NOTE_RELEASE_BAD, LUA_IS_NOTE_RELEASE_BAD, "isNoteRelease", true);
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
