#pragma once

// Shared preamble for the CVPAM test suite.
// Included by CVPam.test.cpp, which pulls the test cases in from CVPam.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"
#include "CVPam.cpp"
#include "../glue/Glue.cpp"

using namespace StoermelderPackOne::CVPam;

Test::TestContext<> testContext;