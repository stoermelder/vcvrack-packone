#pragma once
#include "test_plugin.hpp"
#include <rack.hpp>
#include <patch.hpp>
#include <atomic>
#include <vector>
#include <string>
#include <utility>

namespace Test {

static std::atomic<int> testContextCount{0};

// Registry for model pointer sync (see registerModelSync below).
static std::vector<std::pair<std::string, Model**>>& modelSyncRegistry() {
	static std::vector<std::pair<std::string, Model**>> reg;
	return reg;
}

// Call this before TestContext is created (typically as a file-scope static
// initializer) to ensure a module's model global in this TU is updated to the
// pointer registered by init() in the plugin dylib.
//
// Background: test binaries both #include a module's .cpp (defining a model
// global in the test TU) and link the plugin dylib (which has its own copy of
// that global used by init()). After init() runs, the registered pointer lives
// in the dylib; process() compiled inline uses this TU's pointer. Without this
// sync, expander model checks always fail.
static void registerModelSync(const std::string& slug, Model** ptr) {
	modelSyncRegistry().push_back({slug, ptr});
}

// Declare before TestContext to schedule a model pointer sync.
// Usage: SYNC_MODEL(modelFoo, "Foo");
#define SYNC_MODEL(ptr, slug) \
	static bool _syncModel_##ptr = (Test::registerModelSync(slug, &ptr), true)

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

			for (auto& entry : modelSyncRegistry()) {
				if (auto* m = pluginInstance->getModel(entry.first)) *entry.second = m;
			}

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


static int64_t getModuleId() {
	static std::atomic<int64_t> nextModuleId{1};
	return nextModuleId.fetch_add(1, std::memory_order_acq_rel);
}



template <typename T>
static T* createModule(std::string modelSlug) {
	Model* model = pluginInstance->getModel(modelSlug);
	T* m = dynamic_cast<T*>(model->createModule());
	m->id = getModuleId();

	Module::SampleRateChangeEvent e;
	e.sampleRate = APP->engine->getSampleRate();
	e.sampleTime = 1.0f / e.sampleRate;
	m->onSampleRateChange(e);

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

// Creates a ModuleWidget, without a module, the same way as the module browser
template <typename T>
static T* createWidget(std::string modelSlug) {
	Model* m = pluginInstance->getModel(modelSlug);
	T* mw = dynamic_cast<T*>(m->createModuleWidget(NULL));
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

static const Module::ProcessArgs makeProcessArgs(int64_t frame, float sampleRate = 44100.f) {
	Module::ProcessArgs args;
	args.sampleRate = sampleRate;
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


// SimpleEngine simulates a VCV Rack engine step for module testing.
// This class manages anlist of modules and processes them in sequence,
// automatically flipping expander producer/consumer messages between each step.
// This mimics how the VCV Rack engine processes modules and flips expanders.
//
// Usage:
// Test::SimpleEngine testEngine;
// testEngine.registerModules(moduleA, moduleB); 
// A -> B chain
//
// testEngine.step();  // Process both modules with message flipping
// testEngine.step();  // Continue processing...
struct SimpleEngine {
	std::list<Module*> modules;
	int frame = 0;

	void step() {
		auto args = Test::makeProcessArgs(frame);
		for (Module* module : modules) {
			module->process(args);
			std::swap(module->leftExpander.producerMessage, module->leftExpander.consumerMessage);
			std::swap(module->rightExpander.producerMessage, module->rightExpander.consumerMessage);
		}
		frame++;
 	}

	void registerModule(Module* m) {
		modules.push_back(m);
	}

	/// Register multiple modules at once.
	void registerModule(std::initializer_list<Module*> modules) {
		for (Module* m : modules) {
			this->modules.push_back(m);
		}
	}

	/// Register multiple modules with variadic template.
	template <typename... T>
	void registerModules(T*... _m) {
		Module* arr[] = {_m...};
		for (Module* m : arr) {
			this->modules.push_back(m);
		}
	}
};


} // namespace Test