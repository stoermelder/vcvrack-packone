// SpliceKit.learn.test.cpp — learn modes and overlay messages.
// Tests enable/disable of MIDI and port learn, sequential vs. single-cell
// learn advancement, and the setOverlayMessage helper.

#include "SpliceKit.test.hpp"


TEST_CASE("overlayEnabled gates setOverlayMessage", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();

	m->overlayEnabled = false;
	m->overlayMessageId = -1;
	m->setOverlayMessage("Title", "Sub");
	REQUIRE(m->overlayMessageId == -1);  // not triggered

	m->overlayEnabled = true;
	m->setOverlayMessage("Title", "Sub");
	REQUIRE(m->overlayMessageId == 0);  // triggered
}


TEST_CASE("enableLearn and disableLearn", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();

	REQUIRE(m->learningId == -1);

	m->enableLearn(5);
	REQUIRE(m->learningId == 5);
	REQUIRE(m->trackingProcessor.getMapLearn() == true);

	m->disableLearn();
	REQUIRE(m->learningId == -1);
	REQUIRE(m->trackingProcessor.getMapLearn() == false);
}


TEST_CASE("startGlobalLearn advances through cells via processMapLearn", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();

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
}


// processMapLearn — completion paths beyond sequential.
//
// processMapLearn() is only ever invoked by MidiTrackingProcessor::process() itself, which
// calls disableMapLearn() unconditionally right before invoking the handler (see
// MidiTrackingProcessor.hpp processNoteOn/processCc). So in production getMapLearn() is
// already false by the time this method runs — driving it directly, as these tests used to,
// let a stale enableMapLearn() call linger and made it look like production leaves learn
// half-torn-down. It doesn't; see "MIDI end-to-end - learn stores the received note as a map"
// and "MIDI end-to-end - sequential learn ends after the last cell" in
// SpliceKit.test.midi.hpp, which drive real MIDI through process() and assert
// getMapLearn() == false afterwards.

TEST_CASE("processMapLearn - sequential learn ends after the last cell", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();

	m->midiLearnMode = true;
	m->learningId = MATRIX_COUNT - 1;  // last cell

	m->processMapLearn(MidiTrackingType::NOTE, MATRIX_COUNT - 1);
	// nextId = MATRIX_COUNT — out of range, so learningId stays -1 and
	// midiLearnMode is reset to false.
	REQUIRE(m->learningId == -1);
	REQUIRE(m->midiLearnMode == false);
}

// MidiTrackingProcessor — clearMap on a cell with no prior mapping is a no-op

TEST_CASE("trackingProcessor.clearMap - on an unmapped cell is a safe no-op", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();

	REQUIRE_NOTHROW(m->trackingProcessor.clearMap(20));
	auto m0 = m->trackingProcessor.getMap(20);
	REQUIRE(m0.type == MidiTrackingType::NONE);
	REQUIRE(m0.param == 0);
}


// setOverlayMessage - empty/garbage title is still queued (callers are trusted)

TEST_CASE("setOverlayMessage - both subtitles can be empty", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	m->overlayEnabled = true;
	m->overlayMessageId = -1;

	m->setOverlayMessage("Note", "", "");
	REQUIRE(m->overlayMessageId == 0);
	REQUIRE(m->overlayMessage.title == "Note");
	REQUIRE(m->overlayMessage.subtitle[0].empty());
	REQUIRE(m->overlayMessage.subtitle[1].empty());
}


// port learn — range guard and mode interaction

TEST_CASE("enablePortLearn - out-of-range ids are ignored", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	SpliceKitWidget* w = Test::createWidget<SpliceKitWidget>("SpliceKit");

	m->enablePortLearn(-1, w);
	REQUIRE(m->portLearningId == -1);
	m->enablePortLearn(MATRIX_COUNT, w);
	REQUIRE(m->portLearningId == -1);

	Test::destroyWidget(w);
}

TEST_CASE("enablePortLearn - sets the learning cell and cancels an active MIDI learn", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	SpliceKitWidget* w = Test::createWidget<SpliceKitWidget>("SpliceKit");

	// MIDI learn and port learn are mutually exclusive — starting one cancels the other.
	m->enableLearn(5);
	REQUIRE(m->learningId == 5);

	m->enablePortLearn(7, w);
	REQUIRE(m->portLearningId == 7);
	REQUIRE(m->learningId == -1);
	REQUIRE(m->trackingProcessor.getMapLearn() == false);

	Test::destroyWidget(w);
}

TEST_CASE("disablePortLearn - clears the learning cell and sequential mode", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();

	m->portLearningId = 9;
	m->portLearnMode = true;

	m->disablePortLearn();

	REQUIRE(m->portLearningId == -1);
	REQUIRE(m->portLearnMode == false);
	REQUIRE(m->isPortLearning(9) == false);
}

