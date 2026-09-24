#pragma once

// Shared preamble for the ROTORA test suite.
// Included by RotorA.test.cpp, which pulls the test cases in from RotorA.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"

#include "RotorA.cpp"

using namespace StoermelderPackOne::RotorA;

Test::TestContext<> testContext;