#pragma once
#include "test_plugin.hpp"
#include <rack.hpp>
#include <patch.hpp>
#include <atomic>
#include <vector>
#include <string>
#include <utility>
#include <functional>

namespace Test {

// Function-local static, not a header-scope `static` variable, so every TU that includes this
// header shares one counter instead of getting its own private copy (see A6 in the framework
// review — this is the same trap `#pragma once` does not protect against once a test binary
// links more than one TU).
inline std::atomic<int>& testContextCount() {
	static std::atomic<int> count{0};
	return count;
}

// Registry for model pointer sync (see registerModelSync below).
inline std::vector<std::pair<std::string, Model**>>& modelSyncRegistry() {
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
inline void registerModelSync(const std::string& slug, Model** ptr) {
	modelSyncRegistry().push_back({slug, ptr});
}

// Declare before TestContext to schedule a model pointer sync.
// Usage: SYNC_MODEL(modelFoo, "Foo");
#define SYNC_MODEL(ptr, slug) \
	static bool _syncModel_##ptr = (Test::registerModelSync(slug, &ptr), true)

// Asserts that `model` (a TU-local model global, e.g. modelFoo) was actually synced to the
// slug's model in the plugin dylib — i.e. that a matching SYNC_MODEL(model, slug) ran before
// this call. Call it once, right after constructing the peer(s) whose `->model` you are about
// to compare against `model` (an expander check like `exp->model == modelMidiCatMem`).
//
// SYNC_MODEL itself cannot enforce this: registerModelSync() only records an *intent* to sync,
// and the sync loop in TestContext's constructor runs unconditionally for every registered
// entry, so a present SYNC_MODEL can't fail quietly. The bug this catches is the opposite one —
// a module whose expander code compares against a global for which SYNC_MODEL was never called
// at all. Without this, `exp->model == modelMidiCatMem` silently compares the test TU's own
// stale copy of the model pointer against the dylib's, which never matches, and the resulting
// "wrong expander" behaviour reads as a logic bug rather than a missing test-harness call.
//
// Usage: SYNC_MODEL(modelMidiCatMem, "MidiCatMem"); ... Test::requireModelSync(modelMidiCatMem, "MidiCatMem");
inline void requireModelSync(Model* model, const std::string& slug) {
	CATCH_INFO("Missing SYNC_MODEL(..., \"" << slug << "\") — model global for '" << slug
		<< "' was never registered for sync with the plugin dylib, or TestContext hasn't run yet");
	REQUIRE(model != nullptr);
	REQUIRE(pluginInstance != nullptr);
	Model* dylibModel = pluginInstance->getModel(slug);
	CATCH_INFO("Model global for '" << slug << "' does not match the plugin dylib's model for that "
		"slug — check that SYNC_MODEL(..., \"" << slug << "\") passes the same global that gets "
		"compared elsewhere (e.g. in expander peer checks)");
	REQUIRE(model == dylibModel);
}

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
		// Thread assertions (see utils/thread.hpp) verify against Rack's UI thread and a
		// module's own GuiTaskProcessor worker, neither of which runs in a test binary —
		// the tests drive module methods from their own thread. Each module instance
		// builds its own ThreadVerifier via thread::makeVerifier() in its constructor,
		// which reads this flag, so setting it here — before Test::createModule() ever
		// constructs a module — is enough to make every module's asserts inert for the
		// whole test binary.
		StoermelderPackOne::thread::verifyEnabled = false;

		// If this is the first TestContext, create and initialize the pluginInstance and context
		if (testContextCount().fetch_add(1, std::memory_order_acq_rel) == 0) {
			pluginInstance = new Plugin();
			init(pluginInstance);
			{
				json_error_t err;
				json_t* pJ = json_load_file("plugin.json", 0, &err);
				if (pJ) {
					json_t* slugJ = json_object_get(pJ, "slug");
					if (slugJ) pluginInstance->slug = json_string_value(slugJ);
					json_decref(pJ);
				}
			}
			rack::plugin::plugins.push_back(pluginInstance);

			for (auto& entry : modelSyncRegistry()) {
				if (auto* m = pluginInstance->getModel(entry.first)) *entry.second = m;
			}

			ctx = new rack::Context();
			rack::contextSet(ctx);
			// Create a minimal EventState so code that dereferences APP->event won't segfault.
			// Ownership: rack::Context::~Context() (src/context.cpp) deletes window, patch,
			// scene, event, history and engine unconditionally ("Deleting NULL is safe in
			// C++"), so `delete ctx` below is sufficient to free all three of these — they
			// must NOT also be deleted here, or ~TestContext() would double-free them.
			TEST_SUPPRESS_DEPRECATED_BEGIN
			ctx->engine = new rack::engine::Engine;
			TEST_SUPPRESS_DEPRECATED_END
			ctx->event = new rack::widget::EventState();
			TEST_SUPPRESS_DEPRECATED_BEGIN
			scene = new TScene();
			TEST_SUPPRESS_DEPRECATED_END
			ctx->scene = scene;
			ctx->event->rootWidget = ctx->scene;
		}
		else {
			// Context already exists; reuse it
			ctx = rack::contextGet();
			scene = dynamic_cast<TScene*>(ctx->scene);
			// A second TestContext<CustomScene> after a first TestContext<> (or with a
			// different CustomScene) would otherwise silently leave `scene` null.
			REQUIRE(scene != nullptr);
		}
	}

