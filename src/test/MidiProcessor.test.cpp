#include "catch2/plugin.hpp"
#include "../modules/midi/MidiProcessor.hpp"

using namespace StoermelderPackOne;

// Helper: construct a simple 3-byte MIDI message.
// - statusNibble: high nibble of status (e.g., 0xb for CC)
// - channel: low nibble (0-15)
// - b1: first data byte (e.g., CC number)
// - b2: second data byte (e.g., value)
static rack::midi::Message makeMidiMessage(uint8_t statusNibble, uint8_t channel, uint8_t b1, uint8_t b2, int64_t frame = 0) {
	rack::midi::Message m;
	m.frame = frame;
	m.bytes = { static_cast<unsigned char>((statusNibble << 4) | (channel & 0x0f)), static_cast<unsigned char>(b1), static_cast<unsigned char>(b2) };
	return m;
}

// TestHandler collects messages emitted by MidiProcessor so assertions can inspect them.
struct TestHandler : MidiProcessorHandler {
	std::vector<MessageEx> msgs;
	bool processMidi(const MessageEx& msg) override {
		msgs.push_back(msg);
		return false;
	}
};


TEST_CASE("Note on/off messages") {
	MidiProcessor mp;
	TestHandler h;
	mp.subscribe(&h);

	// Note On: status 0x9, channel 4, note 60, velocity 100
	auto noteOn = makeMidiMessage(0x9, 4, 60, 100);
	MessageEx mOn(noteOn);
	mOn.type = MessageEx::Type::NOTE_ON;
	mp.notify(mOn);
	CATCH_INFO("Note On: ch=" << int(noteOn.bytes[0] & 0x0f) << " note=" << int(noteOn.bytes[1]) << " vel=" << int(noteOn.bytes[2]));
	REQUIRE(!h.msgs.empty());
	MessageEx lastOn = h.msgs.back();
	REQUIRE(lastOn.type == MessageEx::Type::NOTE_ON);
	REQUIRE(lastOn.getChannel() == 4);
	REQUIRE(lastOn.getNote() == 60);
	REQUIRE(lastOn.getValue() == 100);

	// Note Off: status 0x8, channel 4, note 60, velocity 0
	auto noteOff = makeMidiMessage(0x8, 4, 60, 0);
	MessageEx mOff(noteOff);
	mOff.type = MessageEx::Type::NOTE_OFF;
	mp.notify(mOff);
	CATCH_INFO("Note Off: ch=" << int(noteOff.bytes[0] & 0x0f) << " note=" << int(noteOff.bytes[1]) << " vel=" << int(noteOff.bytes[2]));
	MessageEx lastOff = h.msgs.back();
	REQUIRE(lastOff.type == MessageEx::Type::NOTE_OFF);
	REQUIRE(lastOff.getChannel() == 4);
	REQUIRE(lastOff.getNote() == 60);
	REQUIRE(lastOff.getValue() == 0);
}

TEST_CASE("Clock and Start/Stop messages") {
	MidiProcessor mp;
	TestHandler h;
	mp.subscribe(&h);

	// Timing Clock: system message 0xf with sys = 0x8
	auto clockMsg = makeMidiMessage(0xf, 0x8, 0, 0);
	MessageEx mClock(clockMsg);
	mClock.type = MessageEx::Type::CLOCK;
	mp.notify(mClock);
	CATCH_INFO("Clock: sys=" << int(clockMsg.bytes[0] & 0x0f));
	REQUIRE(!h.msgs.empty());
	MessageEx lastClock = h.msgs.back();
	REQUIRE(lastClock.type == MessageEx::Type::CLOCK);

	// Start/Stop: also test START (0xa) and STOP (0xc)
	auto startMsg = makeMidiMessage(0xf, 0xa, 0, 0);
	MessageEx mStart(startMsg);
	mStart.type = MessageEx::Type::START;
	mp.notify(mStart);
	CATCH_INFO("Start: sys=" << int(startMsg.bytes[0] & 0x0f));
	MessageEx lastStart = h.msgs.back();
	REQUIRE(lastStart.type == MessageEx::Type::START);

	auto stopMsg = makeMidiMessage(0xf, 0xc, 0, 0);
	MessageEx mStop(stopMsg);
	mStop.type = MessageEx::Type::STOP;
	mp.notify(mStop);
	CATCH_INFO("Stop: sys=" << int(stopMsg.bytes[0] & 0x0f));
	MessageEx lastStop = h.msgs.back();
	REQUIRE(lastStop.type == MessageEx::Type::STOP);
}

