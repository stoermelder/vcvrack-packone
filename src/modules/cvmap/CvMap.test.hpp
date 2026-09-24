#pragma once

// Shared preamble for the CVMAP test suite.
// Included by CvMap.test.cpp, which pulls the test cases in from CvMap.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"
#include "CVMap.cpp"

using namespace StoermelderPackOne::CVMap;

Test::TestContext<> testContext;