	~TestContext() {
		// If this is the last TestContext, delete the pluginInstance
		if (testContextCount().fetch_sub(1, std::memory_order_acq_rel) == 1) {
			auto it = std::find(rack::plugin::plugins.begin(), rack::plugin::plugins.end(), pluginInstance);
			rack::plugin::plugins.erase(it);
			delete pluginInstance;
			pluginInstance = nullptr;

			// Frees ctx->engine, ctx->event and ctx->scene too — see the ownership note
			// in the constructor above.
			delete ctx;
			ctx = nullptr;
			rack::contextSet(nullptr);
		}
	}
};


inline int64_t getModuleId() {
	static std::atomic<int64_t> nextModuleId{1};
	return nextModuleId.fetch_add(1, std::memory_order_acq_rel);
}



// Single source of truth for the sample rate a test runs at: the engine's own rate. Used by both
// createModule() (to configure a module via onSampleRateChange) and makeProcessArgs() (to step
// it), so the two can never disagree even if a test changes the engine's sample rate.
inline float sampleRate() {
	return APP->engine->getSampleRate();
}

template <typename T>
inline T* createModule(std::string modelSlug) {
	Model* model = pluginInstance->getModel(modelSlug);
	T* m = dynamic_cast<T*>(model->createModule());
	m->id = getModuleId();

	Module::SampleRateChangeEvent e;
	e.sampleRate = Test::sampleRate();
	e.sampleTime = 1.0f / e.sampleRate;
	m->onSampleRateChange(e);

	return m;
}

inline void destroyModule(rack::Module* m) {
	Module::RemoveEvent eRemove;
	m->onRemove(eRemove);
	delete m;
}

// RAII owner for modules under test — destroys them even when Catch2 unwinds the body.
//
// The usual `T* m = Test::createModule<T>(...); ... Test::destroyModule(m);` pattern leaks the
// module whenever an assertion fails, because a failing REQUIRE throws and the trailing
// destroyModule() never runs. For a self-contained module that only wastes memory, but any
// module that registers itself in process-wide state — a static instance registry, a listener
// list, a shared "pending" entry — leaves that entry behind pointing at freed memory. Every
// later test in the same binary then sees the leaked peer, so ONE genuine failure cascades
// into several misleading ones. (Observed in the SpliceKit suite: 2 real failures reported as
// 4, via SpliceKitModule::getInstances().)
//
// Modules are destroyed in reverse creation order, and destroyModule() fires each module's
// onRemove() — which is where a well-behaved module drops its own shared-state entries.
//
// Usage:
//   Test::ModuleScaffold<MyModule> mods;
//   MyModule* m = mods.create("MyModule");        // model slug, as createModule<T>()
//   MyModule* peer = mods.create("MyModule");     // destroyed before m, no explicit cleanup
//
// Suites that shadow Test::createModule() with their own factory (e.g. to set a flag right
// after construction) can pass it instead, keeping that setup on every scaffolded module:
//   Test::ModuleScaffold<MyModule> mods{[]{ return myCreateModule(); }};
//   MyModule* m = mods.create();
template <typename T>
struct ModuleScaffold {
	// Optional factory; when unset, create(slug) uses Test::createModule<T>(slug).
	std::function<T*()> factory;
	std::vector<T*> modules;

	ModuleScaffold() = default;
	explicit ModuleScaffold(std::function<T*()> factory) : factory(std::move(factory)) {}

