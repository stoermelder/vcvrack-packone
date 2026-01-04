#include "catch2/plugin.hpp"
#include "test_context.hpp"
#include "../modules/midi/MidiKey.cpp"
#include "MidiKey.vcvm.h"

using namespace StoermelderPackOne;
using namespace StoermelderPackOne::MidiKey;

struct SceneEx : rack::app::Scene {
	std::vector<event::HoverKey> receivedKeys;
	void onHoverKey(const HoverKeyEvent& e) override {
		receivedKeys.push_back(e);
	}
};

// Define the single instance used by tests
static Test::TestContext<SceneEx> testContext;



TEST_CASE("Construction and initialization", "[MidiKey]") {
	MidiKeyModule<>* m = Test::createModule<MidiKeyModule<>>("MidiKey");
	MidiKeyWidget* mw = Test::createWidget<MidiKeyWidget>(m);

	Test::registerModule(m, mw);
	Test::unregisterModule(m, mw);
}

TEST_CASE("Preset loading", "[MidiKey]") {
	MidiKeyModule<>* m = Test::createModule<MidiKeyModule<>>("MidiKey");
	Test::registerModule(m);

	json_error_t jerr;
	json_t* moduleJ = json_loads(MidiKey_vcvm, 0, &jerr);
	m->dataFromJson(moduleJ);

	json_decref(moduleJ);

	Test::unregisterModule(m);
}

TEST_CASE("Map ID inversion", "[MidiKey]") {
	MidiKeyModule<>* m = Test::createModule<MidiKeyModule<>>("MidiKey");

	// Check negative modifier IDs
	int negIds[] = { ID_CTRL, ID_ALT, ID_SHIFT };
	uint16_t negMapIds[3];
	for (int i = 0; i < 3; ++i) {
		negMapIds[i] = m->getMapId(negIds[i]);
	}
	// Check channel IDs 0..8
	const int chanCount = 9;
	uint16_t chanMapIds[chanCount];
	for (int i = 0; i < chanCount; ++i) {
		chanMapIds[i] = m->getMapId(i);
	}
	// Assertions after collection
	for (int i = 0; i < 3; ++i) {
		REQUIRE(m->getMapIdRev(negMapIds[i]) == negIds[i]);
	}
	for (int i = 0; i < chanCount; ++i) {
		REQUIRE(m->getMapIdRev(chanMapIds[i]) == i);
	}

	Test::destroyModule(m);
}

TEST_CASE("Enable/disable learn and learnKey behavior", "[MidiKey]") {
	MidiKeyModule<>* m = Test::createModule<MidiKeyModule<>>("MidiKey");
	// Enable learn, then disable the tracking processor learn state and call learnKey
	m->enableLearn(0);
	REQUIRE(m->learningId == 0);
	// Simulate that the tracking processor has finished learning MIDI (so commitLearn can proceed)
	m->trackingProcessor.disableMapLearn();
	// Use a known key
	m->learnKey(GLFW_KEY_A, 0);
	// The slot must have been updated
	REQUIRE(m->slot[0].key == GLFW_KEY_A);
	// learningId should have been cleared by commitLearn
	REQUIRE(m->learningId == -1);
	// mapLen should have increased at least to cover slot 0
	REQUIRE(m->mapLen >= 1);
	Test::destroyModule(m);
}

TEST_CASE("ProcessMapUpdate toggles modifier slots and emits key events", "[MidiKey]") {
	MidiKeyModule<>* m = Test::createModule<MidiKeyModule<>>("MidiKey");
	// Test modifier toggle: CTRL (-4 -> mapId 0)
	m->processMapUpdate(MidiTrackingType::NOTE, m->getMapId(ID_CTRL), 1);
	REQUIRE(m->slot[ID_CTRL].active == true);
	m->processMapUpdate(MidiTrackingType::NOTE, m->getMapId(ID_CTRL), 0);
	REQUIRE(m->slot[ID_CTRL].active == false);

	// Test generating a key event for a mapped slot
	int testId = 1;
	m->slot[testId].key = GLFW_KEY_A;
	// Ensure queue empty initially
	REQUIRE(m->keyEventQueue.size() == 0);
	// Trigger a note update -> should enqueue a press event
	m->processMapUpdate(MidiTrackingType::NOTE, m->getMapId(testId), 100);
	REQUIRE(m->keyEventQueue.size() == 1);

	SECTION("Key event contents") {
		// Event should be enqueued correctly
		auto t = m->keyEventQueue.shift();
		auto e = std::get<0>(t);
		auto moduleId = std::get<1>(t);
		REQUIRE(e.key == GLFW_KEY_A);
		REQUIRE(e.action == GLFW_PRESS);
		REQUIRE(moduleId == -1);
	}

	SECTION("Key event window propagate") {
		MidiKeyWidget* mw = Test::createWidget<MidiKeyWidget>(m);
		Test::registerModule(m, mw);

		// Process the press event
		mw->step();

		REQUIRE(testContext.scene->receivedKeys.size() == 1);
		REQUIRE(testContext.scene->receivedKeys[0].key == GLFW_KEY_A);
		REQUIRE(testContext.scene->receivedKeys[0].action == GLFW_PRESS);
		// Clean up
		testContext.scene->receivedKeys.clear();
		Test::unregisterModule(m, mw);
	}

	Test::destroyModule(m);
}