#include "../../test/framework.hpp"
#include "MidiKey.cpp"
#include "MidiKey.vcvm.test.h"

using namespace StoermelderPackOne;
using namespace StoermelderPackOne::MidiKey;

struct SceneEx : rack::app::Scene {
	std::vector<event::HoverKey> receivedKeys;
	void onHoverKey(const HoverKeyEvent& e) override {
		receivedKeys.push_back(e);
	}
};

SYNC_MODEL(modelMidiKey, "MidiKey");
Test::TestContext<SceneEx> testContext;


TEST_CASE("Construction and initialization", "[MidiKey]") {
	MidiKeyModule<>* m = Test::createModule<MidiKeyModule<>>("MidiKey");
	MidiKeyWidget* mw = Test::createWidget<MidiKeyWidget>(m);

	Test::registerModule(m, mw);
	Test::unregisterModule(m, mw);
	Test::destroyModule(m);
}

TEST_CASE("Preset JSON null-guards", "[MidiKey][JSON]") {
	auto module = Test::createModule<MidiKeyModule<>>("MidiKey");

	SECTION("All top-level properties are null-guarded in dataFromJson()") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetNullGuards(module, rootJ);
		json_decref(rootJ);
	}

	SECTION("All properties tolerate wrong-typed values") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetTypeConfusion(module, rootJ);
		json_decref(rootJ);
	}

	SECTION("All arrays tolerate being oversized") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetOversizedArrays(module, rootJ);
		json_decref(rootJ);
	}

	Test::destroyModule(module);
}

