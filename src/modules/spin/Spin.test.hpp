#pragma once

// Shared preamble for the SPIN test suite.
// Included by Spin.test.cpp, which pulls the test cases in from Spin.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"
#include "Spin.cpp"

using namespace StoermelderPackOne::Spin;

Test::TestContext<> testContext;