#pragma once

// Shared preamble for the PILEPOLY test suite.
// Included by PilePoly.test.cpp, which pulls the test cases in from PilePoly.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"

#include "PilePoly.cpp"

using namespace StoermelderPackOne::PilePoly;

Test::TestContext<> testContext;