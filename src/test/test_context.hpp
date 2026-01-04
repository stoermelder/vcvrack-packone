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

		pluginInstance = new Plugin();
		init(pluginInstance);
	}

	~TestContext() {
		delete pluginInstance;

		// Context destructor handled by Rack; free our allocation
		if (ctx) {
			delete ctx;
		}
	}
};


template <typename T>
static T* createModule(std::string modelSlug) {
	Model* model = pluginInstance->getModel(modelSlug);
	T* m = dynamic_cast<T*>(model->createModule());
	return m;
}

template <typename T>
static T* createModuleWidget(Module* m) {
	T* mw = dynamic_cast<T*>(m->model->createModuleWidget(m));
	return mw;
}

static void addModule(rack::Module* m, rack::ModuleWidget* mw = nullptr) {
	APP->engine->addModule(m);
	if (mw) {
		APP->scene->rack->addModule(mw);
	}
}

static void removeModule(rack::Module* m, rack::ModuleWidget* mw = nullptr) {
	if (mw) {
		APP->scene->rack->removeModule(mw);
		// Deletes also m
		delete mw;
	}
	else {
		APP->engine->removeModule(m);
		delete m;
	}
}

} // namespace test