TEST_CASE("enableLearn - starting MIDI learn cancels an active port learn", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	SpliceKitWidget* w = Test::createWidget<SpliceKitWidget>("SpliceKit");

	m->enablePortLearn(3, w);
	REQUIRE(m->portLearningId == 3);

	m->enableLearn(6);
	REQUIRE(m->learningId == 6);
	REQUIRE(m->portLearningId == -1);
	REQUIRE(m->portLearnMode == false);

	Test::destroyWidget(w);
}


// startGlobalPortLearn — sequential port-assignment learn. The advancing logic lives entirely
// in the callback startLearn() installs on portSelectProcessor, which real port-widget clicks
// invoke via processDeselect(); no widget tree is needed to exercise it directly, since the
// callback is a plain std::function reachable through the module's own portSelectProcessor.

TEST_CASE("startGlobalPortLearn - each click assigns the current cell and advances to the next", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	SpliceKitWidget* w = Test::createWidget<SpliceKitWidget>("SpliceKit");
	ModuleScaffold peerMods;
	SpliceKitModule* peer = peerMods.create();  // stands in for "some other module" owning ports

	m->lastClickedCell = 4;
	m->startGlobalPortLearn(w);
	REQUIRE(m->portLearnMode == true);
	REQUIRE(m->portLearningId == 4);

	rack::app::PortWidget pw1;
	pw1.module = peer;
	pw1.portId = 2;
	pw1.type = engine::Port::OUTPUT;
	m->portSelectProcessor.learnCallback(&pw1, Vec());

	// Cell 4 got the port; learn is still active and has advanced to cell 5.
	REQUIRE(m->portAssignments[4].moduleId == peer->getId());
	REQUIRE(m->portAssignments[4].portId == 2);
	REQUIRE(m->portAssignments[4].type == engine::Port::OUTPUT);
	REQUIRE(m->portLearnMode == true);
	REQUIRE(m->portLearningId == 5);
	REQUIRE(m->portSelectProcessor.isLearning());

	rack::app::PortWidget pw2;
	pw2.module = peer;
	pw2.portId = 3;
	pw2.type = engine::Port::INPUT;
	m->portSelectProcessor.learnCallback(&pw2, Vec());

	REQUIRE(m->portAssignments[5].moduleId == peer->getId());
	REQUIRE(m->portAssignments[5].portId == 3);
	REQUIRE(m->portLearningId == 6);
	REQUIRE(m->portSelectProcessor.isLearning());

	// Detach before the ad-hoc PortWidgets go out of scope: their destructor tears down any
	// cables it thinks it owns via APP->scene->rack, which is unrelated to this test.
	pw1.module = nullptr;
	pw2.module = nullptr;
	m->disablePortLearn();
	Test::destroyWidget(w);
}

TEST_CASE("startGlobalPortLearn - assigning the last cell ends sequential learn", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();
	SpliceKitWidget* w = Test::createWidget<SpliceKitWidget>("SpliceKit");
	ModuleScaffold peerMods;
	SpliceKitModule* peer = peerMods.create();

	m->lastClickedCell = MATRIX_COUNT - 1;
	m->startGlobalPortLearn(w);
	REQUIRE(m->portLearningId == MATRIX_COUNT - 1);

	rack::app::PortWidget pw;
	pw.module = peer;
	pw.portId = 0;
	pw.type = engine::Port::OUTPUT;
	m->portSelectProcessor.learnCallback(&pw, Vec());

	// nextId = MATRIX_COUNT — out of range, so sequential learn stops rather than wrapping,
	// and portSelectProcessor itself is torn down (unlike the mid-sequence case above).
	REQUIRE(m->portAssignments[MATRIX_COUNT - 1].moduleId == peer->getId());
	REQUIRE(m->portLearnMode == false);
	REQUIRE(m->portLearningId == -1);
	REQUIRE(m->portSelectProcessor.isLearning() == false);

	pw.module = nullptr;
	Test::destroyWidget(w);
}

TEST_CASE("enableLearn - out-of-range ids are ignored", "[SpliceKit]") {
	ModuleScaffold mods;
	SpliceKitModule* m = mods.create();

	m->enableLearn(-1);
	REQUIRE(m->learningId == -1);
	m->enableLearn(TOTAL_MAPS);
	REQUIRE(m->learningId == -1);

	// The last valid id is a scene button, not a cell.
	m->enableLearn(TOTAL_MAPS - 1);
	REQUIRE(m->learningId == TOTAL_MAPS - 1);
}
