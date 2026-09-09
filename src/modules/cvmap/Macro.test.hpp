#pragma once

// Shared preamble for the MACRO test suite.
// Included by Macro.test.cpp, which pulls the test cases in from Macro.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"
#include "Macro.cpp"
#include "../glue/Glue.cpp"

using namespace StoermelderPackOne::Macro;

Test::TestContext<> testContext;