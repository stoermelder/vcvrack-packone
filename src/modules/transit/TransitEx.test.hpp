#pragma once

// Shared preamble for the TRANSITEX test suite.
// Included by TransitEx.test.cpp, which pulls the test cases in from TransitEx.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"
#include "TransitBase.hpp"
#include "Transit.cpp"
#include "TransitEx.cpp"
// NOTE: TransitEx.cpp is NOT included here to avoid duplicate definition of
// modelTransitEx (it is already exported from the plugin dylib and linked in).
// TransitExModule instances are created via the model factory and accessed
// through the TransitBase<12> interface.

using namespace StoermelderPackOne::Transit;

Test::TestContext<> testContext;

// Helper module with test parameters
struct TestModule : rack::Module {
	enum ParamIds {
		TEST_PARAM_1,
		TEST_PARAM_2,
		NUM_PARAMS
	};

	TestModule() {
		config(NUM_PARAMS, 0, 0, 0);
		configParam(TEST_PARAM_1, 0.f, 1.f, 0.5f, "Test Parameter 1");
		configParam(TEST_PARAM_2, 0.f, 10.f, 5.f, "Test Parameter 2");
	}
};

// ----------------------------------------------------------------
// Helper: create a TransitEx module via the plugin factory and set
// its sample-rate event, just like Test::createModule does.
// Returns the raw Module* pointer alongside a TransitBase<12>* view.
// ----------------------------------------------------------------
static Module* createExModule(TransitBase<12>** baseOut = nullptr) {
	Model* model = pluginInstance->getModel("TransitEx");
	REQUIRE(model != nullptr);
	Module* m = model->createModule();
	m->id = Test::getModuleId();

	Module::SampleRateChangeEvent e;
	e.sampleRate = Test::sampleRate();
	e.sampleTime = 1.0f / e.sampleRate;
	m->onSampleRateChange(e);

	if (baseOut) {
		*baseOut = dynamic_cast<TransitBase<12>*>(m);
		REQUIRE(*baseOut != nullptr);
	}
	return m;
}

// h.connectExpander()/h.connectChain() dispatch Rack's real onExpanderChange, and Transit's
// override (Transit.cpp:207) calls notifyModuleListeners("Transit"), which sets
// moduleChangedFlag -- no hand-written `transit->moduleChangedFlag = true;` needed.
//
// Two things worth knowing before trusting a failure after connecting/disconnecting an expander
// here to mean what it looks like:
//
//  - **The notification is redundant on purpose.** BOTH sides notify: Transit.cpp:207 and
//    TransitEx.cpp:48 each call notifyModuleListeners("Transit"), and the harness dispatches
//    onExpanderChange to both modules. Deleting either one alone keeps this suite green;
//    deleting both crashes it. So a single-sided regression is NOT caught here.
//  - **The first step would rescan anyway.** The gate is
//    `moduleChangedFlag || ctrlMode != BASE::ctrlMode` (Transit.cpp:265), and on a freshly built
//    module the ctrlMode comparison is already true. Every SECTION that connects an expander
//    before stepping would therefore pass even with no notification at all -- see the
//    "connected after Transit has settled" case for one that removes that second term.