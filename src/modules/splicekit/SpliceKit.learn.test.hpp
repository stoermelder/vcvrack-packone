// SpliceKit.learn.test.cpp — learn modes and overlay messages.
// Tests enable/disable of MIDI and port learn, sequential vs. single-cell
// learn advancement, and the setOverlayMessage helper.

#include "SpliceKit.test.hpp"


TEST_CASE("overlayEnabled gates setOverlayMessage", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	m->overlayEnabled = false;
	m->overlayMessageId = -1;
	m->setOverlayMessage("Title", "Sub");
	REQUIRE(m->overlayMessageId == -1);  // not triggered

	m->overlayEnabled = true;
	m->setOverlayMessage("Title", "Sub");
	REQUIRE(m->overlayMessageId == 0);  // triggered

	Test::destroyModule(m);
}


TEST_CASE("enableLearn and disableLearn", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	REQUIRE(m->learningId == -1);

	m->enableLearn(5);
	REQUIRE(m->learningId == 5);
	REQUIRE(m->trackingProcessor.getMapLearn() == true);

	m->disableLearn();
	REQUIRE(m->learningId == -1);
	REQUIRE(m->trackingProcessor.getMapLearn() == false);

	Test::destroyModule(m);
}


TEST_CASE("startGlobalLearn advances through cells via processMapLearn", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

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


// processMapLearn — completion paths beyond sequential

TEST_CASE("processMapLearn - single-learn mode clears learningId but leaves learnActive alone", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	m->midiLearnMode = false;       // single-learn mode
	m->learningId = 7;
	m->trackingProcessor.enableMapLearn(7);
	REQUIRE(m->trackingProcessor.getMapLearn() == true);

	m->processMapLearn(MidiTrackingType::NOTE, 7);
	// processMapLearn only resets learningId; the caller is expected to follow up
	// with disableLearn() (which clears learnActive) for single-learn. The mismatch
	// is a property of the production code that this test pins down.
	REQUIRE(m->learningId == -1);
	REQUIRE(m->midiLearnMode == false);
	REQUIRE(m->trackingProcessor.getMapLearn() == true);

	// Cleanup: the test must leave learnActive false so destruction is clean.
	m->disableLearn();
	REQUIRE(m->trackingProcessor.getMapLearn() == false);

	Test::destroyModule(m);
}

TEST_CASE("processMapLearn - sequential learn ends after the last cell", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	m->midiLearnMode = true;
	m->learningId = MATRIX_COUNT - 1;  // last cell
	m->trackingProcessor.enableMapLearn(MATRIX_COUNT - 1);

	m->processMapLearn(MidiTrackingType::NOTE, MATRIX_COUNT - 1);
	// nextId = MATRIX_COUNT — out of range, so learningId stays -1 and
	// midiLearnMode is reset to false.
	REQUIRE(m->learningId == -1);
	REQUIRE(m->midiLearnMode == false);
	// learnActive is NOT cleared by processMapLearn (same as single-learn path) —
	// the caller has to follow up with disableLearn() to fully tear down.
	REQUIRE(m->trackingProcessor.getMapLearn() == true);

	m->disableLearn();
	Test::destroyModule(m);
}

TEST_CASE("processMapLearn - single learn does not advance to the next cell", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	m->midiLearnMode = false;
	m->learningId = 42;
	m->trackingProcessor.enableMapLearn(42);

	m->processMapLearn(MidiTrackingType::CC, 42);
	REQUIRE(m->learningId == -1);  // single learn: cleared, not advanced
	// The very next learn invocation must NOT assume sequential state.
	REQUIRE(m->midiLearnMode == false);

	m->disableLearn();
	Test::destroyModule(m);
}


// MidiTrackingProcessor — clearMap on a cell with no prior mapping is a no-op

TEST_CASE("trackingProcessor.clearMap - on an unmapped cell is a safe no-op", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	REQUIRE_NOTHROW(m->trackingProcessor.clearMap(20));
	auto m0 = m->trackingProcessor.getMap(20);
	REQUIRE(m0.type == MidiTrackingType::NONE);
	REQUIRE(m0.param == 0);

	Test::destroyModule(m);
}


// setOverlayMessage - empty/garbage title is still queued (callers are trusted)

TEST_CASE("setOverlayMessage - both subtitles can be empty", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->overlayEnabled = true;
	m->overlayMessageId = -1;

	m->setOverlayMessage("Note", "", "");
	REQUIRE(m->overlayMessageId == 0);
	REQUIRE(m->overlayMessage.title == "Note");
	REQUIRE(m->overlayMessage.subtitle[0].empty());
	REQUIRE(m->overlayMessage.subtitle[1].empty());

	Test::destroyModule(m);
}


// port learn — range guard and mode interaction

TEST_CASE("enablePortLearn - out-of-range ids are ignored", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	SpliceKitWidget* w = Test::createWidget<SpliceKitWidget>("SpliceKit");

	m->enablePortLearn(-1, w);
	REQUIRE(m->portLearningId == -1);
	m->enablePortLearn(MATRIX_COUNT, w);
	REQUIRE(m->portLearningId == -1);

	Test::destroyWidget(w);
	Test::destroyModule(m);
}

TEST_CASE("enablePortLearn - sets the learning cell and cancels an active MIDI learn", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	SpliceKitWidget* w = Test::createWidget<SpliceKitWidget>("SpliceKit");

	// MIDI learn and port learn are mutually exclusive — starting one cancels the other.
	m->enableLearn(5);
	REQUIRE(m->learningId == 5);

	m->enablePortLearn(7, w);
	REQUIRE(m->portLearningId == 7);
	REQUIRE(m->learningId == -1);
	REQUIRE(m->trackingProcessor.getMapLearn() == false);

	Test::destroyWidget(w);
	Test::destroyModule(m);
}

TEST_CASE("disablePortLearn - clears the learning cell and sequential mode", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	m->portLearningId = 9;
	m->portLearnMode = true;

	m->disablePortLearn();

	REQUIRE(m->portLearningId == -1);
	REQUIRE(m->portLearnMode == false);
	REQUIRE(m->isPortLearning(9) == false);

	Test::destroyModule(m);
}

TEST_CASE("enableLearn - starting MIDI learn cancels an active port learn", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	SpliceKitWidget* w = Test::createWidget<SpliceKitWidget>("SpliceKit");

	m->enablePortLearn(3, w);
	REQUIRE(m->portLearningId == 3);

	m->enableLearn(6);
	REQUIRE(m->learningId == 6);
	REQUIRE(m->portLearningId == -1);
	REQUIRE(m->portLearnMode == false);

	Test::destroyWidget(w);
	Test::destroyModule(m);
}

TEST_CASE("enableLearn - out-of-range ids are ignored", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	m->enableLearn(-1);
	REQUIRE(m->learningId == -1);
	m->enableLearn(TOTAL_MAPS);
	REQUIRE(m->learningId == -1);

	// The last valid id is a scene button, not a cell.
	m->enableLearn(TOTAL_MAPS - 1);
	REQUIRE(m->learningId == TOTAL_MAPS - 1);

	Test::destroyModule(m);
}
