#pragma once

// Shared preamble for the INTERMIXCV test suite.
// Included by IntermixCv.test.cpp, which pulls the test cases in from IntermixCv.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"

#include "IntermixCv.cpp"

using namespace StoermelderPackOne::Intermix;

Test::TestContext<> testContext;
