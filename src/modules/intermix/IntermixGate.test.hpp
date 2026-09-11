#pragma once

// Shared preamble for the INTERMIXGATE test suite.
// Included by IntermixGate.test.cpp, which pulls the test cases in from IntermixGate.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"

#include "IntermixGate.cpp"

using namespace StoermelderPackOne::Intermix;

Test::TestContext<> testContext;

// Forward declare Intermix module type for expander tests
template<int PORTS>
struct IntermixModuleMock : Module, IntermixBase<PORTS> {
	alignas(16) float currentMatrix[PORTS][PORTS];
	
	IntermixModuleMock() {
		config(0, 0, 0, 0);
		model = modelIntermixGate;
		for (int i = 0; i < PORTS; i++) {
			for (int j = 0; j < PORTS; j++) {
				currentMatrix[i][j] = 0.f;
			}
		}
	}
	
	typename IntermixBase<PORTS>::IntermixMatrix expGetCurrentMatrix() override {
		return currentMatrix;
	}
	
	int expGetChannelCount() override { return 1; }
	void expSetFade(int i, float* fadeIn, float* fadeOut) override { }
	
	void process(const ProcessArgs& args) override {
		rightExpander.producerMessage = (IntermixBase<PORTS>*)this;
		rightExpander.messageFlipRequested = true;
	}
};