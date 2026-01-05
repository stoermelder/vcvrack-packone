#include "catch2/plugin.hpp"
#include "../modules/midi/MidiTrackingProcessor.hpp"
#include "../modules/midi/MidiTrackingProcessor.cpp"  // Include implementation for templates

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
struct TestHandler : MidiTrackingProcessorHandler {
	std::vector<std::pair<MidiTrackingType, uint16_t>> learned;
	std::vector<std::tuple<MidiTrackingType, uint16_t, uint16_t>> updates;

	void processMapLearn(MidiTrackingType type, uint16_t mapId) override {
		learned.emplace_back(type, mapId);
	}
	void processMapUpdate(MidiTrackingType type, uint16_t mapId, uint16_t value) override {
		updates.emplace_back(type, mapId, value);
	}
};


TEST_CASE("CC map triggers updates", "[MidiTrackingProcessor]") {
	MidiTrackingProcessor<19> p;
	TestHandler h;
	p.handler = &h;
	p.enableCc();

	// Map mapId=1 to CC number 10
	p.setMap(MidiTrackingType::CC, 1, 10);

	// Send CC 10 with value 55
	auto msg = makeMidiMessage(0xb, 0, 10, 55);
	MessageEx me(msg);
	me.type = MessageEx::Type::CC;
	p.midiProcessor.notify(me);

	REQUIRE(h.updates.size() == 1);
	REQUIRE(std::get<0>(h.updates[0]) == MidiTrackingType::CC);
	REQUIRE(std::get<1>(h.updates[0]) == 1);
	REQUIRE(std::get<2>(h.updates[0]) == 55);
}

TEST_CASE("Note map on/off updates and velocity", "[MidiTrackingProcessor]") {
	MidiTrackingProcessor<19> p;
	TestHandler h;
	p.handler = &h;
	p.enableNotes();

	// Map mapId=2 to note 60
	p.setMap(MidiTrackingType::NOTE, 2, 60);

	// Note on with velocity 100 should send update with value=100
	auto on = makeMidiMessage(0x9, 1, 60, 100);
	MessageEx mo(on);
	mo.type = MessageEx::Type::NOTE_ON;
	p.midiProcessor.notify(mo);

	REQUIRE(!h.updates.empty());
	REQUIRE(std::get<0>(h.updates[0]) == MidiTrackingType::NOTE);
	REQUIRE(std::get<1>(h.updates[0]) == 2);
	REQUIRE(std::get<2>(h.updates[0]) == 100);

	// Note off should send value 0
	auto off = makeMidiMessage(0x8, 1, 60, 0);
	MessageEx moff(off);
	moff.type = MessageEx::Type::NOTE_OFF;
	p.midiProcessor.notify(moff);
	REQUIRE(std::get<2>(h.updates.back()) == 0);
}

TEST_CASE("CC learn and then update", "[MidiTrackingProcessor]") {
	MidiTrackingProcessor<19> p;
	TestHandler h;
	p.handler = &h;
	p.enableCc();

	// Enable learn for mapId=3
	p.enableMapLearn(3);
	REQUIRE(p.getMapLearn() == true);

	// Send CC 77 with non-zero value to trigger learning
	auto m = makeMidiMessage(0xb, 0, 77, 10);
	MessageEx me(m);
	me.type = MessageEx::Type::CC;
	p.midiProcessor.notify(me);

	// Handler must have received processMapLearn
	REQUIRE(h.learned.size() == 1);
	REQUIRE(h.learned[0].first == MidiTrackingType::CC);
	REQUIRE(h.learned[0].second == 3);

	// After learning, subsequent CC 77 should create updates for mapId 3
	h.learned.clear();
	h.updates.clear();
	auto m2 = makeMidiMessage(0xb, 0, 77, 99);
	MessageEx me2(m2);
	me2.type = MessageEx::Type::CC;
	p.midiProcessor.notify(me2);
	REQUIRE(h.updates.size() == 1);
	REQUIRE(std::get<0>(h.updates[0]) == MidiTrackingType::CC);
	REQUIRE(std::get<1>(h.updates[0]) == 3);
	REQUIRE(std::get<2>(h.updates[0]) == 99);
}

TEST_CASE("Note learn and disable learn behavior", "[MidiTrackingProcessor]") {
	MidiTrackingProcessor<19> p;
	TestHandler h;
	p.handler = &h;
	p.enableNotes();

	p.enableMapLearn(1);
	REQUIRE(p.getMapLearn() == true);

	// Send note 40 with vel>0 to learn
	auto m = makeMidiMessage(0x9, 0, 40, 8);
	MessageEx me(m);
	me.type = MessageEx::Type::NOTE_ON;
	p.midiProcessor.notify(me);
	REQUIRE(h.learned.size() == 1);
	REQUIRE(h.learned[0].first == MidiTrackingType::NOTE);
	REQUIRE(h.learned[0].second == 1);

	// disable learn explicitly and ensure state is false
	p.disableMapLearn(1);
	REQUIRE(p.getMapLearn() == false);
}

TEST_CASE("Mapping persistence", "[MidiTrackingProcessor]") {
	MidiTrackingProcessor<19> p;
	TestHandler h;
	p.handler = &h;
	p.enableCc();
	p.enableNotes();

	p.setMap(MidiTrackingType::CC, 0, 11);
	p.setMap(MidiTrackingType::NOTE, 1, 61);

	// Verify getMap returns set values
	auto m0 = p.getMap(0);
	REQUIRE(m0.type == MidiTrackingType::CC);
	REQUIRE(m0.param == 11);
	auto m1 = p.getMap(1);
	REQUIRE(m1.type == MidiTrackingType::NOTE);
	REQUIRE(m1.param == 61);

	// Clear map 0 and verify
	p.clearMap(0);
	auto c0 = p.getMap(0);
	REQUIRE(c0.type == MidiTrackingType::NONE);
	REQUIRE(c0.param == 0);

	// JSON roundtrip: save and restore into a fresh processor
	auto j = p.dataToJson();
	MidiTrackingProcessor<19> p2;
	TestHandler h2;
	p2.handler = &h2;
	p2.enableCc();
	p2.enableNotes();
	p2.dataFromJson(j);

	// After restoring, map 1 (note) should still exist
	auto r1 = p2.getMap(1);
	REQUIRE(r1.type == MidiTrackingType::NOTE);
	REQUIRE(r1.param == 61);
	// map 0 was cleared so should be NONE
	auto r0 = p2.getMap(0);
	REQUIRE(r0.type == MidiTrackingType::NONE);
}
