#pragma once

// Shared preamble for the GLUE test suite.
// Included by Glue.test.cpp, which pulls the test cases in from Glue.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"
#include "Glue.cpp"

using namespace StoermelderPackOne;
using namespace StoermelderPackOne::Glue;

Test::TestContext<> testContext;

// Records pushed actions and owns them (push takes ownership). Same shape as
// Strip.test.cpp's MockHistoryAccess.
struct MockHistoryAccess : vcv::HistoryAccess {
	std::vector<::rack::history::Action*> pushed;
	void push(::rack::history::Action* a) override { pushed.push_back(a); }
	~MockHistoryAccess() { for (auto* a : pushed) delete a; }
};