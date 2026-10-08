#pragma once

// Shared preamble for the MIRROR test suite.
// Included by Mirror.test.cpp, which pulls the test cases in from Mirror.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"
#include "Mirror.cpp"
#include "Macro.cpp"

using namespace StoermelderPackOne::Mirror;

Test::TestContext<> testContext;