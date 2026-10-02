// 14-bit CC and NRPN message construction and flush order, the midi.is* predicates and midi.getChannel.
//
// Part of the cross-engine suite: included into the __engine namespace by
// MidiKit.test.cpp after MidiKit.test.engine.hpp, which defines the shared helpers.

// setCc14bit

static const char* JS_CC_14BIT = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let msb = midi.create();
    let lsb = midi.create();
    midi.setCc14bit(msb, lsb, 8, 1, 100.5);
    midiOut.send(msb);
    midiOut.send(lsb);
};
)";

static const char* LUA_CC_14BIT = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local msb = midi.create()
    local lsb = midi.create()
    midi.setCc14bit(msb, lsb, 8, 1, 100.5)
    midiOut.send(msb)
    midiOut.send(lsb)
end
)";

TEST_CASE("setCc14bit produces identical MSB/LSB wire messages", "[MidiKit][CrossEngine]") {
	requireEquivalent(JS_CC_14BIT, LUA_CC_14BIT);
}


// setCc14bit on a createCc14bit() pair (atomic 2-message flush)
// The two-handle form above sends two independent messages. A
// createCc14bit() pair is the atomic alternative: midiOut.send(cc14) flushes
// both underlying CC messages in order when passed the first handle of the
// pair (per SCRIPTING.md), so sending it is what exercises the pair end to
// end.

static const char* JS_CC_14BIT_PAIR = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let cc14 = midi.createCc14bit();
    midi.setCc14bit(cc14, 8, 1, 100.5);
    midiOut.send(cc14);
};
)";

static const char* LUA_CC_14BIT_PAIR = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local cc14 = midi.createCc14bit()
    midi.setCc14bit(cc14, 8, 1, 100.5)
    midiOut.send(cc14)
end
)";

TEST_CASE("createCc14bit pair produces identical MSB/LSB wire messages", "[MidiKit][CrossEngine]") {
	requireEquivalent(JS_CC_14BIT_PAIR, LUA_CC_14BIT_PAIR);
}

// Cross-engine equivalence only pins JS and Lua to each other — it can't
// catch a bug shared by both (the NRPN quad once flushed MSB-after-LSB in
// both engines). This asserts the actual wire bytes/order against the
// 14-bit CC convention: CC 1 (value MSB), then CC 33 (value LSB).
TEST_CASE("createCc14bit pair wire order is spec-compliant (MSB before LSB)", "[MidiKit]") {
	EngineResult r = run(JS_CC_14BIT_PAIR);
	REQUIRE(r.sent.size() == 2);
	// channel 8 -> status/channel byte 0xb7; value=100.5 -> MSB=100, LSB=64
	REQUIRE(r.sent[0].bytes == std::vector<uint8_t>{0xb7, 1, 100});
	REQUIRE(r.sent[1].bytes == std::vector<uint8_t>{0xb7, 33, 64});
}


// 14-bit CC pair send() order
// A 14-bit CC pair flushes as a unit when the group leader is sent. This
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
    midi.setCc14bit(p1, 9, 1, 100.5);
    let p2 = midi.createCc14bit();
    midi.setCc14bit(p2, 9, 2, 3.5);
    midiOut.send(p2);   // created second, sent first
    midiOut.send(p1);   // created first, sent last
};
)";

static const char* LUA_CC_14BIT_SEND_ORDER = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local p1 = midi.createCc14bit()
    midi.setCc14bit(p1, 9, 1, 100.5)
    local p2 = midi.createCc14bit()
    midi.setCc14bit(p2, 9, 2, 3.5)
    midiOut.send(p2)
    midiOut.send(p1)
end
)";

