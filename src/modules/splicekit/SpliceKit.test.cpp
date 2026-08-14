// SpliceKit.test.cpp — core module behavior.
// Covers construction and initialization, the port-assignment bitmask store,
// cable direction resolution, and pending-cell/trigger handling including
// MIDI note-pending and note-off resolution.

#include "SpliceKit.test.hpp"

#include "SpliceKit.crossinstance.test.hpp"
#include "SpliceKit.feedback.test.hpp"
#include "SpliceKit.json.test.hpp"
#include "SpliceKit.learn.test.hpp"
#include "SpliceKit.mapping.test.hpp"
#include "SpliceKit.process.test.hpp"
#include "SpliceKit.randomize.test.hpp"
#include "SpliceKit.scenes.test.hpp"


TEST_CASE("Construction and initialization", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	SpliceKitWidget* mw = Test::createWidget<SpliceKitWidget>("SpliceKit");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);
	REQUIRE(m->sceneStore.current == 0);
	REQUIRE(m->pendingCellId == -1);
	REQUIRE(m->buttonMode == SpliceKitModule::BUTTON_TOGGLE);
	REQUIRE(m->overlayEnabled == true);
	REQUIRE(m->sceneStore.connections.size() == (size_t)SCENE_COUNT);
	REQUIRE(m->sceneStore.connections.capacity() >= (size_t)SCENE_COUNT);
	REQUIRE(m->feedback.sceneLedState.size() == (size_t)SCENE_COUNT);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}


TEST_CASE("onSampleRateChange - sets lightDivider relative to sample rate, independent of processDivider", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	Module::SampleRateChangeEvent e;
	e.sampleRate = 48000.f;
	e.sampleTime = 1.f / e.sampleRate;
	m->onSampleRateChange(e);
	REQUIRE(m->lightDivider.getDivision() == (uint32_t)(48000.f / 100.f));
	REQUIRE(m->processDivider.getDivision() == 256);  // unaffected by sample rate

	e.sampleRate = 96000.f;
	e.sampleTime = 1.f / e.sampleRate;
	m->onSampleRateChange(e);
	REQUIRE(m->lightDivider.getDivision() == (uint32_t)(96000.f / 100.f));

	Test::destroyModule(m);
}


TEST_CASE("isConnected and setConnection bitmask", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	REQUIRE(m->sceneStore.isConnected(0, 0, 1) == false);

	m->sceneStore.setConnection(0, 0, 1, true);
	REQUIRE(m->sceneStore.isConnected(0, 0, 1) == true);
	REQUIRE(m->sceneStore.isConnected(0, 1, 0) == true);  // symmetric

	m->sceneStore.setConnection(0, 0, 1, false);
	REQUIRE(m->sceneStore.isConnected(0, 0, 1) == false);
	REQUIRE(m->sceneStore.isConnected(0, 1, 0) == false);

	// Different scene is unaffected
	REQUIRE(m->sceneStore.isConnected(1, 0, 1) == false);

	Test::destroyModule(m);
}


// ─── resolveDirection ─────────────────────────────────────────────────────────────────────
// Pure, no module needed — the call sites (removeCableBetween/addCableBetween, which
// every live connection change funnels through, toggleConnection, and the cross-instance
// path) all delegate to this.

TEST_CASE("resolveDirection - output/input resolves output first regardless of argument order", "[SpliceKit]") {
	PortAssignment out;
	out.moduleId = 1;
	out.portId = 0;
	out.type = engine::Port::OUTPUT;
	PortAssignment in;
	in.moduleId = 2;
	in.portId = 0;
	in.type = engine::Port::INPUT;

	auto dir1 = resolveDirection(out, in);
	REQUIRE(dir1.first == &out);
	REQUIRE(dir1.second == &in);

	auto dir2 = resolveDirection(in, out);
	REQUIRE(dir2.first == &out);
	REQUIRE(dir2.second == &in);
}

TEST_CASE("resolveDirection - same-direction pairs return nullptr", "[SpliceKit]") {
	PortAssignment a; a.moduleId = 1; a.portId = 0; a.type = engine::Port::OUTPUT;
	PortAssignment b; b.moduleId = 2; b.portId = 0; b.type = engine::Port::OUTPUT;
	auto dir = resolveDirection(a, b);
	REQUIRE(dir.first == nullptr);
	REQUIRE(dir.second == nullptr);

	PortAssignment c; c.moduleId = 3; c.portId = 0; c.type = engine::Port::INPUT;
	PortAssignment d; d.moduleId = 4; d.portId = 0; d.type = engine::Port::INPUT;
	auto dir2 = resolveDirection(c, d);
	REQUIRE(dir2.first == nullptr);
	REQUIRE(dir2.second == nullptr);
}

