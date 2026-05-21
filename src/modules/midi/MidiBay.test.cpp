#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "MidiBay.cpp"

using namespace StoermelderPackOne;
using namespace StoermelderPackOne::MidiBay;

SYNC_MODEL(modelMidiBay, "MidiBay");
Test::TestContext<> testContext;


TEST_CASE("Construction and initialization", "[MidiBay]") {
	MidiBayModule* m = Test::createModule<MidiBayModule>("MidiBay");
	MidiBayWidget* mw = Test::createWidget<MidiBayWidget>("MidiBay");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);
	REQUIRE(m->currentScene == 0);
	REQUIRE(m->pendingCellId == -1);
	REQUIRE(m->buttonMode == MidiBayModule::BUTTON_TOGGLE);
	REQUIRE(m->overlayEnabled == true);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}


TEST_CASE("isConnected and setConnection bitmask", "[MidiBay]") {
	MidiBayModule* m = Test::createModule<MidiBayModule>("MidiBay");

	REQUIRE(m->isConnected(0, 0, 1) == false);

	m->setConnection(0, 0, 1, true);
	REQUIRE(m->isConnected(0, 0, 1) == true);
	REQUIRE(m->isConnected(0, 1, 0) == true);  // symmetric

	m->setConnection(0, 0, 1, false);
	REQUIRE(m->isConnected(0, 0, 1) == false);
	REQUIRE(m->isConnected(0, 1, 0) == false);

	// Different scene is unaffected
	REQUIRE(m->isConnected(1, 0, 1) == false);

	Test::destroyModule(m);
}


TEST_CASE("clearPending resets pendingCellId and pendingCellIsPhysical", "[MidiBay]") {
	MidiBayModule* m = Test::createModule<MidiBayModule>("MidiBay");

	m->pendingCellId = 5;
	m->pendingCellIsPhysical = true;
	m->clearPending();

	REQUIRE(m->pendingCellId == -1);
	REQUIRE(m->pendingCellIsPhysical == false);

	Test::destroyModule(m);
}


TEST_CASE("triggerCell - first press sets pending", "[MidiBay]") {
	MidiBayModule* m = Test::createModule<MidiBayModule>("MidiBay");

	// Assign cell 3 a port so triggerCell doesn't bail early
	m->portAssignments[3].moduleId = 42;
	m->portAssignments[3].portId   = 0;
	m->portAssignments[3].type     = engine::Port::OUTPUT;

	REQUIRE(m->pendingCellId == -1);
	m->triggerCell(3);
	REQUIRE(m->pendingCellId == 3);

	Test::destroyModule(m);
}


TEST_CASE("triggerCell - pressing same cell cancels pending", "[MidiBay]") {
	MidiBayModule* m = Test::createModule<MidiBayModule>("MidiBay");

	m->portAssignments[2].moduleId = 42;
	m->portAssignments[2].portId   = 0;
	m->portAssignments[2].type     = engine::Port::OUTPUT;

	m->triggerCell(2);
	REQUIRE(m->pendingCellId == 2);

	m->triggerCell(2);
	REQUIRE(m->pendingCellId == -1);

	Test::destroyModule(m);
}


TEST_CASE("triggerCell - unassigned cell is ignored", "[MidiBay]") {
	MidiBayModule* m = Test::createModule<MidiBayModule>("MidiBay");

	// Cell 10 has no port assignment
	m->triggerCell(10);
	REQUIRE(m->pendingCellId == -1);

	Test::destroyModule(m);
}


TEST_CASE("processMapUpdate - MIDI note-on sets pending", "[MidiBay]") {
	MidiBayModule* m = Test::createModule<MidiBayModule>("MidiBay");

	m->portAssignments[5].moduleId = 1;
	m->portAssignments[5].portId   = 0;
	m->portAssignments[5].type     = engine::Port::OUTPUT;

	m->processMapUpdate(MidiTrackingType::NOTE, 5, 100);
	REQUIRE(m->pendingCellId == 5);
	REQUIRE(m->pendingCellIsPhysical == false);

	Test::destroyModule(m);
}


TEST_CASE("processMapUpdate - momentary MIDI note-off clears pending", "[MidiBay]") {
	MidiBayModule* m = Test::createModule<MidiBayModule>("MidiBay");

	m->buttonMode = MidiBayModule::BUTTON_MOMENTARY;
	m->portAssignments[7].moduleId = 1;
	m->portAssignments[7].portId   = 0;
	m->portAssignments[7].type     = engine::Port::OUTPUT;

	// Note-on: cell 7 becomes pending
	m->processMapUpdate(MidiTrackingType::NOTE, 7, 80);
	REQUIRE(m->pendingCellId == 7);

	// Note-off: momentary mode must clear it
	m->processMapUpdate(MidiTrackingType::NOTE, 7, 0);
	REQUIRE(m->pendingCellId == -1);

	Test::destroyModule(m);
}