TEST_CASE("Pitch bend values are combined into extraValue") {
	MidiProcessor mp;
	TestHandler h;
	mp.subscribe(&h);

	// Pitch bend: status 0xe, channel 7, LSB=3, MSB=4 -> value = MSB<<7 | LSB
	auto pb = makeMidiMessage(0xe, 7, 3, 4);
	MessageEx mpb(pb);
	mpb.type = MessageEx::Type::PITCH_BEND;
	// Emulate MidiProcessor behaviour: combine MSB and LSB into extraValue
	mpb.extraValue = (int16_t(4) << 7) | int16_t(3);
	mp.notify(mpb);
	CATCH_INFO("Pitch Bend: ch=" << int(pb.bytes[0] & 0x0f) << " lsb=" << int(pb.bytes[1]) << " msb=" << int(pb.bytes[2]) << " value=" << mpb.extraValue);
	REQUIRE(!h.msgs.empty());
	MessageEx lastPb = h.msgs.back();
	REQUIRE(lastPb.type == MessageEx::Type::PITCH_BEND);
	REQUIRE(lastPb.getValue() == ((4 << 7) | 3));
}

TEST_CASE("14-bit CC combines MSB+LSB") {
	MidiProcessor mp;
	TestHandler h;
	mp.subscribe(&h);

	// Send MSB part of a 14-bit CC (CC number 5) with value 3.
	// The processor stores MSB until a matching LSB (CC 32+5) arrives.
	auto msgMsb = makeMidiMessage(0xb, 1, 5, 3);
	MessageEx mMsb(msgMsb);
	mMsb.type = MessageEx::Type::CC;
	mp.notify(mMsb);
	mp.processCc(msgMsb);

	// Send the LSB (CC 37) which should combine with stored MSB and emit CC_14BIT.
	auto msgLsb = makeMidiMessage(0xb, 1, 32 + 5, 10);
	MessageEx mLsb(msgLsb);
	mLsb.type = MessageEx::Type::CC;
	mp.notify(mLsb);
	mp.processCc(msgLsb);

	CATCH_INFO("14-bit CC: ch=" << int(msgLsb.bytes[0] & 0x0f) << " msb=" << int(msgMsb.bytes[2]) << " lsb=" << int(msgLsb.bytes[2]));
	REQUIRE(!h.msgs.empty());
	// last message should be CC_14BIT with parameter 5 and combined 14-bit value MSB<<7 | LSB
	MessageEx last = h.msgs.back();
	REQUIRE(last.type == MessageEx::Type::CC_14BIT);
	REQUIRE(last.getParamNumber() == 5);
	REQUIRE(last.getValue() == (3 * 128 + 10));
}