TEST_CASE("resolveDirection - either assignment invalid returns nullptr", "[SpliceKit]") {
	PortAssignment valid; valid.moduleId = 1; valid.portId = 0; valid.type = engine::Port::OUTPUT;
	PortAssignment invalid;  // default-constructed, not valid

	auto dir1 = resolveDirection(valid, invalid);
	REQUIRE(dir1.first == nullptr);

	auto dir2 = resolveDirection(invalid, valid);
	REQUIRE(dir2.first == nullptr);
}


TEST_CASE("clearPending resets pendingCellId and pendingCellIsPhysical", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	m->pendingCellId = 5;
	m->pendingCellIsPhysical = true;
	m->clearPending();

	REQUIRE(m->pendingCellId == -1);
	REQUIRE(m->pendingCellIsPhysical == false);

	Test::destroyModule(m);
}


TEST_CASE("triggerCell - first press sets pending", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	// Assign cell 3 a port so triggerCell doesn't bail early
	m->portAssignments[3].moduleId = 42;
	m->portAssignments[3].portId = 0;
	m->portAssignments[3].type = engine::Port::OUTPUT;

	REQUIRE(m->pendingCellId == -1);
	m->triggerCell(3);
	REQUIRE(m->pendingCellId == 3);

	Test::destroyModule(m);
}


TEST_CASE("triggerCell - pressing same cell cancels pending", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	m->portAssignments[2].moduleId = 42;
	m->portAssignments[2].portId = 0;
	m->portAssignments[2].type = engine::Port::OUTPUT;

	m->triggerCell(2);
	REQUIRE(m->pendingCellId == 2);

	m->triggerCell(2);
	REQUIRE(m->pendingCellId == -1);

	Test::destroyModule(m);
}


TEST_CASE("triggerCell - unassigned cell is ignored", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	// Cell 10 has no port assignment
	m->triggerCell(10);
	REQUIRE(m->pendingCellId == -1);

	Test::destroyModule(m);
}


TEST_CASE("processMapUpdate - MIDI note-on sets pending", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	m->portAssignments[5].moduleId = 1;
	m->portAssignments[5].portId = 0;
	m->portAssignments[5].type = engine::Port::OUTPUT;

	m->processMapUpdate(MidiTrackingType::NOTE, 5, 100);
	REQUIRE(m->pendingCellId == 5);
	REQUIRE(m->pendingCellIsPhysical == false);

	Test::destroyModule(m);
}


TEST_CASE("processMapUpdate - momentary MIDI note-off clears pending", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	m->buttonMode = SpliceKitModule::BUTTON_MOMENTARY;
	m->portAssignments[7].moduleId = 1;
	m->portAssignments[7].portId = 0;
	m->portAssignments[7].type = engine::Port::OUTPUT;

	// Note-on: cell 7 becomes pending
	m->processMapUpdate(MidiTrackingType::NOTE, 7, 80);
	REQUIRE(m->pendingCellId == 7);

	// Note-off: momentary mode must clear it
	m->processMapUpdate(MidiTrackingType::NOTE, 7, 0);
	REQUIRE(m->pendingCellId == -1);

	Test::destroyModule(m);
}


TEST_CASE("processMapUpdate - toggle mode ignores note-off", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	m->buttonMode = SpliceKitModule::BUTTON_TOGGLE;
	m->portAssignments[4].moduleId = 1;
	m->portAssignments[4].portId = 0;
	m->portAssignments[4].type = engine::Port::OUTPUT;

	m->processMapUpdate(MidiTrackingType::NOTE, 4, 64);
	REQUIRE(m->pendingCellId == 4);

	// Note-off in toggle mode must NOT clear pending
	m->processMapUpdate(MidiTrackingType::NOTE, 4, 0);
	REQUIRE(m->pendingCellId == 4);

	Test::destroyModule(m);
}


TEST_CASE("portAssignment isValid and clear", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	REQUIRE(m->portAssignments[0].isValid() == false);

	m->portAssignments[0].moduleId = 10;
	m->portAssignments[0].portId = 2;
	m->portAssignments[0].type = engine::Port::INPUT;
	REQUIRE(m->portAssignments[0].isValid() == true);

	m->portAssignments[0].clear();
	REQUIRE(m->portAssignments[0].isValid() == false);

	Test::destroyModule(m);
}
