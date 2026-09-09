#pragma once

// Shared preamble for the TRANSIT test suite.
// Included by Transit.test.cpp, which pulls the test cases in from Transit.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"
#include "TransitBase.hpp"
#include "Transit.cpp"

using namespace StoermelderPackOne::Transit;

Test::TestContext<> testContext;