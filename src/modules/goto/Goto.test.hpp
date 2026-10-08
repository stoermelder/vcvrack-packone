#pragma once

// Shared preamble for the GOTO test suite.
// Included by Goto.test.cpp, which pulls the test cases in from Goto.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"

#include "Goto.cpp"

using namespace StoermelderPackOne;
using namespace StoermelderPackOne::Goto;

Test::TestContext<> testContext;

// A ModuleAccess mock that records getModuleWidget lookups (returns nullptr).
struct MockModuleAccess : vcv::ModuleAccess {
	mutable std::vector<int64_t> getModuleWidgetCalls;
	ModuleWidget* getModuleWidget(int64_t moduleId) const override {
		getModuleWidgetCalls.push_back(moduleId);
		return nullptr;
	}
};