#pragma once

// Shared preamble for the RAW test suite.
// Included by Raw.test.cpp, which pulls the test cases in from Raw.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"

#include "Raw.cpp"

using namespace StoermelderPackOne::Raw;

Test::TestContext<> testContext;