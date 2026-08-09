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
			auto it = std::find(rack::plugin::plugins.begin(), rack::plugin::plugins.end(), pluginInstance);
			rack::plugin::plugins.erase(it);
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
	Module::RemoveEvent eRemove;
	m->onRemove(eRemove);
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

	void stepBlock(int n) {
		for (int i = 0; i < n; i++) step();
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
			auto it = std::find(this->modules.begin(), this->modules.end(), m);
			assert(it == this->modules.end());
			// Set ID if unset or collides with an existing ID
			if (m->id < 0) {
				// Randomly generate ID
				m->id = random::u64() % (1ull << 53);
			}
			this->modules.push_back(m);
		}
	}
};



// Verifies that every property (at any nesting depth) in a preset JSON
// is properly null-guarded in the module's dataFromJson() implementation.
//
// For every path in the JSON object tree:
//   1. A deep copy of the JSON is created.
//   2. The value at that path is replaced with `json_null()`.
//   3. The copy is loaded via the module's dataFromJson().
//
// The helper also verifies that an empty JSON object (no keys) can be
// loaded without crashing.
//
// If dataFromJson() does not properly null-guard a property, this test
// will crash (e.g. on `json_array_foreach(json_null(), ...)`) or throw.
// Common bugs caught by this helper include:
//   - Dereferencing a value without checking that the key exists:
//         json_t* fooJ = json_object_get(rootJ, "foo");
//         foo = json_integer_value(fooJ);  // UB if "foo" is missing/null
//   - Iterating over a value that is not an array:
//         json_t* dataJ = json_object_get(rootJ, "data");
//         if (dataJ) {  // true even if dataJ is json_null
//             json_array_foreach(dataJ, ...)  // UB
//         }
//   - Reading a nested object property without checking the parent:
//         json_t* settingsJ = json_object_get(rootJ, "settings");
//         json_t* colorJ = json_object_get(settingsJ, "color");
//         color = json_string_value(colorJ);  // CRASH if settingsJ is null
//
// Usage:
//   auto module = Test::createModule<MyModule>("MySlug");
//   json_t* rootJ = module->dataToJson();
//   REQUIRE(rootJ != nullptr);
//   Test::testPresetNullGuards(module, rootJ);
//   json_decref(rootJ);
//   Test::destroyModule(module);
template <typename T>
static void testPresetNullGuards(T* module, json_t* rootJ) {
	REQUIRE(module != nullptr);
	REQUIRE(rootJ != nullptr);
	REQUIRE(json_is_object(rootJ));

	// Recursive path collector. std::function is required because the
	// lambda captures itself for recursion (C++11 has no generic
	// lambdas with `auto` parameters). Arrays are not descended into -
	// we only test the array as a whole.
	std::vector<std::vector<std::string>> paths;
	std::vector<std::string> currentPath;
	std::function<void(json_t*)> collectPaths = [&](json_t* node) {
		if (!json_is_object(node)) return;
		const char* key;
		json_t* value;
		json_object_foreach(node, key, value) {
			currentPath.push_back(key);
			paths.push_back(currentPath);
			collectPaths(value);
			currentPath.pop_back();
		}
	};
	collectPaths(rootJ);

	// Render a path as a dotted string for the CATCH_INFO annotation
	// (e.g. {"a", "b", "c"} -> "a.b.c").
	auto formatPath = [](const std::vector<std::string>& path) -> std::string {
		std::string s;
		for (size_t i = 0; i < path.size(); i++) {
			if (i > 0) s += ".";
			s += path[i];
		}
		return s;
	};

	// Walk the path in a (deep-copied) JSON object and replace the
	// leaf value with json_null(). Intermediate objects that are not
	// themselves objects (e.g. null due to a previous iteration) are
	// left untouched - this is a no-op rather than an error, because
	// the helper should not change behaviour between iterations.
	auto setPathToNull = [](json_t* obj, const std::vector<std::string>& path) {
		json_t* current = obj;
		for (size_t i = 0; i + 1 < path.size(); i++) {
			json_t* next = json_object_get(current, path[i].c_str());
			if (!json_is_object(next)) return;
			current = next;
		}
		json_object_set_new(current, path.back().c_str(), json_null());
	};

	// For each path, set the value to null and test the loader.
	for (const auto& path : paths) {
		CATCH_INFO("Property '" << formatPath(path) << "' should be null-guarded in dataFromJson()");

		json_t* copyJ = json_deep_copy(rootJ);
		REQUIRE(copyJ != nullptr);
		setPathToNull(copyJ, path);

		REQUIRE_NOTHROW(module->dataFromJson(copyJ));
		json_decref(copyJ);
	}

	// An empty JSON object should also be safe to load.
	CATCH_INFO("Empty JSON object should load without crashing");
	json_t* emptyJ = json_object();
	REQUIRE_NOTHROW(module->dataFromJson(emptyJ));
	json_decref(emptyJ);
}


} // namespace Test