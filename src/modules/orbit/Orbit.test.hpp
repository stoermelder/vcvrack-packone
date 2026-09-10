#pragma once

// Shared preamble for the ORBIT test suite.
// Included by Orbit.test.cpp, which pulls the test cases in from Orbit.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"

#include "Orbit.cpp"

using namespace StoermelderPackOne::Orbit;

Test::TestContext<> testContext;