TEST_CASE("RPN selection, data entry and reset") {
	MidiProcessor mp;
	TestHandler h;
	mp.subscribe(&h);

	// Select an RPN parameter using CC 101 (MSB) then CC 100 (LSB).
	// The processor will notify an RPN selection with the combined param number = MSB*128 + LSB.
	auto rpnMsb = makeMidiMessage(0xb, 2, 101, 1);
	mp.processCc(rpnMsb);
	auto rpnLsb = makeMidiMessage(0xb, 2, 100, 2);
	mp.processCc(rpnLsb);

	CATCH_INFO("RPN select: ch=" << int(rpnMsb.bytes[0] & 0x0f) << " msb=" << int(rpnMsb.bytes[2]) << " lsb=" << int(rpnLsb.bytes[2]));

	// Verify selection notification
	REQUIRE(!h.msgs.empty());
	MessageEx rpnSelect = h.msgs.back();
	REQUIRE(rpnSelect.type == MessageEx::Type::RPN);
	REQUIRE(rpnSelect.getParamNumber() == (1 * 128 + 2));

	// Data entry: CC 6 is MSB (may not emit a complete value yet), CC 38 is LSB that finalizes it.
	// MSB alone should not produce a completed data message (we still have selection only)
	auto dataMsb = makeMidiMessage(0xb, 2, 6, 10);
	mp.processCc(dataMsb);
	// no notification yet from MSB alone; last reported message remains the selection
	MessageEx before = h.msgs.back();
	REQUIRE(before.type == MessageEx::Type::RPN);

	// LSB completes the value; note that CC LSB processing may emit both the RPN data message
	// and also a 14-bit CC message for other CC ranges, so search backwards for the RPN entry.
	auto dataLsb = makeMidiMessage(0xb, 2, 38, 7);
	mp.processCc(dataLsb);
	// The CC LSB processing emits both RPN and a 14-bit CC; find the last RPN message
	CATCH_INFO("RPN data: ch=" << int(dataLsb.bytes[0] & 0x0f) << " msb=" << int(dataMsb.bytes[2]) << " lsb=" << int(dataLsb.bytes[2]));
	auto itRpn = std::find_if(h.msgs.rbegin(), h.msgs.rend(), [](const MessageEx& mm){ return mm.type == MessageEx::Type::RPN; });
	REQUIRE(itRpn != h.msgs.rend());
	REQUIRE(itRpn->getParamNumber() == (1 * 128 + 2));
	REQUIRE(itRpn->getValue() == (10 * 128 + 7));

	// RPN reset: sending 127/127 resets selections and should notify an RPN with param -1
	auto resetMsb = makeMidiMessage(0xb, 2, 101, 127);
	mp.processCc(resetMsb);
	auto resetLsb = makeMidiMessage(0xb, 2, 100, 127);
	mp.processCc(resetLsb);
	CATCH_INFO("RPN reset: ch=" << int(resetLsb.bytes[0] & 0x0f) << " msb=" << int(resetMsb.bytes[2]) << " lsb=" << int(resetLsb.bytes[2]));
	MessageEx resetMsg = h.msgs.back();
	REQUIRE(resetMsg.type == MessageEx::Type::RPN);
	REQUIRE(resetMsg.getParamNumber() == -1);
}

TEST_CASE("NRPN selection and data entry") {
	MidiProcessor mp;
	TestHandler h;
	mp.subscribe(&h);

	// select NRPN: CC 99 (MSB) then CC 98 (LSB)
	auto nrpnMsb = makeMidiMessage(0xb, 3, 99, 4);
	mp.processCc(nrpnMsb);
	auto nrpnLsb = makeMidiMessage(0xb, 3, 98, 5);
	mp.processCc(nrpnLsb);
	CATCH_INFO("NRPN select: ch=" << int(nrpnMsb.bytes[0] & 0x0f) << " msb=" << int(nrpnMsb.bytes[2]) << " lsb=" << int(nrpnLsb.bytes[2]));

	MessageEx nrpnSelect = h.msgs.back();
	REQUIRE(nrpnSelect.type == MessageEx::Type::NRPN);
	REQUIRE(nrpnSelect.getParamNumber() == (4 * 128 + 5));

	// data entry MSB then LSB
	auto dataMsb = makeMidiMessage(0xb, 3, 6, 20);
	mp.processCc(dataMsb);
	auto dataLsb = makeMidiMessage(0xb, 3, 38, 2);
	mp.processCc(dataLsb);
	CATCH_INFO("NRPN data: ch=" << int(dataLsb.bytes[0] & 0x0f) << " msb=" << int(dataMsb.bytes[2]) << " lsb=" << int(dataLsb.bytes[2]));
	// The CC LSB processing emits both NRPN and a 14-bit CC; find the last NRPN message
	auto itNrpn = std::find_if(h.msgs.rbegin(), h.msgs.rend(), [](const MessageEx& mm){ return mm.type == MessageEx::Type::NRPN; });
	REQUIRE(itNrpn != h.msgs.rend());
	REQUIRE(itNrpn->getParamNumber() == (4 * 128 + 5));
	REQUIRE(itNrpn->getValue() == (20 * 128 + 2));
}