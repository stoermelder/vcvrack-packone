#pragma once

// Shared preamble for the STRIPPP test suite.
// Included by StripPp.test.cpp, which pulls the test cases in from StripPp.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"
#include "StripPp.cpp"

using namespace StoermelderPackOne::Strip;

Test::TestContext<> testContext;