TEST_CASE("Preset loading", "[MidiKey]") {
	MidiKeyModule<>* m = Test::createModule<MidiKeyModule<>>("MidiKey");
	Test::registerModule(m);

	json_error_t jerr;
	json_t* moduleJ = json_loads(MidiKey_vcvm, 0, &jerr);
	m->dataFromJson(moduleJ);

	json_decref(moduleJ);

	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("Legacy preset migrates cc/note into the tracking processor", "[MidiKey][JSON]") {
	// MidiKey.vcvm.test.h is in the pre-trackingProcessor format: the MIDI
	// assignment lives in per-map "cc"/"note" fields instead of a
	// "trackingProcessor" object. dataFromJson() must migrate it.
	MidiKeyModule<>* m = Test::createModule<MidiKeyModule<>>("MidiKey");

	json_error_t jerr;
	json_t* moduleJ = json_loads(MidiKey_vcvm, 0, &jerr);
	REQUIRE(moduleJ != nullptr);
	json_t* dataJ = json_object_get(moduleJ, "data");
	REQUIRE(dataJ != nullptr);
	m->dataFromJson(dataJ);

	// The legacy array is indexed by raw slot storage order, so entries 0..2
	// are the modifier rows and entry 3 onwards are channels 0..15.
	auto modCtrl = m->trackingProcessor.getMap(m->getMapId(ID_CTRL));
	REQUIRE(modCtrl.type == MidiTrackingType::NOTE);
	REQUIRE(modCtrl.param == 76);

	// Channel 0 is legacy entry 3: note 68, key 70 (GLFW_KEY_F).
	auto ch0 = m->trackingProcessor.getMap(m->getMapId(0));
	REQUIRE(ch0.type == MidiTrackingType::NOTE);
	REQUIRE(ch0.param == 68);
	REQUIRE(m->slot[0].key == 70);

	// Entry 10 onwards are unmapped (note -1) and must stay NONE rather than
	// being registered as a map for some wrapped note number.
	auto ch8 = m->trackingProcessor.getMap(m->getMapId(8));
	REQUIRE(ch8.type == MidiTrackingType::NONE);
	REQUIRE(m->slot[8].key == -1);

	json_decref(moduleJ);
	Test::destroyModule(m);
}

TEST_CASE("JSON round-trip preserves state", "[MidiKey][JSON]") {
	MidiKeyModule<>* src = Test::createModule<MidiKeyModule<>>("MidiKey");

	// Build a state that spans both dimensions: a modifier row with a MIDI
	// source, a channel row with MIDI + key + mods, and a bound module id.
	src->trackingProcessor.setMap(MidiTrackingType::NOTE, src->getMapId(ID_SHIFT), 48);
	src->trackingProcessor.setMap(MidiTrackingType::CC, src->getMapId(2), 17);
	src->slot[2].key = GLFW_KEY_B;
	src->slot[2].mods = RACK_MOD_CTRL | GLFW_MOD_ALT;
	src->slot[2].moduleId = 4242;
	src->updateMapLen();

	json_t* rootJ = src->dataToJson();
	REQUIRE(rootJ != nullptr);

	MidiKeyModule<>* dst = Test::createModule<MidiKeyModule<>>("MidiKey");
	dst->dataFromJson(rootJ);

	auto shiftMap = dst->trackingProcessor.getMap(dst->getMapId(ID_SHIFT));
	REQUIRE(shiftMap.type == MidiTrackingType::NOTE);
	REQUIRE(shiftMap.param == 48);

	auto ch2 = dst->trackingProcessor.getMap(dst->getMapId(2));
	REQUIRE(ch2.type == MidiTrackingType::CC);
	REQUIRE(ch2.param == 17);

	REQUIRE(dst->slot[2].key == GLFW_KEY_B);
	REQUIRE(dst->slot[2].mods == (RACK_MOD_CTRL | GLFW_MOD_ALT));
	REQUIRE(dst->slot[2].moduleId == 4242);
	REQUIRE(dst->mapLen == src->mapLen);

	json_decref(rootJ);
	Test::destroyModule(dst);
	Test::destroyModule(src);
}

TEST_CASE("dataFromJson tolerates an oversized maps array", "[MidiKey][JSON]") {
	// A preset written by a build with more channels (or a hand-edited patch)
	// must not write past the end of the slot vector.
	MidiKeyModule<>* m = Test::createModule<MidiKeyModule<>>("MidiKey");

	json_t* rootJ = json_object();
	json_t* mapsJ = json_array();
	// The module has MAX_CHANNELS + 3 == 19 slots; write twice that many.
	for (int i = 0; i < 40; i++) {
		json_t* mapJ = json_object();
		json_object_set_new(mapJ, "key", json_integer(GLFW_KEY_A));
		json_object_set_new(mapJ, "mods", json_integer(0));
		json_object_set_new(mapJ, "moduleId", json_integer(-1));
		json_array_append_new(mapsJ, mapJ);
	}
	json_object_set_new(rootJ, "maps", mapsJ);

	REQUIRE_NOTHROW(m->dataFromJson(rootJ));

	json_decref(rootJ);
	Test::destroyModule(m);
}

TEST_CASE("Legacy dataFromJson rejects out-of-range cc/note numbers", "[MidiKey][JSON]") {
	// cc/note from a corrupt preset index 128-element vectors in the tracking
	// processor, so they must be range-checked before use.
	MidiKeyModule<>* m = Test::createModule<MidiKeyModule<>>("MidiKey");

	json_t* rootJ = json_object();
	json_t* mapsJ = json_array();
	json_t* mapJ = json_object();
	json_object_set_new(mapJ, "key", json_integer(-1));
	json_object_set_new(mapJ, "mods", json_integer(0));
	json_object_set_new(mapJ, "cc", json_integer(9999));
	json_object_set_new(mapJ, "note", json_integer(9999));
	json_object_set_new(mapJ, "moduleId", json_integer(-1));
	json_array_append_new(mapsJ, mapJ);
	json_object_set_new(rootJ, "maps", mapsJ);
	// No "trackingProcessor" key, so the legacy migration branch runs.

	m->dataFromJson(rootJ);

	// Out-of-range values index 128-element vectors in the tracking processor,
	// so they must be dropped rather than registered. (REQUIRE_NOTHROW alone
	// would not catch this: an out-of-bounds write is UB, not an exception.)
	REQUIRE(m->trackingProcessor.getMap(m->getMapId(ID_CTRL)).type == MidiTrackingType::NONE);

	json_decref(rootJ);
	Test::destroyModule(m);
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

TEST_CASE("Map IDs are unique across modifiers and channels", "[MidiKey]") {
	// The existing "Map ID inversion" case checks round-tripping, which cannot
	// detect two distinct ids folding onto the same map id.
	MidiKeyModule<>* m = Test::createModule<MidiKeyModule<>>("MidiKey");

	std::set<uint16_t> seen;
	int ids[] = { ID_CTRL, ID_ALT, ID_SHIFT, 0, 1, 2, 3, 15 };
	for (int id : ids) {
		uint16_t mapId = m->getMapId(id);
		CATCH_INFO("id " << id << " -> mapId " << mapId);
		REQUIRE(seen.count(mapId) == 0);
		seen.insert(mapId);
		// Every map id must be addressable in the tracking processor.
		REQUIRE(mapId < 16 + 3);
	}

	Test::destroyModule(m);
}

TEST_CASE("Slot indexing does not alias distinct ids", "[MidiKey]") {
	MidiKeyModule<>* m = Test::createModule<MidiKeyModule<>>("MidiKey");

	// Writing channel 0 must not be observable through any modifier row, and
	// the highest channel must stay inside the backing vector.
	m->slot[0].key = GLFW_KEY_A;
	m->slot[15].key = GLFW_KEY_Z;
	REQUIRE(m->slot[ID_CTRL].key != GLFW_KEY_A);
	REQUIRE(m->slot[ID_ALT].key != GLFW_KEY_A);
	REQUIRE(m->slot[ID_SHIFT].key != GLFW_KEY_A);
	REQUIRE(m->slot[0].key == GLFW_KEY_A);
	REQUIRE(m->slot[15].key == GLFW_KEY_Z);

	// The last channel must map inside the vector, not one past the end.
	REQUIRE(m->getMapId(15) < m->slot.v.size());

	Test::destroyModule(m);
}

TEST_CASE("disableLearn() without an id disarms the tracking processor", "[MidiKey]") {
	// Reachable from enableLearn(id) when id == mapLen, i.e. clicking the
	// trailing "Mapping..." row while another row is already armed. If the
	// processor stays armed, the next incoming CC/note is swallowed as a learn
	// assignment instead of being dispatched as a key event.
	MidiKeyModule<>* m = Test::createModule<MidiKeyModule<>>("MidiKey");

	m->enableLearn(3);
	REQUIRE(m->learningId == 3);
	REQUIRE(m->trackingProcessor.getMapLearn() == true);

	m->disableLearn();

	REQUIRE(m->learningId == -1);
	REQUIRE(m->trackingProcessor.getMapLearn() == false);

	Test::destroyModule(m);
}

TEST_CASE("learnKey() is a no-op when no learn session is active", "[MidiKey]") {
	MidiKeyModule<>* m = Test::createModule<MidiKeyModule<>>("MidiKey");

	REQUIRE(m->learningId == -1);
	m->slot[0].key = GLFW_KEY_Q;

	// No session: this must not fall through onto some slot.
	m->learnKey(GLFW_KEY_A, 0);

	REQUIRE(m->slot[0].key == GLFW_KEY_Q);

	Test::destroyModule(m);
}

TEST_CASE("learnKey() masks unsupported modifier bits", "[MidiKey]") {
	MidiKeyModule<>* m = Test::createModule<MidiKeyModule<>>("MidiKey");

	m->enableLearn(0);
	m->trackingProcessor.disableMapLearn();
	m->learnKey(GLFW_KEY_A, RACK_MOD_CTRL | GLFW_MOD_SHIFT | GLFW_MOD_SUPER | GLFW_MOD_CAPS_LOCK);

	// Only CTRL/ALT/SHIFT are supported; the rest must not be stored.
	REQUIRE((m->slot[0].mods & ~(RACK_MOD_CTRL | GLFW_MOD_ALT | GLFW_MOD_SHIFT)) == 0);
	REQUIRE((m->slot[0].mods & RACK_MOD_CTRL) != 0);
	REQUIRE((m->slot[0].mods & GLFW_MOD_SHIFT) != 0);

	Test::destroyModule(m);
}

TEST_CASE("onReset clears active state", "[MidiKey]") {
	// A latched modifier ORs itself into every subsequent key event, so reset
	// must clear it. Reachable when a note-off is lost (device unplugged,
	// port switched, patch reloaded mid-hold).
	MidiKeyModule<>* m = Test::createModule<MidiKeyModule<>>("MidiKey");

	m->processMapUpdate(MidiTrackingType::NOTE, m->getMapId(ID_CTRL), 127);
	REQUIRE(m->slot[ID_CTRL].active == true);

	Module::ResetEvent re;
	m->onReset(re);

	REQUIRE(m->slot[ID_CTRL].active == false);

	Test::destroyModule(m);
}

TEST_CASE("onReset clears tracked NRPN/14-bit CC state", "[MidiKey][reset]") {
	// MidiKey itself only maps notes and plain CCs, so this state never reaches
	// its handler -- but it lives in the shared MidiProcessor, and leaving it
	// armed across a reset means the first CC 6 (or a 14-bit LSB) after the
	// reset is decoded against a parameter selected before it.
	MidiKeyModule<>* m = Test::createModule<MidiKeyModule<>>("MidiKey");
	auto& mp = m->trackingProcessor.midiProcessor;

	// Arm an NRPN parameter and store a 14-bit CC MSB on channel 0.
	mp.processCc(Test::makeMidiMessage(0xb, 0, 99, 4));
	mp.processCc(Test::makeMidiMessage(0xb, 0, 98, 5));
	mp.processCc(Test::makeMidiMessage(0xb, 0, 5, 3));
	REQUIRE(mp.ccNrpnParam[0] == (4 * 128 + 5));
	REQUIRE(mp.cc14bitMsb[0][5] == 3);

	Module::ResetEvent re;
	m->onReset(re);

	REQUIRE(mp.ccNrpnParam[0] == -1);
	REQUIRE(mp.cc14bitMsb[0][5] == -1);

	Test::destroyModule(m);
}

TEST_CASE("clearMaps clears active state", "[MidiKey]") {
	MidiKeyModule<>* m = Test::createModule<MidiKeyModule<>>("MidiKey");

	m->processMapUpdate(MidiTrackingType::NOTE, m->getMapId(ID_SHIFT), 127);
	m->slot[0].key = GLFW_KEY_A;
	m->processMapUpdate(MidiTrackingType::NOTE, m->getMapId(0), 127);
	REQUIRE(m->slot[ID_SHIFT].active == true);
	REQUIRE(m->slot[0].active == true);

	m->clearMaps();

	REQUIRE(m->slot[ID_SHIFT].active == false);
	REQUIRE(m->slot[0].active == false);

	Test::destroyModule(m);
}

TEST_CASE("A latched modifier does not leak into later key events", "[MidiKey]") {
	MidiKeyModule<>* m = Test::createModule<MidiKeyModule<>>("MidiKey");

	// Modifier goes down and is then cleared by the user (unmap), simulating
	// the note-off never arriving.
	m->processMapUpdate(MidiTrackingType::NOTE, m->getMapId(ID_CTRL), 127);
	m->clearMap(ID_CTRL);

	m->slot[1].key = GLFW_KEY_A;
	m->processMapUpdate(MidiTrackingType::NOTE, m->getMapId(1), 127);

	REQUIRE(m->keyEventQueue.size() == 1);
	auto e = std::get<0>(m->keyEventQueue.shift());
	REQUIRE((e.mods & RACK_MOD_CTRL) == 0);

	Test::destroyModule(m);
}

TEST_CASE("Modifier slots combine into emitted key events", "[MidiKey]") {
	MidiKeyModule<>* m = Test::createModule<MidiKeyModule<>>("MidiKey");
	m->slot[1].key = GLFW_KEY_A;

	SECTION("Live modifier rows are ORed in") {
		m->processMapUpdate(MidiTrackingType::NOTE, m->getMapId(ID_CTRL), 127);
		m->processMapUpdate(MidiTrackingType::NOTE, m->getMapId(ID_SHIFT), 127);
		m->processMapUpdate(MidiTrackingType::NOTE, m->getMapId(1), 127);

		REQUIRE(m->keyEventQueue.size() == 1);
		auto e = std::get<0>(m->keyEventQueue.shift());
		REQUIRE((e.mods & RACK_MOD_CTRL) != 0);
		REQUIRE((e.mods & GLFW_MOD_SHIFT) != 0);
		REQUIRE((e.mods & GLFW_MOD_ALT) == 0);
	}

	SECTION("Per-slot mods are ORed in without a live modifier row") {
		m->slot[1].mods = GLFW_MOD_ALT;
		m->processMapUpdate(MidiTrackingType::NOTE, m->getMapId(1), 127);

		REQUIRE(m->keyEventQueue.size() == 1);
		auto e = std::get<0>(m->keyEventQueue.shift());
		REQUIRE((e.mods & GLFW_MOD_ALT) != 0);
		REQUIRE((e.mods & RACK_MOD_CTRL) == 0);
	}

	Test::destroyModule(m);
}

TEST_CASE("Note-off emits a release and repeats are filtered", "[MidiKey]") {
	MidiKeyModule<>* m = Test::createModule<MidiKeyModule<>>("MidiKey");
	m->slot[1].key = GLFW_KEY_A;

	// Press
	m->processMapUpdate(MidiTrackingType::NOTE, m->getMapId(1), 127);
	REQUIRE(m->keyEventQueue.size() == 1);
	REQUIRE(std::get<0>(m->keyEventQueue.shift()).action == GLFW_PRESS);

	// A second note-on while held must not emit a duplicate press.
	m->processMapUpdate(MidiTrackingType::NOTE, m->getMapId(1), 127);
	REQUIRE(m->keyEventQueue.size() == 0);

	// Release
	m->processMapUpdate(MidiTrackingType::NOTE, m->getMapId(1), 0);
	REQUIRE(m->keyEventQueue.size() == 1);
	REQUIRE(std::get<0>(m->keyEventQueue.shift()).action == GLFW_RELEASE);

	// A second note-off must not emit a duplicate release.
	m->processMapUpdate(MidiTrackingType::NOTE, m->getMapId(1), 0);
	REQUIRE(m->keyEventQueue.size() == 0);

	Test::destroyModule(m);
}

TEST_CASE("Unmapped slots emit nothing", "[MidiKey]") {
	MidiKeyModule<>* m = Test::createModule<MidiKeyModule<>>("MidiKey");

	// Slot has a MIDI source but no key assigned yet.
	REQUIRE(m->slot[4].key == -1);
	m->processMapUpdate(MidiTrackingType::NOTE, m->getMapId(4), 127);

	REQUIRE(m->keyEventQueue.size() == 0);

	Test::destroyModule(m);
}

TEST_CASE("updateMapLen tracks the last non-empty slot", "[MidiKey]") {
	MidiKeyModule<>* m = Test::createModule<MidiKeyModule<>>("MidiKey");

	// Fresh module: one empty "Mapping..." row.
	REQUIRE(m->mapLen == 1);

	SECTION("A key-only slot counts as occupied") {
		m->slot[4].key = GLFW_KEY_A;
		m->updateMapLen();
		REQUIRE(m->mapLen == 6); // slots 0..4 plus the trailing empty row
	}

	SECTION("A MIDI-only slot counts as occupied") {
		m->trackingProcessor.setMap(MidiTrackingType::CC, m->getMapId(2), 10);
		m->updateMapLen();
		REQUIRE(m->mapLen == 4);
	}

	SECTION("mapLen is capped at MAX_CHANNELS") {
		m->slot[15].key = GLFW_KEY_A;
		m->updateMapLen();
		REQUIRE(m->mapLen == 16);
	}

	Test::destroyModule(m);
}

TEST_CASE("clearMap(midiOnly) keeps the key binding", "[MidiKey]") {
	MidiKeyModule<>* m = Test::createModule<MidiKeyModule<>>("MidiKey");

	m->trackingProcessor.setMap(MidiTrackingType::CC, m->getMapId(0), 11);
	m->slot[0].key = GLFW_KEY_A;
	m->slot[0].mods = RACK_MOD_CTRL;

	m->clearMap(0, true);

	REQUIRE(m->trackingProcessor.getMap(m->getMapId(0)).type == MidiTrackingType::NONE);
	REQUIRE(m->slot[0].key == GLFW_KEY_A);
	REQUIRE(m->slot[0].mods == RACK_MOD_CTRL);

	SECTION("A full clear drops both") {
		m->clearMap(0);
		REQUIRE(m->slot[0].key == -1);
		REQUIRE(m->slot[0].mods == 0);
	}

	Test::destroyModule(m);
}

TEST_CASE("Learn assigns the incoming MIDI source to the armed slot", "[MidiKey]") {
	MidiKeyModule<>* m = Test::createModule<MidiKeyModule<>>("MidiKey");

	SECTION("Note") {
		m->enableLearn(0);
		m->trackingProcessor.getInput().onMessage(Test::makeMidiMessage(0x9, 0, 64, 100));
		m->process(Test::makeProcessArgs(1));

		auto map = m->trackingProcessor.getMap(m->getMapId(0));
		REQUIRE(map.type == MidiTrackingType::NOTE);
		REQUIRE(map.param == 64);
		// MIDI arrived but no key yet, so the session stays open.
		REQUIRE(m->learningId == 0);
	}

	SECTION("CC") {
		m->enableLearn(0);
		m->trackingProcessor.getInput().onMessage(Test::makeMidiMessage(0xb, 0, 21, 100));
		m->process(Test::makeProcessArgs(1));

		auto map = m->trackingProcessor.getMap(m->getMapId(0));
		REQUIRE(map.type == MidiTrackingType::CC);
		REQUIRE(map.param == 21);
	}

	SECTION("Modifier rows commit on MIDI alone") {
		m->enableLearn(ID_CTRL);
		m->trackingProcessor.getInput().onMessage(Test::makeMidiMessage(0x9, 0, 36, 100));
		m->process(Test::makeProcessArgs(1));

		auto map = m->trackingProcessor.getMap(m->getMapId(ID_CTRL));
		REQUIRE(map.type == MidiTrackingType::NOTE);
		REQUIRE(map.param == 36);
		// Modifier rows need no key, so the session closes.
		REQUIRE(m->learningId == -1);
	}

	Test::destroyModule(m);
}

TEST_CASE("Relearning a slot releases the previous MIDI source", "[MidiKey]") {
	MidiKeyModule<>* m = Test::createModule<MidiKeyModule<>>("MidiKey");

	m->enableLearn(0);
	m->trackingProcessor.getInput().onMessage(Test::makeMidiMessage(0x9, 0, 60, 100));
	m->process(Test::makeProcessArgs(1));
	m->learnKey(GLFW_KEY_A, 0);

	// Relearn the same slot to a different note.
	m->enableLearn(0);
	m->trackingProcessor.getInput().onMessage(Test::makeMidiMessage(0x9, 0, 62, 100));
	m->process(Test::makeProcessArgs(2));

	REQUIRE(m->trackingProcessor.getMap(m->getMapId(0)).param == 62);

	// The old note must no longer drive this slot.
	m->slot[0].active = false;
	m->processMapUpdate(MidiTrackingType::NOTE, m->getMapId(0), 0);
	m->keyEventQueue.clear();

	m->trackingProcessor.getInput().onMessage(Test::makeMidiMessage(0x9, 0, 60, 100));
	m->process(Test::makeProcessArgs(3));
	REQUIRE(m->keyEventQueue.size() == 0);

	Test::destroyModule(m);
}

TEST_CASE("processBypass drains queued MIDI without emitting key events", "[MidiKey]") {
	MidiKeyModule<>* m = Test::createModule<MidiKeyModule<>>("MidiKey");

	// Learn note 60 -> slot 0, then bind it to a key, so a normal process()
	// of note 60 would enqueue a key event.
	m->enableLearn(0);
	m->trackingProcessor.getInput().onMessage(Test::makeMidiMessage(0x9, 0, 60, 100));
	m->process(Test::makeProcessArgs(1));
	m->learnKey(GLFW_KEY_A, 0);
	REQUIRE(m->slot[0].key == GLFW_KEY_A);
	REQUIRE(m->keyEventQueue.size() == 0);

	// Queue another note-on for the now-mapped note.
	m->trackingProcessor.getInput().onMessage(Test::makeMidiMessage(0x9, 0, 60, 100));
	REQUIRE(m->trackingProcessor.getInput().size() == 1);

	m->processBypass(Test::makeProcessArgs(2));

	REQUIRE(m->trackingProcessor.getInput().size() == 0);
	REQUIRE(m->keyEventQueue.size() == 0);

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