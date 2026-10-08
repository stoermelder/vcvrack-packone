#pragma once

// Shared preamble for the INTERMIX test suite.
// Included by Intermix.test.cpp, which pulls the test cases in from Intermix.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"
#include "Intermix.cpp"

using namespace StoermelderPackOne::Intermix;

Test::TestContext<> testContext;