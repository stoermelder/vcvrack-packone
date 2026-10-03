#include "MidiKit.test.hpp"

// midiOut.cancel(): the address rules (MidiScriptTypes.hpp), no module involved.
// The module side is tested in MidiKit.test.timing.hpp ("Cancel: ...") and
// MidiKit.test.swap.hpp.
//
// Run alone: ./build/test/MidiKit.test "[cancel]"

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

TEST_CASE("Cancel: Note-On matches Note-On on the same channel and note, any velocity", "[MidiKit][cancel]") {
	REQUIRE(same(noteOn(0, 60, 100), noteOn(0, 60, 1)));
	REQUIRE_FALSE(same(noteOn(0, 60, 100), noteOn(1, 60, 100)));
	REQUIRE_FALSE(same(noteOn(0, 60, 100), noteOn(0, 61, 100)));
}

TEST_CASE("Cancel: Note-Off matches Note-Off, in either encoding", "[MidiKit][cancel]") {
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

TEST_CASE("Cancel: Note-On and Note-Off are separate addresses", "[MidiKit][cancel]") {
	REQUIRE_FALSE(same(noteOn(0, 60, 100), noteOff(0, 60)));
	REQUIRE_FALSE(same(noteOff(0, 60), noteOn(0, 60, 100)));
	REQUIRE_FALSE(same(noteOn(0, 60, 100), noteOn(0, 60, 0)));
	REQUIRE_FALSE(same(noteOn(0, 60, 0), noteOn(0, 60, 100)));
}

TEST_CASE("Cancel: poly aftertouch and control change address a data byte, not the value", "[MidiKit][cancel]") {
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

TEST_CASE("Cancel: program change, channel pressure and pitch bend address the channel only", "[MidiKit][cancel]") {
	REQUIRE(same(rawMsg(0xC0, 5, 0, 2), rawMsg(0xC0, 99, 0, 2)));
	REQUIRE_FALSE(same(rawMsg(0xC0, 5, 0, 2), rawMsg(0xC1, 5, 0, 2)));
	REQUIRE(same(rawMsg(0xD2, 5, 0, 2), rawMsg(0xD2, 99, 0, 2)));
	REQUIRE(same(rawMsg(0xE3, 1, 2), rawMsg(0xE3, 100, 90)));
	REQUIRE_FALSE(same(rawMsg(0xE3, 1, 2), rawMsg(0xE4, 1, 2)));
	REQUIRE_FALSE(same(rawMsg(0xC0, 5, 0, 2), rawMsg(0xD0, 5, 0, 2)));
}

TEST_CASE("Cancel: any SysEx matches any SysEx, other system messages their status byte", "[MidiKit][cancel]") {
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

TEST_CASE("Cancel: a message without a status byte matches nothing", "[MidiKit][cancel]") {
	midi::Message empty;   // a default midi::Message: three zero bytes
	midi::Message none;
	none.bytes.clear();
	REQUIRE_FALSE(same(empty, empty));
	REQUIRE_FALSE(same(empty, noteOn(0, 60, 100)));
	REQUIRE_FALSE(same(noteOn(0, 60, 100), empty));
	REQUIRE_FALSE(same(none, noteOn(0, 60, 100)));
	REQUIRE_FALSE(same(noteOn(0, 60, 100), none));
}

TEST_CASE("Cancel: short messages are compared on what they carry", "[MidiKit][cancel]") {
	// A 2-byte Note-On has no velocity byte: it stays a Note-On, not a Note-Off.
	REQUIRE(same(rawMsg(0x90, 60, 0, 2), noteOn(0, 60, 100)));
	REQUIRE_FALSE(same(rawMsg(0x90, 60, 0, 2), noteOff(0, 60)));
	// A note message without its note byte has no address.
	REQUIRE_FALSE(same(rawMsg(0x90, 0, 0, 1), noteOn(0, 60, 100)));
	REQUIRE_FALSE(same(noteOn(0, 60, 100), rawMsg(0x90, 0, 0, 1)));
	REQUIRE_FALSE(same(rawMsg(0xB0, 0, 0, 1), rawMsg(0xB0, 0, 0, 1)));
}

TEST_CASE("Cancel: cancelMatches by mode, groups and plain messages", "[MidiKit][cancel]") {
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
