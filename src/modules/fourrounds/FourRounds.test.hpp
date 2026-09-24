#pragma once

// Shared preamble for the FOURROUNDS test suite.
// Included by FourRounds.test.cpp, which pulls the test cases in from FourRounds.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"

#include "FourRounds.cpp"

using namespace StoermelderPackOne::FourRounds;

Test::TestContext<> testContext;