	// Creates a module via the factory, if one was supplied, otherwise via the model slug.
	T* create(const std::string& modelSlug = "") {
		T* m = factory ? factory() : Test::createModule<T>(modelSlug);
		modules.push_back(m);
		return m;
	}

	// Hands ownership of an already-created module to the scaffold.
	T* adopt(T* m) {
		modules.push_back(m);
		return m;
	}

	~ModuleScaffold() {
		for (auto it = modules.rbegin(); it != modules.rend(); ++it) {
			Test::destroyModule(*it);
		}
	}

	// Non-copyable: two scaffolds owning the same module would double-free it.
	ModuleScaffold(const ModuleScaffold&) = delete;
	ModuleScaffold& operator=(const ModuleScaffold&) = delete;
};

// Creates a ModuleWidget and adds it to the engine
template <typename T>
inline T* createWidget(Module* m) {
	T* mw = dynamic_cast<T*>(m->model->createModuleWidget(m));
	return mw;
}

// Creates a ModuleWidget, without a module, the same way as the module browser
template <typename T>
inline T* createWidget(std::string modelSlug) {
	Model* m = pluginInstance->getModel(modelSlug);
	T* mw = dynamic_cast<T*>(m->createModuleWidget(NULL));
	return mw;
}

inline void destroyWidget(rack::ModuleWidget* mw) {
	APP->event->finalizeWidget(mw);
	mw->module = NULL;
	delete mw;
}

inline void registerModule(rack::Module* m, rack::ModuleWidget* mw = nullptr) {
	TEST_SUPPRESS_DEPRECATED_BEGIN
	APP->engine->addModule_NoLock(m);
	TEST_SUPPRESS_DEPRECATED_END
	if (mw) {
		APP->scene->rack->addModule(mw);
	}
}

inline void unregisterModule(rack::Module* m, rack::ModuleWidget* mw = nullptr) {
	if (mw) {
		APP->scene->rack->removeModule(mw);
		TEST_SUPPRESS_DEPRECATED_BEGIN
		APP->engine->removeModule_NoLock(m);
		TEST_SUPPRESS_DEPRECATED_END
		// Delegates to destroyWidget() for teardown so this path also calls
		// APP->event->finalizeWidget(mw) — otherwise a widget that was hovered, dragged or
		// selected leaves a dangling pointer behind in EventState.
		destroyWidget(mw);
	}
	else {
		TEST_SUPPRESS_DEPRECATED_BEGIN
		APP->engine->removeModule_NoLock(m);
		TEST_SUPPRESS_DEPRECATED_END
	}
}

// Defaults to Test::sampleRate() (the same source Test::createModule() uses for its
// onSampleRateChange event) rather than a hardcoded value, so a module is always stepped at the
// rate it was configured for — even in tests that change the engine's sample rate.
inline const Module::ProcessArgs makeProcessArgs(int64_t frame, float sampleRate = Test::sampleRate()) {
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
inline const rack::midi::Message makeMidiMessage(uint8_t statusNibble, uint8_t channel, uint8_t b1, uint8_t b2, int64_t frame = 0) {
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
// testEngine.addModules(moduleA, moduleB);
// A -> B chain
//
// testEngine.step();  // Process both modules with message flipping
// testEngine.step();  // Continue processing...
//
// Named addModule(s), not registerModule(s), to stay distinct from Test::registerModule() —
// that one registers a module with Rack's real engine (APP->engine->addModule_NoLock); this one
// only appends to SimpleEngine's own std::list. Same word, unrelated operations (see B2 in the
// framework review).
struct SimpleEngine {
	std::list<Module*> modules;
	int frame = 0;

	void step() {
		auto args = Test::makeProcessArgs(frame);
		for (Module* module : modules) {
			module->process(args);
			if (module->leftExpander.messageFlipRequested) {
				std::swap(module->leftExpander.producerMessage, module->leftExpander.consumerMessage);
				module->leftExpander.messageFlipRequested = false;
			}
			if (module->rightExpander.messageFlipRequested) {
				std::swap(module->rightExpander.producerMessage, module->rightExpander.consumerMessage);
				module->rightExpander.messageFlipRequested = false;
			}
		}
		frame++;
 	}

	void addModule(Module* m) {
		modules.push_back(m);
	}

	/// Add multiple modules at once.
	template <typename... T>
	void addModules(T*... _m) {
		Module* arr[] = {_m...};
		for (Module* m : arr) {
			this->modules.push_back(m);
		}
	}
};


} // namespace Test