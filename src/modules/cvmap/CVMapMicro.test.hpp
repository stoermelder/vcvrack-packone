#pragma once

// Shared preamble for the CVMAPMICRO test suite.
// Included by CVMapMicro.test.cpp, which pulls the test cases in from CVMapMicro.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"
#include "CVMapMicro.cpp"

using namespace StoermelderPackOne::CVMapMicro;

Test::TestContext<> testContext;