#pragma once

// Shared preamble for the SAIL test suite: includes and the TestContext.
// Included by Sail.test.cpp, which pulls the test cases in from Sail.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"
#include "Sail.cpp"

using namespace StoermelderPackOne::Sail;

Test::TestContext<> testContext;