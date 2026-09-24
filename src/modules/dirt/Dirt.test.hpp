#pragma once

// Shared preamble for the DIRT test suite.
// Included by Dirt.test.cpp, which pulls the test cases in from Dirt.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"
#include "Dirt.cpp"

using namespace StoermelderPackOne::Dirt;

Test::TestContext<> testContext;