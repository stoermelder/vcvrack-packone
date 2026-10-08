#pragma once

// Shared preamble for the AFFIX test suite.
// Included by Affix.test.cpp, which pulls the test cases in from Affix.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"
#include "Affix.cpp"

using namespace StoermelderPackOne::Affix;

// Helper to create module with proper SampleRateChangeEvent
template <int CHANNELS>
static AffixModule<CHANNELS>* createAffixModule(std::string modelSlug) {
	auto module = Test::createModule<AffixModule<CHANNELS>>(modelSlug);
	return module;
}

Test::TestContext<> testContext;