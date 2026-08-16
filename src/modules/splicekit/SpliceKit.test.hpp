#pragma once

// Shared preamble and helpers for the SpliceKit test suite.
// Included by every SpliceKit*.test.cpp file; each test file builds its
// own binary (see plugin-test.mk), so the test context is per-binary.

#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "SpliceKit.cpp"

using namespace StoermelderPackOne;
using namespace StoermelderPackOne::SpliceKit;

SYNC_MODEL(modelSpliceKit, "SpliceKit");
Test::TestContext<> testContext;

// Shadows Test::createModule<SpliceKitModule> for this suite: puts taskProcessorUi into
// sync mode (see GuiTaskProcessor::syncMode) right after construction, before any test
// code can call triggerCell()/process() and give it a chance to enqueue something. With
// sync mode off, taskProcessorUi.process() (called every processDivider tick, from
// SpliceKitModule::process()) starts a REAL background worker thread the instant
// APP->window is null — which it always is in this headless test binary. That worker
// then races the test's own thread over crossPending/the instance registry (both
// documented GUI-thread-only, an invariant the production code upholds by construction
// but this harness cannot), and over any module the test destroys while the worker is
// still mid-callback. Sync mode collapses enqueue() to an inline call and makes
// process() a no-op, so no such thread ever exists — the same fix MidiKit's
// SyncTaskWorker (TaskWorker.hpp) applies for its own worker abstraction.
static SpliceKitModule* createModule() {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->taskProcessorUi.syncMode = true;
	return m;
}

// Helper: build a preset with NOTE_ON + FIXED mode for all states.
static MidiOutPreset makeNoteOnPreset(int note = 36, int value = 127) {
	MidiOutPreset preset;
	for (int s = 0; s < LED_STATE_COUNT; s++) {
		preset.specs[s].type = MIDI_OUT_NOTE_ON;
		preset.specs[s].noteMode = MIDI_OUT_FIXED;
		preset.specs[s].note = note;
		preset.specs[s].value = value;
	}
	return preset;
}


// ---- Mock cable registry (SpliceKit test review 2.1) ----
// Production Splice-Kit routes cable create/remove/find through vcv::hasCable/addCable/
// removeCable (see vcv_cables.hpp), which consult the null-by-default cableAccess pointer.
// Installing this registry lets the suite assert the observable patch effect — a cable
// actually appearing/disappearing — without a RackWidget/CableWidget tree. Cables are keyed
// by output port -> set of input ports, the same orientation every vcv:: call site uses
// (output first).
#include <map>
#include <set>
#include <cstdint>
struct MockCableRegistry : vcv::CableAccess {
	using vcv::CableAccess::removeCable;  // keep the object-view overload visible beside the port-pair one

	// (output moduleId, output portId) -> set of (input moduleId, input portId)
	std::map<std::pair<int64_t, int>, std::set<std::pair<int64_t, int>>> cables;

	bool hasCable(int64_t outModuleId, int outPortId, int64_t inModuleId, int inPortId) const override {
		auto it = cables.find({outModuleId, outPortId});
		return it != cables.end() && it->second.count({inModuleId, inPortId}) > 0;
	}

	void addCable(int64_t outModuleId, int outPortId, int64_t inModuleId, int inPortId, bool /*addToHistory*/) override {
		cables[{outModuleId, outPortId}].insert({inModuleId, inPortId});
	}

	void removeCable(int64_t outModuleId, int outPortId, int64_t inModuleId, int inPortId, bool /*addToHistory*/) override {
		auto it = cables.find({outModuleId, outPortId});
		if (it == cables.end()) return;
		it->second.erase({inModuleId, inPortId});
		if (it->second.empty()) cables.erase(it);
	}
};

// RAII installs a MockCableRegistry as the active vcv::cableAccess for the enclosing test
// and restores the previous access on exit. Declare it first in a test body: it owns the
// mock, so the mock outlives the override and both are torn down on scope exit — also when
// a REQUIRE fails and Catch2 unwinds the body.
struct CableScaffold {
	MockCableRegistry mock;
	vcv::CableAccess* prev;
	CableScaffold() : prev(vcv::cableAccess) { vcv::cableAccess = &mock; }
	~CableScaffold() { vcv::cableAccess = prev; }
};