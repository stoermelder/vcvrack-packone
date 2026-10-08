#pragma once

// Shared preamble for the CVMAPCTX test suite.
// Included by CVMapCtx.test.cpp, which pulls the test cases in from CVMapCtx.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"
#include "CVMapCtx.cpp"

using namespace StoermelderPackOne::CVMap;

Test::TestContext<> testContext;