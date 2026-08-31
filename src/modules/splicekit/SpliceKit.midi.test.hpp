// SpliceKit.midi.test.hpp — MIDI end-to-end through MidiTrackingProcessor (test review 2.4).
// The suite previously invoked processMapUpdate/processMapLearn directly and pre-seeded maps
// with setMap, so nothing proved a real midi::Message fed into trackingProcessor.process()
// actually (a) triggers the mapped cell button, or (b) stores a learned map. These tests feed
// rack::midi::Message objects into the input queue and drain them through
// trackingProcessor.process(frame) — the same pump the module's process() drives — asserting
// the observable outcome (pending cell / patch cable / stored map).

#include "SpliceKit.test.hpp"


// 2.4a — a mapped note/CC actually triggers the cell button.

TEST_CASE("MIDI end-to-end - mapped note arms the cell, second press creates the cable", "[SpliceKit]") {
	CableScaffold cables;
	SpliceKitModule* m = createModule();
	m->assignPort(0, 42, 0, engine::Port::OUTPUT);
	m->assignPort(1, 43, 0, engine::Port::INPUT);
	// As a preset would: cell 0 listens to note 36, cell 1 to note 37.
	m->trackingProcessor.setMap(MidiTrackingType::NOTE, 0, 36);
	m->trackingProcessor.setMap(MidiTrackingType::NOTE, 1, 37);

	// First press: note 36 feeds through the processor and arms cell 0.
	m->trackingProcessor.getInput().onMessage(Test::makeMidiMessage(0x9, 0, 36, 100));
	m->trackingProcessor.process(1);
	m->taskProcessorUi.step();
	REQUIRE(m->pendingCellId == 0);

	// Second press: note 37 toggles the connection between cells 0 and 1.
	m->trackingProcessor.getInput().onMessage(Test::makeMidiMessage(0x9, 0, 37, 100));
	m->trackingProcessor.process(3);
	m->taskProcessorUi.step();
	REQUIRE(m->pendingCellId == -1);
	REQUIRE(m->overlayMessage.title == "Cable created");
	REQUIRE(cables.mock.hasCable(42, 0, 43, 0));

	Test::destroyModule(m);
}

TEST_CASE("MIDI end-to-end - mapped CC triggers the cell button", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	m->assignPort(3, 42, 0, engine::Port::OUTPUT);
	m->trackingProcessor.setMap(MidiTrackingType::CC, 3, 74);

	m->trackingProcessor.getInput().onMessage(Test::makeMidiMessage(0xb, 0, 74, 100));
	m->trackingProcessor.process(1);
	m->taskProcessorUi.step();

	REQUIRE(m->pendingCellId == 3);

	Test::destroyModule(m);
}

TEST_CASE("MIDI end-to-end - unmapped note is ignored", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	m->assignPort(0, 42, 0, engine::Port::OUTPUT);
	// Cell 0 is assigned but nothing is mapped to any note.

	m->trackingProcessor.getInput().onMessage(Test::makeMidiMessage(0x9, 0, 36, 100));
	m->trackingProcessor.process(1);
	m->taskProcessorUi.step();

	REQUIRE(m->pendingCellId == -1);

	Test::destroyModule(m);
}


// 2.4b — MIDI learn actually stores a map.

TEST_CASE("MIDI end-to-end - learn stores the received note as a map", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	m->enableLearn(0);
	REQUIRE(m->trackingProcessor.getMapLearn() == true);

	m->trackingProcessor.getInput().onMessage(Test::makeMidiMessage(0x9, 0, 60, 100));
	m->trackingProcessor.process(1);

	// The map was stored...
	const auto& map = m->trackingProcessor.getMap(0);
	REQUIRE(map.type == MidiTrackingType::NOTE);
	REQUIRE(map.param == 60);
	// ...learn is now off, and the single-learn cursor was cleared.
	REQUIRE(m->trackingProcessor.getMapLearn() == false);
	REQUIRE(m->learningId == -1);

	Test::destroyModule(m);
}


// Edge cases for the MIDI end-to-end flow.

TEST_CASE("MIDI end-to-end - momentary mode: note-off clears the pending cell", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	m->buttonMode = SpliceKitModule::BUTTON_MOMENTARY;
	m->assignPort(0, 42, 0, engine::Port::OUTPUT);
	m->trackingProcessor.setMap(MidiTrackingType::NOTE, 0, 36);

	// NOTE_ON arms cell 0.
	m->trackingProcessor.getInput().onMessage(Test::makeMidiMessage(0x9, 0, 36, 100));
	m->trackingProcessor.process(1);
	m->taskProcessorUi.step();
	REQUIRE(m->pendingCellId == 0);

	// NOTE_OFF (status 0x8) releases it → momentary mode clears the pending selection.
	m->trackingProcessor.getInput().onMessage(Test::makeMidiMessage(0x8, 0, 36, 0));
	m->trackingProcessor.process(1);
	m->taskProcessorUi.step();
	REQUIRE(m->pendingCellId == -1);

	Test::destroyModule(m);
}

TEST_CASE("MIDI end-to-end - pressing the same cell again cancels the selection", "[SpliceKit]") {
	CableScaffold cables;
	SpliceKitModule* m = createModule();
	m->assignPort(0, 42, 0, engine::Port::OUTPUT);
	m->assignPort(1, 43, 0, engine::Port::INPUT);
	m->trackingProcessor.setMap(MidiTrackingType::NOTE, 0, 36);
	m->trackingProcessor.setMap(MidiTrackingType::NOTE, 1, 37);

	// NOTE_ON arms cell 0.
	m->trackingProcessor.getInput().onMessage(Test::makeMidiMessage(0x9, 0, 36, 100));
	m->trackingProcessor.process(1);
	m->taskProcessorUi.step();
	REQUIRE(m->pendingCellId == 0);

	// NOTE_ON the same cell again → triggerCell's pendingCellId == id branch cancels the
	// selection; no toggle is queued, so no cable is created.
	m->trackingProcessor.getInput().onMessage(Test::makeMidiMessage(0x9, 0, 36, 100));
	m->trackingProcessor.process(1);
	m->taskProcessorUi.step();
	REQUIRE(m->pendingCellId == -1);
	REQUIRE(cables.mock.hasCable(42, 0, 43, 0) == false);

	Test::destroyModule(m);
}

TEST_CASE("MIDI end-to-end - learn stores a received CC as a map", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	m->enableLearn(0);
	REQUIRE(m->trackingProcessor.getMapLearn() == true);

	m->trackingProcessor.getInput().onMessage(Test::makeMidiMessage(0xb, 0, 74, 100));
	m->trackingProcessor.process(1);

	const auto& map = m->trackingProcessor.getMap(0);
	REQUIRE(map.type == MidiTrackingType::CC);
	REQUIRE(map.param == 74);
	REQUIRE(m->trackingProcessor.getMapLearn() == false);
	REQUIRE(m->learningId == -1);

	Test::destroyModule(m);
}

TEST_CASE("MIDI end-to-end - a mapped note switches scenes", "[SpliceKit]") {
	SpliceKitModule* m = createModule();
	// Scene button 2 (mapId MATRIX_COUNT + 2) mapped to note 50.
	m->trackingProcessor.setMap(MidiTrackingType::NOTE, MATRIX_COUNT + 2, 50);

	m->trackingProcessor.getInput().onMessage(Test::makeMidiMessage(0x9, 0, 50, 100));
	m->trackingProcessor.process(1);
	m->taskProcessorUi.step();

	// requestSceneChange queued a switchTo(2); draining ran it.
	REQUIRE(m->sceneStore.current == 2);

	Test::destroyModule(m);
}
