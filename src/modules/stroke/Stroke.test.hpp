#pragma once

// Shared preamble for the STROKE test suite.
// Included by Stroke.test.cpp, which pulls the test cases in from Stroke.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"
#include "Stroke.cpp"

using namespace StoermelderPackOne;
using namespace StoermelderPackOne::Stroke;

Test::TestContext<> testContext;

// Number of ports exposed by the registered Stroke module. Used to keep tests
// in sync with the PORTS template parameter of the registered Model.
static constexpr int STROKE_PORTS = 10;