TEST_CASE("14-bit CC pairs flush in send() order, not handle-creation order, in both engines", "[MidiKit][CrossEngine]") {
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


// setNRPN (4 chained CC messages)
// midiOut.send(nrpnHandle) flushes all 4 underlying CC messages in order
// when passed the first handle of an NRPN quad (per SCRIPTING.md), so
// sending it is what actually exercises setNRPN's byte layout end to end.

static const char* JS_4MESSAGE_NRPN = R"(/**
 * @engine QuickJs@v1
 */
midi.onMessage = function(port, msg) {
    let nrpn = midi.createNRPN();
    midi.setNRPN(nrpn, 9, 1234, 5678);
    midiOut.send(nrpn);
};
)";

static const char* LUA_4MESSAGE_NRPN = R"(--[[
@engine minilua@v1
--]]
midi.onMessage = function(midiPort, msg)
    local nrpn = midi.createNRPN()
    midi.setNRPN(nrpn, 9, 1234, 5678)
    midiOut.send(nrpn)
end
)";

TEST_CASE("setNRPN produces identical 4-message wire sequence", "[MidiKit][CrossEngine]") {
	requireEquivalent(JS_4MESSAGE_NRPN, LUA_4MESSAGE_NRPN);
}

// Cross-engine equivalence above only pins JS and Lua to each other — it
// can't catch a bug shared by both (as happened: both engines flushed the
// quad as CC98/CC99/CC38/CC6, MSB-after-LSB for both pairs, which desyncs
// MidiProcessor::processCc's NRPN state machine and corrupts every value
// after the first). This asserts the actual wire bytes/order against the
// spec: CC99 (param MSB), CC98 (param LSB), CC6 (data MSB), CC38 (data LSB).
TEST_CASE("setNRPN wire order is spec-compliant (MSB before LSB)", "[MidiKit]") {
	EngineResult r = run(JS_4MESSAGE_NRPN);
	REQUIRE(r.sent.size() == 4);
	// channel 9 -> status/channel byte 0xb8; number=1234 -> msb=9,lsb=82; value=5678 -> msb=44,lsb=46
	REQUIRE(r.sent[0].bytes == std::vector<uint8_t>{0xb8, 99, 9});
	REQUIRE(r.sent[1].bytes == std::vector<uint8_t>{0xb8, 98, 82});
	REQUIRE(r.sent[2].bytes == std::vector<uint8_t>{0xb8, 6, 44});
	REQUIRE(r.sent[3].bytes == std::vector<uint8_t>{0xb8, 38, 46});
}


// NRPN send() order
// An NRPN is a quad of 4 CC messages that flush as a unit when the group
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

TEST_CASE("NRPN quads flush in send() order, not handle-creation order, in both engines", "[MidiKit][CrossEngine]") {
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
)";

static const char* LUA_IS_TYPES = R"(--[[
@engine minilua@v1
--]]
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
)";

TEST_CASE("midi.is* predicates agree on every message type", "[MidiKit][CrossEngine]") {
	requireLoggedValues(JS_IS_TYPES, LUA_IS_TYPES, {"1001010000"});
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
let note = midi.create();
midi.setNoteOn(note, 5, 60, 100);
rack.log("PROBE:" + number.toString(midi.getChannel(note)));

let clock = midi.create();
midi.setSysEx(clock, "");
rack.log("PROBE:" + number.toString(midi.getChannel(clock)));
)";

static const char* LUA_GET_CHANNEL_SENTINEL = R"(--[[
@engine minilua@v1
--]]
local note = midi.create()
midi.setNoteOn(note, 5, 60, 100)
rack.log("PROBE:" .. number.toString(midi.getChannel(note)))

local clock = midi.create()
midi.setSysEx(clock, "")
rack.log("PROBE:" .. number.toString(midi.getChannel(clock)))
)";

TEST_CASE("midi.getChannel returns -1 on realtime/SysEx, the real channel otherwise", "[MidiKit][CrossEngine]") {
	requireLoggedValues(JS_GET_CHANNEL_SENTINEL, LUA_GET_CHANNEL_SENTINEL, {"5", "-1"});
}
