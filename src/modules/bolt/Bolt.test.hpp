#pragma once

// Shared preamble for the BOLT test suite.
// Included by Bolt.test.cpp, which pulls the test cases in from Bolt.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"
#include "Bolt.cpp"

using namespace StoermelderPackOne::Bolt;

Test::TestContext<> testContext;