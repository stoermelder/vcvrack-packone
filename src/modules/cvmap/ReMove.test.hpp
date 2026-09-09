#pragma once

// Shared preamble for the REMOVE test suite.
// Included by ReMove.test.cpp, which pulls the test cases in from ReMove.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"
#include "ReMove.cpp"

using namespace StoermelderPackOne;
using namespace StoermelderPackOne::ReMove;

Test::TestContext<> testContext;