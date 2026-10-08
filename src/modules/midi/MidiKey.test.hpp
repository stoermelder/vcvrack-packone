#pragma once

// Shared preamble for the MIDIKEY test suite.
// Included by MidiKey.test.cpp, which pulls the test cases in from MidiKey.test.module.hpp
// inside their own namespace.

#include "../../test/framework.hpp"
#include "MidiKey.cpp"
#include "MidiKey.vcvm.test.h"

using namespace StoermelderPackOne;
using namespace StoermelderPackOne::MidiKey;

// rack::app::Scene is PRIVATE (deprecated) — Rack has no public subclassing seam for it, but
// this is exactly the escape hatch TestContext<TScene> exists for.
TEST_SUPPRESS_DEPRECATED_BEGIN
struct SceneEx : rack::app::Scene {
	std::vector<event::HoverKey> receivedKeys;
	void onHoverKey(const HoverKeyEvent& e) override {
		receivedKeys.push_back(e);
	}
};
TEST_SUPPRESS_DEPRECATED_END

Test::TestContext<SceneEx> testContext;