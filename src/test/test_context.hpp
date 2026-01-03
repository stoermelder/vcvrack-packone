#pragma once
#include "catch2/plugin.hpp"
#include <rack.hpp>
#include <patch.hpp>

namespace Test {

// Test-only context initializer to prevent APP (rack::contextGet()) from being null
// Included by test harness only.
// Allows specifying a custom Scene type for event testing.
template<typename TScene = rack::app::Scene>
struct TestContext {
	rack::Context* ctx = nullptr;
	TScene* scene;

#if defined(__clang__)
	#pragma clang diagnostic push
	#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__) || defined(__GNUG__)
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

	TestContext() {
		// Ensure headless mode for tests
		settings::headless = true;
		
		ctx = new rack::Context();
		rack::contextSet(ctx);
		// Create a minimal EventState so code that dereferences APP->event won't segfault
		ctx->engine = new rack::engine::Engine;
		ctx->event = new rack::widget::EventState();
		scene = new TScene();
		ctx->scene = scene;
		ctx->event->rootWidget = ctx->scene;
	}

	~TestContext() {
		// Context destructor handled by Rack; free our allocation
		if (ctx) {
			delete ctx;
		}
	}

#if defined(__clang__)
	#pragma clang diagnostic pop
#elif defined(__GNUC__) || defined(__GNUG__)
	#pragma GCC diagnostic pop
#endif
};

} // namespace test