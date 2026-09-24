#pragma once

// Shared preamble for the GRIP test suite.
// Included by Grip.test.cpp, which pulls the test cases in from Grip.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"
#include "Grip.cpp"

using namespace StoermelderPackOne::Grip;

Test::TestContext<> testContext;