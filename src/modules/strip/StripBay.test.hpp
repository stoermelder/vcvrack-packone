#pragma once

// Shared preamble for the STRIPBAY test suite.
// Included by StripBay.test.cpp, which pulls the test cases in from StripBay.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"
#include "StripBay.cpp"

using namespace StoermelderPackOne::StripBay;

Test::TestContext<> testContext;