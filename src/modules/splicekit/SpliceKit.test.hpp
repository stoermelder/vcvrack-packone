#pragma once

// Shared preamble and helpers for the SpliceKit test suite.
// Included by every SpliceKit*.test.cpp file; each test file builds its
// own binary (see plugin-test.mk), so the test context is per-binary.

#include "../../test/framework.hpp"
#include "SpliceKit.cpp"

using namespace StoermelderPackOne;
using namespace StoermelderPackOne::SpliceKit;

SYNC_MODEL(modelSpliceKit, "SpliceKit");
Test::TestContext<> testContext;

// Shadows Test::createModule to put taskProcessorUi in sync mode right after construction.
// Without it, taskProcessorUi.process() (every processDivider tick) starts a real worker
// thread once APP->window is null — always true headless — which races the test thread over
// crossPending/the instance registry (GUI-thread-only in production) and any module destroyed
// mid-callback. Sync mode collapses enqueue() to an inline call and makes process() a no-op.
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


// ---- Mock cable registry ----
// Production routes cables through vcv::hasCable/addCable/removeCable (vcv_cables.hpp), which
// consult the null-by-default cableAccess. This registry lets the suite assert a cable
// actually appears/disappears without a RackWidget/CableWidget tree. Keyed output -> inputs.
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

// RAII installs a MockCableRegistry as the active vcv::cableAccess, restoring the previous on
// exit. Declare first in a test body so the mock outlives the override through scope exit
// (including Catch2 unwinding on a failed REQUIRE).
struct CableScaffold {
	MockCableRegistry mock;
	vcv::CableAccess* prev;
	CableScaffold() : prev(vcv::cableAccess) { vcv::cableAccess = &mock; }
	~CableScaffold() { vcv::cableAccess = prev; }
};

// RAII owner for modules under test (see Test::ModuleScaffold for why bare create/destroy is
// unsafe once an assertion can fail). Binds to this suite's createModule() shadow so every
// scaffolded module gets taskProcessorUi.syncMode set. SpliceKitModule needs no extra teardown:
// it drops its two process-wide statics (getInstances(), crossPending()) in its destructor and
// onRemove(), both fired by Test::destroyModule().
struct ModuleScaffold : Test::ModuleScaffold<SpliceKitModule> {
	ModuleScaffold() : Test::ModuleScaffold<SpliceKitModule>([]() { return createModule(); }) {}
};