#pragma once

// Shared preamble for the REMOVE test suite.
// Included by ReMove.test.cpp, which pulls the test cases in from ReMove.test.json.hpp,
// ReMove.test.process.hpp and ReMove.test.playback.hpp, each inside its own namespace.

#include "../../test/framework.hpp"
#include "ReMove.cpp"

using namespace StoermelderPackOne;
using namespace StoermelderPackOne::ReMove;

Test::TestContext<> testContext;

// Records pushed actions and owns them (push takes ownership) — lets a test assert on what
// stopRecording() pushes without touching the real APP->history, which is unavailable headless.
struct MockHistoryAccess : vcv::HistoryAccess {
	std::vector<::rack::history::Action*> pushed;
	void push(::rack::history::Action* a) override { pushed.push_back(a); }
	~MockHistoryAccess() { for (auto* a : pushed) delete a; }
};