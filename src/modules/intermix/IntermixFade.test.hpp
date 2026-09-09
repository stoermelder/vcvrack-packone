#pragma once

// Shared preamble for the INTERMIXFADE test suite.
// Included by IntermixFade.test.cpp, which pulls the test cases in from IntermixFade.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"

#include "IntermixFade.cpp"

using namespace StoermelderPackOne::Intermix;

Test::TestContext<> testContext;

// Mock that captures the actual float values passed to expSetFade.
// Used to verify that the expander sends seconds directly, not seconds * maxFade.
template<int PORTS>
struct CapturingIntermixMock : Module, IntermixBase<PORTS> {
	alignas(16) float currentMatrix[PORTS][PORTS];
	int channelCount = 1;
	uint32_t fadeInTs[PORTS] = {};
	uint32_t fadeOutTs[PORTS] = {};

	float lastFadeIn[PORTS] = {};
	float lastFadeOut[PORTS] = {};
	bool fadeInReceived = false;
	bool fadeOutReceived = false;

	CapturingIntermixMock() {
		config(0, 0, 0, 0);
		model = modelIntermixFade;
		for (int i = 0; i < PORTS; i++)
			for (int j = 0; j < PORTS; j++)
				currentMatrix[i][j] = 0.f;
	}

	typename IntermixBase<PORTS>::IntermixMatrix expGetCurrentMatrix() override { return currentMatrix; }
	int expGetChannelCount() override { return channelCount; }

	void expSetFade(int i, float* fadeIn, float* fadeOut) override {
		if (fadeIn) {
			fadeInReceived = true;
			for (int j = 0; j < PORTS; j++) lastFadeIn[j] = fadeIn[j];
		}
		if (fadeOut) {
			fadeOutReceived = true;
			for (int j = 0; j < PORTS; j++) lastFadeOut[j] = fadeOut[j];
		}
	}

	void process(const ProcessArgs& args) override {
		rightExpander.producerMessage = (IntermixBase<PORTS>*)this;
		rightExpander.messageFlipRequested = true;
	}
};

// Forward declare Intermix module type for expander tests
template<int PORTS>
struct IntermixModuleMock : Module, IntermixBase<PORTS> {
	alignas(16) float currentMatrix[PORTS][PORTS];
	int channelCount = 1;
	uint32_t fadeInTs[PORTS] = {};
	uint32_t fadeOutTs[PORTS] = {};
	
	IntermixModuleMock() {
		config(0, 0, 0, 0);
		model = modelIntermixFade;
		for (int i = 0; i < PORTS; i++) {
			for (int j = 0; j < PORTS; j++) {
				currentMatrix[i][j] = 0.f;
			}
		}
	}
	
	typename IntermixBase<PORTS>::IntermixMatrix expGetCurrentMatrix() override {
		return currentMatrix;
	}
	
	int expGetChannelCount() override { return channelCount; }
	
	void expSetFade(int i, float* fadeIn, float* fadeOut) override {
		if (fadeIn) fadeInTs[i]++;
		if (fadeOut) fadeOutTs[i]++;
	}
	
	void process(const ProcessArgs& args) override {
		rightExpander.producerMessage = (IntermixBase<PORTS>*)this;
		rightExpander.messageFlipRequested = true;
	}
};

