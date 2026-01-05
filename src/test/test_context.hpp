#pragma once
#include "catch2/plugin.hpp"
#include <rack.hpp>
#include <patch.hpp>
#include <atomic>

namespace Test {

static std::atomic<int> testContextCount{0};

// Test-only context initializer to prevent APP (rack::contextGet()) from being null
// Included by test harness only.
// Allows specifying a custom Scene type for event testing.
template<typename TScene = rack::app::Scene>
struct TestContext {
	rack::Context* ctx = NULL;
	TScene* scene;

	TestContext() {
		// Ensure headless mode for tests
		settings::headless = true;

		// If this is the first TestContext, create and initialize the pluginInstance and context
		if (testContextCount.fetch_add(1, std::memory_order_acq_rel) == 0) {
			pluginInstance = new Plugin();
			init(pluginInstance);

			ctx = new rack::Context();
			rack::contextSet(ctx);
			// Create a minimal EventState so code that dereferences APP->event won't segfault
			ctx->engine = new rack::engine::Engine;
			ctx->event = new rack::widget::EventState();
			scene = new TScene();
			ctx->scene = scene;
			ctx->event->rootWidget = ctx->scene;
		}
		else {
			// Context already exists; reuse it
			ctx = rack::contextGet();
			scene = dynamic_cast<TScene*>(ctx->scene);
		}
	} 

	~TestContext() {
		// If this is the last TestContext, delete the pluginInstance
		if (testContextCount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
			delete pluginInstance;
			pluginInstance = nullptr;

			// Context destructor handled by Rack; free our allocation
			if (ctx) {
				delete ctx;
			}
		}
	}
};


template <typename T>
static T* createModule(std::string modelSlug) {
	Model* model = pluginInstance->getModel(modelSlug);
	T* m = dynamic_cast<T*>(model->createModule());
	return m;
}

static void destroyModule(rack::Module* m) {
	delete m;
}

// Creates a ModuleWidget and adds it to the engine
template <typename T>
static T* createWidget(Module* m) {
	T* mw = dynamic_cast<T*>(m->model->createModuleWidget(m));
	return mw;
}

static void destroyWidget(rack::ModuleWidget* mw) {
	mw->module = NULL;
	delete mw;
}

static void registerModule(rack::Module* m, rack::ModuleWidget* mw = nullptr) {
	APP->engine->addModule_NoLock(m);
	if (mw) {
		APP->scene->rack->addModule(mw);
	}
}

static void unregisterModule(rack::Module* m, rack::ModuleWidget* mw = nullptr) {
	if (mw) {
		APP->scene->rack->removeModule(mw);
		mw->module = NULL;
		APP->engine->removeModule_NoLock(m);
		delete mw;
	}
	else {
		APP->engine->removeModule_NoLock(m);
	}
}

static const Module::ProcessArgs makeProcessArgs(int64_t frame) {
	Module::ProcessArgs args;
	args.sampleRate = 44100.0f;
	args.sampleTime = 1.0f / args.sampleRate;
	args.frame = frame;
	return args;
}

// Helper: construct a simple 3-byte MIDI message.
// - statusNibble: high nibble of status (e.g., 0xb for CC)
// - channel: low nibble (0-15)
// - b1: first data byte (e.g., CC number)
// - b2: second data byte (e.g., value)
static const rack::midi::Message makeMidiMessage(uint8_t statusNibble, uint8_t channel, uint8_t b1, uint8_t b2, int64_t frame = 0) {
	rack::midi::Message m;
	m.frame = frame;
	m.bytes = { static_cast<unsigned char>((statusNibble << 4) | (channel & 0x0f)), static_cast<unsigned char>(b1), static_cast<unsigned char>(b2) };
	return m;
}

} // namespace Test