TEST_CASE("processMapUpdate - toggle mode ignores note-off", "[MidiBay]") {
	MidiBayModule* m = Test::createModule<MidiBayModule>("MidiBay");

	m->buttonMode = MidiBayModule::BUTTON_TOGGLE;
	m->portAssignments[4].moduleId = 1;
	m->portAssignments[4].portId   = 0;
	m->portAssignments[4].type     = engine::Port::OUTPUT;

	m->processMapUpdate(MidiTrackingType::NOTE, 4, 64);
	REQUIRE(m->pendingCellId == 4);

	// Note-off in toggle mode must NOT clear pending
	m->processMapUpdate(MidiTrackingType::NOTE, 4, 0);
	REQUIRE(m->pendingCellId == 4);

	Test::destroyModule(m);
}


TEST_CASE("JSON roundtrip preserves scene and button mode", "[MidiBay]") {
	MidiBayModule* m = Test::createModule<MidiBayModule>("MidiBay");

	m->currentScene    = 3;
	m->buttonMode      = MidiBayModule::BUTTON_MOMENTARY;
	m->overlayEnabled  = false;
	m->setConnection(3, 1, 5, true);

	json_t* j = m->dataToJson();

	MidiBayModule* m2 = Test::createModule<MidiBayModule>("MidiBay");
	m2->dataFromJson(j);
	json_decref(j);

	REQUIRE(m2->currentScene   == 3);
	REQUIRE(m2->buttonMode     == MidiBayModule::BUTTON_MOMENTARY);
	REQUIRE(m2->overlayEnabled == false);
	REQUIRE(m2->isConnected(3, 1, 5) == true);
	REQUIRE(m2->isConnected(3, 5, 1) == true);  // symmetric
	REQUIRE(m2->isConnected(0, 1, 5) == false);  // other scene untouched

	Test::destroyModule(m2);
	Test::destroyModule(m);
}


TEST_CASE("JSON roundtrip preserves MIDI maps", "[MidiBay]") {
	MidiBayModule* m = Test::createModule<MidiBayModule>("MidiBay");

	m->trackingProcessor.setMap(MidiTrackingType::NOTE, 0, 36);
	m->trackingProcessor.setMap(MidiTrackingType::CC,   1, 74);

	json_t* j = m->dataToJson();

	MidiBayModule* m2 = Test::createModule<MidiBayModule>("MidiBay");
	m2->dataFromJson(j);
	json_decref(j);

	auto map0 = m2->trackingProcessor.getMap(0);
	REQUIRE(map0.type  == MidiTrackingType::NOTE);
	REQUIRE(map0.param == 36);

	auto map1 = m2->trackingProcessor.getMap(1);
	REQUIRE(map1.type  == MidiTrackingType::CC);
	REQUIRE(map1.param == 74);

	Test::destroyModule(m2);
	Test::destroyModule(m);
}


TEST_CASE("overlayEnabled gates setOverlayMessage", "[MidiBay]") {
	MidiBayModule* m = Test::createModule<MidiBayModule>("MidiBay");

	m->overlayEnabled  = false;
	m->overlayMessageId = -1;
	m->setOverlayMessage("Title", "Sub");
	REQUIRE(m->overlayMessageId == -1);  // not triggered

	m->overlayEnabled = true;
	m->setOverlayMessage("Title", "Sub");
	REQUIRE(m->overlayMessageId == 0);  // triggered

	Test::destroyModule(m);
}


TEST_CASE("enableLearn and disableLearn", "[MidiBay]") {
	MidiBayModule* m = Test::createModule<MidiBayModule>("MidiBay");

	REQUIRE(m->learningId == -1);

	m->enableLearn(5);
	REQUIRE(m->learningId == 5);
	REQUIRE(m->trackingProcessor.getMapLearn() == true);

	m->disableLearn();
	REQUIRE(m->learningId == -1);
	REQUIRE(m->trackingProcessor.getMapLearn() == false);

	Test::destroyModule(m);
}


TEST_CASE("startGlobalLearn advances through cells via processMapLearn", "[MidiBay]") {
	MidiBayModule* m = Test::createModule<MidiBayModule>("MidiBay");

	m->startGlobalLearn();
	REQUIRE(m->midiLearnMode == true);
	REQUIRE(m->learningId == 0);

	// Simulate learning cell 0
	m->processMapLearn(MidiTrackingType::NOTE, 0);
	REQUIRE(m->learningId == 1);

	// Disable and verify cleanup
	m->disableLearn();
	REQUIRE(m->midiLearnMode == false);
	REQUIRE(m->learningId == -1);

	Test::destroyModule(m);
}


TEST_CASE("portAssignment isValid and clear", "[MidiBay]") {
	MidiBayModule* m = Test::createModule<MidiBayModule>("MidiBay");

	REQUIRE(m->portAssignments[0].isValid() == false);

	m->portAssignments[0].moduleId = 10;
	m->portAssignments[0].portId   = 2;
	m->portAssignments[0].type     = engine::Port::INPUT;
	REQUIRE(m->portAssignments[0].isValid() == true);

	m->portAssignments[0].clear();
	REQUIRE(m->portAssignments[0].isValid() == false);

	Test::destroyModule(m);
}
