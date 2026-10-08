#pragma once

// Shared preamble for the SIPO test suite.
// Included by Sipo.test.cpp, which pulls the test cases in from Sipo.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"
#include "Sipo.cpp"

using namespace StoermelderPackOne::Sipo;

Test::TestContext<> testContext;