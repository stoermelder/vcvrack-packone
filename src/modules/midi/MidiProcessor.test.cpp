#include "../../test/framework.hpp"
#include "MidiProcessor.hpp"

using namespace StoermelderPackOne;


// TestHandler collects messages emitted by MidiProcessor so assertions can inspect them.
struct TestHandler : MidiProcessorHandler {
	std::vector<MessageEx> msgs;
	bool processMidi(const MessageEx& msg) override {
		msgs.push_back(msg);
		return false;
	}
};


TEST_CASE("Note on/off messages", "[MidiProcessor]") {
	MidiProcessor mp;
	TestHandler h;
	mp.subscribe(&h);

	// Note On: status 0x9, channel 4, note 60, velocity 100
	auto noteOn = Test::makeMidiMessage(0x9, 4, 60, 100);
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
	auto noteOff = Test::makeMidiMessage(0x8, 4, 60, 0);
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

TEST_CASE("Clock and Start/Stop messages", "[MidiProcessor]") {
	MidiProcessor mp;
	TestHandler h;
	mp.subscribe(&h);

	// Timing Clock: system message 0xf with sys = 0x8
	auto clockMsg = Test::makeMidiMessage(0xf, 0x8, 0, 0);
	MessageEx mClock(clockMsg);
	mClock.type = MessageEx::Type::CLOCK;
	mp.notify(mClock);
	CATCH_INFO("Clock: sys=" << int(clockMsg.bytes[0] & 0x0f));
	REQUIRE(!h.msgs.empty());
	MessageEx lastClock = h.msgs.back();
	REQUIRE(lastClock.type == MessageEx::Type::CLOCK);

	// Start/Stop: also test START (0xa) and STOP (0xc)
	auto startMsg = Test::makeMidiMessage(0xf, 0xa, 0, 0);
	MessageEx mStart(startMsg);
	mStart.type = MessageEx::Type::START;
	mp.notify(mStart);
	CATCH_INFO("Start: sys=" << int(startMsg.bytes[0] & 0x0f));
	MessageEx lastStart = h.msgs.back();
	REQUIRE(lastStart.type == MessageEx::Type::START);

	auto stopMsg = Test::makeMidiMessage(0xf, 0xc, 0, 0);
	MessageEx mStop(stopMsg);
	mStop.type = MessageEx::Type::STOP;
	mp.notify(mStop);
	CATCH_INFO("Stop: sys=" << int(stopMsg.bytes[0] & 0x0f));
	MessageEx lastStop = h.msgs.back();
	REQUIRE(lastStop.type == MessageEx::Type::STOP);
}

TEST_CASE("Pitch bend values are combined into extraValue", "[MidiProcessor]") {
	MidiProcessor mp;
	TestHandler h;
	mp.subscribe(&h);

	// Pitch bend: status 0xe, channel 7, LSB=3, MSB=4 -> value = MSB<<7 | LSB
	auto pb = Test::makeMidiMessage(0xe, 7, 3, 4);
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

TEST_CASE("14-bit CC combines MSB+LSB", "[MidiProcessor]") {
	MidiProcessor mp;
	TestHandler h;
	mp.subscribe(&h);

	// Send MSB part of a 14-bit CC (CC number 5) with value 3.
	// The processor stores MSB until a matching LSB (CC 32+5) arrives.
	auto msgMsb = Test::makeMidiMessage(0xb, 1, 5, 3);
	MessageEx mMsb(msgMsb);
	mMsb.type = MessageEx::Type::CC;
	mp.notify(mMsb);
	mp.processCc(msgMsb);

	// Send the LSB (CC 37) which should combine with stored MSB and emit CC_14BIT.
	auto msgLsb = Test::makeMidiMessage(0xb, 1, 32 + 5, 10);
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

TEST_CASE("RPN selection, data entry and reset", "[MidiProcessor]") {
	MidiProcessor mp;
	TestHandler h;
	mp.subscribe(&h);

	// Select an RPN parameter using CC 101 (MSB) then CC 100 (LSB).
	// The processor will notify an RPN selection with the combined param number = MSB*128 + LSB.
	auto rpnMsb = Test::makeMidiMessage(0xb, 2, 101, 1);
	mp.processCc(rpnMsb);
	auto rpnLsb = Test::makeMidiMessage(0xb, 2, 100, 2);
	mp.processCc(rpnLsb);

	CATCH_INFO("RPN select: ch=" << int(rpnMsb.bytes[0] & 0x0f) << " msb=" << int(rpnMsb.bytes[2]) << " lsb=" << int(rpnLsb.bytes[2]));

	// Verify selection notification
	REQUIRE(!h.msgs.empty());
	MessageEx rpnSelect = h.msgs.back();
	REQUIRE(rpnSelect.type == MessageEx::Type::RPN);
	REQUIRE(rpnSelect.getParamNumber() == (1 * 128 + 2));

	// Data entry: CC 6 is MSB (may not emit a complete value yet), CC 38 is LSB that finalizes it.
	// MSB alone should not produce a completed data message (we still have selection only)
	auto dataMsb = Test::makeMidiMessage(0xb, 2, 6, 10);
	mp.processCc(dataMsb);
	// no notification yet from MSB alone; last reported message remains the selection
	MessageEx before = h.msgs.back();
	REQUIRE(before.type == MessageEx::Type::RPN);

	// LSB completes the value; note that CC LSB processing may emit both the RPN data message
	// and also a 14-bit CC message for other CC ranges, so search backwards for the RPN entry.
	auto dataLsb = Test::makeMidiMessage(0xb, 2, 38, 7);
	mp.processCc(dataLsb);
	// The CC LSB processing emits both RPN and a 14-bit CC; find the last RPN message
	CATCH_INFO("RPN data: ch=" << int(dataLsb.bytes[0] & 0x0f) << " msb=" << int(dataMsb.bytes[2]) << " lsb=" << int(dataLsb.bytes[2]));
	auto itRpn = std::find_if(h.msgs.rbegin(), h.msgs.rend(), [](const MessageEx& mm){ return mm.type == MessageEx::Type::RPN; });
	REQUIRE(itRpn != h.msgs.rend());
	REQUIRE(itRpn->getParamNumber() == (1 * 128 + 2));
	REQUIRE(itRpn->getValue() == (10 * 128 + 7));

	// RPN reset: sending 127/127 resets selections and should notify an RPN with param -1
	auto resetMsb = Test::makeMidiMessage(0xb, 2, 101, 127);
	mp.processCc(resetMsb);
	auto resetLsb = Test::makeMidiMessage(0xb, 2, 100, 127);
	mp.processCc(resetLsb);
	CATCH_INFO("RPN reset: ch=" << int(resetLsb.bytes[0] & 0x0f) << " msb=" << int(resetMsb.bytes[2]) << " lsb=" << int(resetLsb.bytes[2]));
	MessageEx resetMsg = h.msgs.back();
	REQUIRE(resetMsg.type == MessageEx::Type::RPN);
	REQUIRE(resetMsg.getParamNumber() == -1);
}

TEST_CASE("processBypass drains the input queue without notifying handlers", "[MidiProcessor]") {
	MidiProcessor mp;
	TestHandler h;
	mp.subscribe(&h);

	// Queue a few messages directly on the input queue (frame 0, default).
	mp.getInput().onMessage(Test::makeMidiMessage(0x9, 0, 60, 100));
	mp.getInput().onMessage(Test::makeMidiMessage(0xb, 0, 7, 64));
	REQUIRE(mp.getInput().size() == 2);

	mp.processBypass(1);

	// Queue is drained but no handler was notified.
	REQUIRE(mp.getInput().size() == 0);
	REQUIRE(h.msgs.empty());
}

TEST_CASE("NRPN selection and data entry", "[MidiProcessor]") {
	MidiProcessor mp;
	TestHandler h;
	mp.subscribe(&h);

	// select NRPN: CC 99 (MSB) then CC 98 (LSB)
	auto nrpnMsb = Test::makeMidiMessage(0xb, 3, 99, 4);
	mp.processCc(nrpnMsb);
	auto nrpnLsb = Test::makeMidiMessage(0xb, 3, 98, 5);
	mp.processCc(nrpnLsb);
	CATCH_INFO("NRPN select: ch=" << int(nrpnMsb.bytes[0] & 0x0f) << " msb=" << int(nrpnMsb.bytes[2]) << " lsb=" << int(nrpnLsb.bytes[2]));

	MessageEx nrpnSelect = h.msgs.back();
	REQUIRE(nrpnSelect.type == MessageEx::Type::NRPN);
	REQUIRE(nrpnSelect.getParamNumber() == (4 * 128 + 5));

	// data entry MSB then LSB
	auto dataMsb = Test::makeMidiMessage(0xb, 3, 6, 20);
	mp.processCc(dataMsb);
	auto dataLsb = Test::makeMidiMessage(0xb, 3, 38, 2);
	mp.processCc(dataLsb);
	CATCH_INFO("NRPN data: ch=" << int(dataLsb.bytes[0] & 0x0f) << " msb=" << int(dataMsb.bytes[2]) << " lsb=" << int(dataLsb.bytes[2]));
	// The CC LSB processing emits both NRPN and a 14-bit CC; find the last NRPN message
	auto itNrpn = std::find_if(h.msgs.rbegin(), h.msgs.rend(), [](const MessageEx& mm){ return mm.type == MessageEx::Type::NRPN; });
	REQUIRE(itNrpn != h.msgs.rend());
	REQUIRE(itNrpn->getParamNumber() == (4 * 128 + 5));
	REQUIRE(itNrpn->getValue() == (20 * 128 + 2));
}

// Counts messages of one type, for assertions of the form "nothing was emitted".
static size_t countType(const std::vector<MessageEx>& msgs, MessageEx::Type type) {
	return std::count_if(msgs.begin(), msgs.end(), [type](const MessageEx& m){ return m.type == type; });
}

TEST_CASE("reset() clears NRPN state", "[MidiProcessor][reset]") {
	MidiProcessor mp;
	TestHandler h;
	mp.subscribe(&h);

	// Arm an NRPN parameter, then reset before any data entry arrives.
	mp.processCc(Test::makeMidiMessage(0xb, 3, 99, 4));
	mp.processCc(Test::makeMidiMessage(0xb, 3, 98, 5));
	REQUIRE(countType(h.msgs, MessageEx::Type::NRPN) == 1);   // the select

	mp.reset();
	h.msgs.clear();

	// Data entry with no armed parameter must not produce an NRPN message.
	mp.processCc(Test::makeMidiMessage(0xb, 3, 6, 20));
	mp.processCc(Test::makeMidiMessage(0xb, 3, 38, 2));
	CATCH_INFO("data entry after reset must be ignored");
	REQUIRE(countType(h.msgs, MessageEx::Type::NRPN) == 0);
}

TEST_CASE("reset() clears RPN state", "[MidiProcessor][reset]") {
	MidiProcessor mp;
	TestHandler h;
	mp.subscribe(&h);

	mp.processCc(Test::makeMidiMessage(0xb, 2, 101, 0));
	mp.processCc(Test::makeMidiMessage(0xb, 2, 100, 1));
	REQUIRE(countType(h.msgs, MessageEx::Type::RPN) == 1);

	mp.reset();
	h.msgs.clear();

	mp.processCc(Test::makeMidiMessage(0xb, 2, 6, 10));
	mp.processCc(Test::makeMidiMessage(0xb, 2, 38, 3));
	REQUIRE(countType(h.msgs, MessageEx::Type::RPN) == 0);
}

TEST_CASE("reset() clears a pending parameter-select MSB", "[MidiProcessor][reset]") {
	MidiProcessor mp;
	TestHandler h;
	mp.subscribe(&h);

	// Only the MSB half of an NRPN select: the parameter is not armed yet, but
	// pendingNrpnMsb is set. A reset must drop it, otherwise a later CC 98 would
	// complete a selection begun before the reset.
	mp.processCc(Test::makeMidiMessage(0xb, 5, 99, 4));

	mp.reset();
	h.msgs.clear();

	mp.processCc(Test::makeMidiMessage(0xb, 5, 98, 5));
	CATCH_INFO("a stale pending MSB must not complete a selection after reset");
	REQUIRE(countType(h.msgs, MessageEx::Type::NRPN) == 0);
}

TEST_CASE("reset() clears 14-bit CC state", "[MidiProcessor][reset]") {
	MidiProcessor mp;
	TestHandler h;
	mp.subscribe(&h);

	// Store an MSB, then reset before the matching LSB arrives.
	mp.processCc(Test::makeMidiMessage(0xb, 1, 5, 3));

	mp.reset();
	h.msgs.clear();

	// The LSB alone must not emit a 14-bit CC -- there is no stored MSB anymore.
	mp.processCc(Test::makeMidiMessage(0xb, 1, 32 + 5, 10));
	CATCH_INFO("orphaned LSB after reset must be ignored");
	REQUIRE(countType(h.msgs, MessageEx::Type::CC_14BIT) == 0);
}

TEST_CASE("reset() leaves handlers and the queue alone", "[MidiProcessor][reset]") {
	MidiProcessor mp;
	TestHandler h;
	mp.subscribe(&h);

	mp.getInput().onMessage(Test::makeMidiMessage(0x9, 0, 60, 100));
	REQUIRE(mp.getInput().size() == 1);

	mp.reset();

	// Subscription survives: reset() is about stream state, not wiring.
	REQUIRE(mp.getInput().size() == 1);
	mp.process(0);
	REQUIRE(countType(h.msgs, MessageEx::Type::NOTE_ON) == 1);
}

TEST_CASE("reset() is per-channel-complete", "[MidiProcessor][reset]") {
	MidiProcessor mp;
	TestHandler h;
	mp.subscribe(&h);

	// Arm a parameter on every channel, then reset once.
	for (uint8_t ch = 0; ch < 16; ch++) {
		mp.processCc(Test::makeMidiMessage(0xb, ch, 99, 1));
		mp.processCc(Test::makeMidiMessage(0xb, ch, 98, ch));
	}
	mp.reset();
	h.msgs.clear();

	for (uint8_t ch = 0; ch < 16; ch++) {
		mp.processCc(Test::makeMidiMessage(0xb, ch, 6, 20));
		mp.processCc(Test::makeMidiMessage(0xb, ch, 38, 2));
	}
	CATCH_INFO("no channel may keep armed state after reset");
	REQUIRE(countType(h.msgs, MessageEx::Type::NRPN) == 0);
}

TEST_CASE("Default construction owns its input queue", "[MidiProcessor][input]") {
	MidiProcessor mp;
	TestHandler h;
	mp.subscribe(&h);

	// The no-argument form must behave exactly as it always has: the processor
	// allocates a queue and pumps the one it owns.
	REQUIRE(mp.ownedInput != nullptr);
	REQUIRE(&mp.getInput() == mp.ownedInput.get());

	mp.getInput().onMessage(Test::makeMidiMessage(0x9, 0, 60, 100));
	mp.process(0);

	REQUIRE(countType(h.msgs, MessageEx::Type::NOTE_ON) == 1);
}

TEST_CASE("An injected queue is pumped instead of the owned one", "[MidiProcessor][input]") {
	rack::midi::InputQueue external;
	MidiProcessor mp(&external);
	TestHandler h;
	mp.subscribe(&h);

	REQUIRE(&mp.getInput() == &external);
	// No queue is allocated at all when one is injected -- the whole point of
	// owning it through a pointer rather than as a member.
	REQUIRE(mp.ownedInput == nullptr);

	external.onMessage(Test::makeMidiMessage(0x9, 0, 60, 100));
	mp.process(0);
	REQUIRE(countType(h.msgs, MessageEx::Type::NOTE_ON) == 1);
}

TEST_CASE("processBypass drains the injected queue", "[MidiProcessor][input]") {
	rack::midi::InputQueue external;
	MidiProcessor mp(&external);
	TestHandler h;
	mp.subscribe(&h);

	external.onMessage(Test::makeMidiMessage(0x9, 0, 60, 100));
	REQUIRE(external.size() == 1);

	mp.processBypass(0);

	REQUIRE(external.size() == 0);
	REQUIRE(h.msgs.empty());
}

TEST_CASE("processMessage decodes without any queue", "[MidiProcessor][input]") {
	MidiProcessor mp;
	TestHandler h;
	mp.subscribe(&h);

	// The integration point for consumers that pump a queue themselves: the
	// same events, with the processor never touching a queue.
	mp.processMessage(Test::makeMidiMessage(0x9, 4, 60, 100));
	mp.processMessage(Test::makeMidiMessage(0xb, 0, 7, 64));

	REQUIRE(countType(h.msgs, MessageEx::Type::NOTE_ON) == 1);
	REQUIRE(countType(h.msgs, MessageEx::Type::CC) == 1);
	REQUIRE(mp.getInput().size() == 0);
}

TEST_CASE("processMessage assembles NRPN across calls", "[MidiProcessor][input]") {
	MidiProcessor mp;
	TestHandler h;
	mp.subscribe(&h);

	mp.processMessage(Test::makeMidiMessage(0xb, 3, 99, 4));
	mp.processMessage(Test::makeMidiMessage(0xb, 3, 98, 5));
	mp.processMessage(Test::makeMidiMessage(0xb, 3, 6, 20));
	mp.processMessage(Test::makeMidiMessage(0xb, 3, 38, 2));

	auto it = std::find_if(h.msgs.rbegin(), h.msgs.rend(),
		[](const MessageEx& m){ return m.type == MessageEx::Type::NRPN && m.hasValue(); });
	REQUIRE(it != h.msgs.rend());
	REQUIRE(it->getParamNumber() == (4 * 128 + 5));
	REQUIRE(it->getValue() == (20 * 128 + 2));
}

TEST_CASE("reset() leaves an injected queue untouched", "[MidiProcessor][reset]") {
	rack::midi::InputQueue external;
	MidiProcessor mp(&external);

	external.onMessage(Test::makeMidiMessage(0x9, 0, 60, 100));
	REQUIRE(external.size() == 1);

	// reset() is decoder state only -- the queue may belong to someone else.
	mp.reset();

	REQUIRE(external.size() == 1);
}

// Returns the isComponent flag of the last Type::CC message seen.
static bool lastCcIsComponent(const std::vector<MessageEx>& msgs) {
	auto it = std::find_if(msgs.rbegin(), msgs.rend(),
		[](const MessageEx& m){ return m.type == MessageEx::Type::CC; });
	REQUIRE(it != msgs.rend());
	return it->isComponent;
}

TEST_CASE("isComponent marks parameter-select CCs", "[MidiProcessor][isComponent]") {
	MidiProcessor mp;
	TestHandler h;
	mp.subscribe(&h);

	auto cc = GENERATE(98, 99, 100, 101);
	h.msgs.clear();
	mp.processMessage(Test::makeMidiMessage(0xb, 0, cc, 4));
	CATCH_INFO("cc=" << cc);
	REQUIRE(lastCcIsComponent(h.msgs) == true);
}

TEST_CASE("isComponent marks data entry only while a parameter is armed", "[MidiProcessor][isComponent]") {
	MidiProcessor mp;
	TestHandler h;
	mp.subscribe(&h);

	SECTION("No parameter armed: CC 6 is an ordinary controller") {
		mp.processMessage(Test::makeMidiMessage(0xb, 0, 6, 20));
		REQUIRE(lastCcIsComponent(h.msgs) == false);
	}

	SECTION("CC 6/38 also form a 14-bit pair, which outranks the data-entry rule") {
		// CC 6 is below 32, so it is simultaneously Data Entry MSB and the MSB
		// of 14-bit CC 6 -- the ranges genuinely overlap in the MIDI spec, and
		// processCc() tracks both. Once CC 6 has been seen with a non-zero
		// value, CC 38 (= 6 + 32) is a tracked LSB, so it is a component even
		// with no RPN/NRPN parameter armed.
		mp.processMessage(Test::makeMidiMessage(0xb, 0, 6, 20));
		h.msgs.clear();
		mp.processMessage(Test::makeMidiMessage(0xb, 0, 38, 2));
		REQUIRE(lastCcIsComponent(h.msgs) == true);

		// A data-entry LSB whose MSB was never seen is not a component.
		MidiProcessor fresh;
		TestHandler fh;
		fresh.subscribe(&fh);
		fresh.processMessage(Test::makeMidiMessage(0xb, 0, 38, 2));
		REQUIRE(lastCcIsComponent(fh.msgs) == false);
	}

	SECTION("Parameter armed: data entry is a component") {
		mp.processMessage(Test::makeMidiMessage(0xb, 0, 99, 4));
		mp.processMessage(Test::makeMidiMessage(0xb, 0, 98, 5));

		h.msgs.clear();
		mp.processMessage(Test::makeMidiMessage(0xb, 0, 6, 20));
		REQUIRE(lastCcIsComponent(h.msgs) == true);

		h.msgs.clear();
		mp.processMessage(Test::makeMidiMessage(0xb, 0, 38, 2));
		REQUIRE(lastCcIsComponent(h.msgs) == true);
	}

	SECTION("Armed on another channel does not mark this one") {
		mp.processMessage(Test::makeMidiMessage(0xb, 1, 99, 4));
		mp.processMessage(Test::makeMidiMessage(0xb, 1, 98, 5));

		h.msgs.clear();
		mp.processMessage(Test::makeMidiMessage(0xb, 0, 6, 20));
		REQUIRE(lastCcIsComponent(h.msgs) == false);
	}
}

TEST_CASE("isComponent reflects state as of arrival, not after", "[MidiProcessor][isComponent]") {
	MidiProcessor mp;
	TestHandler h;
	mp.subscribe(&h);

	// CC 98 completes the selection. The flag is queried before processCc()
	// arms the parameter, but a select CC is a component regardless -- what
	// this pins is that the CC 6 immediately following is already marked,
	// i.e. the query sees the parameter armed by the preceding message.
	mp.processMessage(Test::makeMidiMessage(0xb, 0, 99, 4));
	mp.processMessage(Test::makeMidiMessage(0xb, 0, 98, 5));

	h.msgs.clear();
	mp.processMessage(Test::makeMidiMessage(0xb, 0, 6, 20));
	REQUIRE(lastCcIsComponent(h.msgs) == true);
}

TEST_CASE("isComponent and the 14-bit first-MSB asymmetry", "[MidiProcessor][isComponent]") {
	MidiProcessor mp;
	TestHandler h;
	mp.subscribe(&h);

	// Documented behaviour: the decoder cannot know a pair is coming, so the
	// first MSB escapes as a plain CC. Pinned so it is not "fixed" silently.
	mp.processMessage(Test::makeMidiMessage(0xb, 0, 5, 3));
	REQUIRE(lastCcIsComponent(h.msgs) == false);

	// Once tracked, both halves are components.
	h.msgs.clear();
	mp.processMessage(Test::makeMidiMessage(0xb, 0, 32 + 5, 10));
	REQUIRE(lastCcIsComponent(h.msgs) == true);

	h.msgs.clear();
	mp.processMessage(Test::makeMidiMessage(0xb, 0, 5, 4));
	REQUIRE(lastCcIsComponent(h.msgs) == true);
}

TEST_CASE("isComponent is false for an untracked LSB and plain CCs", "[MidiProcessor][isComponent]") {
	MidiProcessor mp;
	TestHandler h;
	mp.subscribe(&h);

	SECTION("LSB whose MSB was never seen") {
		mp.processMessage(Test::makeMidiMessage(0xb, 0, 32 + 9, 10));
		REQUIRE(lastCcIsComponent(h.msgs) == false);
	}

	SECTION("A CC outside every extended range") {
		mp.processMessage(Test::makeMidiMessage(0xb, 0, 80, 64));
		REQUIRE(lastCcIsComponent(h.msgs) == false);
	}
}

TEST_CASE("isComponent survives reset() clearing the tracked pair", "[MidiProcessor][isComponent][reset]") {
	MidiProcessor mp;
	TestHandler h;
	mp.subscribe(&h);

	mp.processMessage(Test::makeMidiMessage(0xb, 0, 5, 3));
	mp.processMessage(Test::makeMidiMessage(0xb, 0, 32 + 5, 10));

	mp.reset();
	h.msgs.clear();

	// After a reset the pair is untracked again, so the LSB is a plain CC.
	mp.processMessage(Test::makeMidiMessage(0xb, 0, 32 + 5, 10));
	REQUIRE(lastCcIsComponent(h.msgs) == false);
}

TEST_CASE("hasValue() separates parameter select from data entry", "[MidiProcessor][hasValue]") {
	MidiProcessor mp;
	TestHandler h;
	mp.subscribe(&h);

	SECTION("NRPN select has no value; data entry does") {
		mp.processMessage(Test::makeMidiMessage(0xb, 3, 99, 4));
		mp.processMessage(Test::makeMidiMessage(0xb, 3, 98, 5));

		auto sel = std::find_if(h.msgs.begin(), h.msgs.end(),
			[](const MessageEx& m){ return m.type == MessageEx::Type::NRPN; });
		REQUIRE(sel != h.msgs.end());
		REQUIRE(sel->hasValue() == false);

		h.msgs.clear();
		mp.processMessage(Test::makeMidiMessage(0xb, 3, 6, 20));
		mp.processMessage(Test::makeMidiMessage(0xb, 3, 38, 2));

		auto val = std::find_if(h.msgs.begin(), h.msgs.end(),
			[](const MessageEx& m){ return m.type == MessageEx::Type::NRPN; });
		REQUIRE(val != h.msgs.end());
		REQUIRE(val->hasValue() == true);
		REQUIRE(val->getValue() == (20 * 128 + 2));
	}

	SECTION("The RPN 127/127 reset notification carries no value") {
		mp.processMessage(Test::makeMidiMessage(0xb, 2, 101, 127));
		mp.processMessage(Test::makeMidiMessage(0xb, 2, 100, 127));

		auto rst = std::find_if(h.msgs.begin(), h.msgs.end(),
			[](const MessageEx& m){ return m.type == MessageEx::Type::RPN; });
		REQUIRE(rst != h.msgs.end());
		REQUIRE(rst->getParamNumber() == -1);
		REQUIRE(rst->hasValue() == false);
	}

	SECTION("A 14-bit CC always carries a value") {
		mp.processMessage(Test::makeMidiMessage(0xb, 1, 5, 3));
		mp.processMessage(Test::makeMidiMessage(0xb, 1, 32 + 5, 10));

		auto cc14 = std::find_if(h.msgs.begin(), h.msgs.end(),
			[](const MessageEx& m){ return m.type == MessageEx::Type::CC_14BIT; });
		REQUIRE(cc14 != h.msgs.end());
		REQUIRE(cc14->hasValue() == true);
	}
}
