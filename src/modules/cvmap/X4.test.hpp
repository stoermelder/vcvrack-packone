#pragma once

// Shared preamble for the X4 test suite.
// Included by X4.test.cpp, which pulls the test cases in from X4.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"
#include "X4.cpp"

using namespace StoermelderPackOne::X4;

Test::TestContext<> testContext;