#pragma once

// Shared preamble for the PILE test suite.
// Included by Pile.test.cpp, which pulls the test cases in from Pile.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"

#include "Pile.cpp"

using namespace StoermelderPackOne::Pile;

Test::TestContext<> testContext;