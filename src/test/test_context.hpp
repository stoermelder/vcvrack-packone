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
		// Thread assertions (see utils/thread.hpp) verify against Rack's UI thread and a
		// module's own GuiTaskProcessor worker, neither of which runs in a test binary —
		// the tests drive module methods from their own thread. Each module instance
		// builds its own ThreadVerifier via thread::makeVerifier() in its constructor,
		// which reads this flag, so setting it here — before Test::createModule() ever
		// constructs a module — is enough to make every module's asserts inert for the
		// whole test binary.
		StoermelderPackOne::thread::verifyEnabled = false;

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
	APP->event->finalizeWidget(mw);
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



// Renders a JSON path as a dotted string for CATCH_INFO annotations
// (e.g. {"a", "b", "c"} -> "a.b.c"). Array indices are encoded as their
// decimal representation.
static std::string formatJsonPath(const std::vector<std::string>& path) {
	std::string s;
	for (size_t i = 0; i < path.size(); i++) {
		if (i > 0) s += ".";
		s += path[i];
	}
	return s;
}

// Resolves a path of keys/indices (array indices encoded as decimal strings)
// to its node within a JSON document. An empty path resolves to the root
// itself. Returns NULL if any step is missing or passes through a scalar -
// callers treat this as a no-op rather than an error.
static json_t* resolveJsonPath(json_t* root, const std::vector<std::string>& path) {
	json_t* current = root;
	for (const auto& step : path) {
		if (json_is_array(current))
			current = json_array_get(current, (size_t)std::stoul(step));
		else if (json_is_object(current))
			current = json_object_get(current, step.c_str());
		else
			return nullptr;
		if (!current) return nullptr;
	}
	return current;
}

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
		CATCH_INFO("Property '" << formatJsonPath(path) << "' should be null-guarded in dataFromJson()");

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


// Verifies that every value (at any nesting depth, including inside arrays)
// tolerates being replaced with a value of a deliberately WRONG type without
// crashing the module's dataFromJson() implementation.
//
// Unlike testPresetNullGuards(), arrays ARE descended into: wrong-typed values
// inside array elements are exactly the shape of real-world patch corruption
// (e.g. "label": 42 where a string is expected). For every node except the
// root itself:
//   1. A deep copy of the JSON is created.
//   2. The value at that path is replaced with a wrong-typed value:
//        - strings become integers, catching unguarded json_string_value()
//          which returns NULL for non-strings (UB when assigned to
//          std::string);
//        - everything else becomes a string, catching iteration over
//          non-containers and type-punned reads.
//   3. The copy is loaded via the module's dataFromJson().
//
// Usage: same as testPresetNullGuards().
template <typename T>
static void testPresetTypeConfusion(T* module, json_t* rootJ) {
	REQUIRE(module != nullptr);
	REQUIRE(rootJ != nullptr);
	REQUIRE(json_is_object(rootJ));

	// Recursive collector for the path and type of every node except the root.
	struct NodeVisit {
		std::vector<std::string> path;
		json_type type;
	};
	std::vector<NodeVisit> visits;
	std::vector<std::string> currentPath;
	std::function<void(json_t*)> collectPaths = [&](json_t* node) {
		if (json_is_object(node)) {
			const char* key;
			json_t* value;
			json_object_foreach(node, key, value) {
				currentPath.push_back(key);
				visits.push_back({currentPath, json_typeof(value)});
				collectPaths(value);
				currentPath.pop_back();
			}
		}
		else if (json_is_array(node)) {
			for (size_t i = 0; i < json_array_size(node); i++) {
				json_t* value = json_array_get(node, i);
				currentPath.push_back(std::to_string(i));
				visits.push_back({currentPath, json_typeof(value)});
				collectPaths(value);
				currentPath.pop_back();
			}
		}
	};
	collectPaths(rootJ);

	// Returns a replacement of a deliberately wrong type for a node of the
	// given JSON type.
	auto makeWrongType = [](json_type type) -> json_t* {
		switch (type) {
			case JSON_STRING: return json_integer(-42);
			default: return json_string("wrong-type");
		}
	};

	for (const auto& visit : visits) {
		CATCH_INFO("Property '" << formatJsonPath(visit.path) << "' should tolerate a wrong-typed value in dataFromJson()");

		json_t* copyJ = json_deep_copy(rootJ);
		REQUIRE(copyJ != nullptr);

		// Replace the value within its parent container; NULL cannot happen on
		// a fresh deep copy but is treated as a no-op rather than an error.
		json_t* parent = resolveJsonPath(copyJ, std::vector<std::string>(visit.path.begin(), visit.path.end() - 1));
		if (parent) {
			const std::string& last = visit.path.back();
			json_t* replacement = makeWrongType(visit.type);
			if (json_is_array(parent))
				json_array_set_new(parent, (size_t)std::stoul(last), replacement);
			else
				json_object_set_new(parent, last.c_str(), replacement);
		}

		REQUIRE_NOTHROW(module->dataFromJson(copyJ));
		json_decref(copyJ);
	}
}


// Verifies that array-valued properties tolerate being much longer than any
// fixed-size destination ([SETS], [SNAPSHOTS], ...) in the module's
// dataFromJson() implementation.
//
// For every array in the JSON tree (at any nesting depth):
//   1. A deep copy of the JSON is created.
//   2. The array is grown well past any plausible fixed-size destination by
//      duplicating its first element, so all values stay well-typed.
//   3. The copy is loaded via the module's dataFromJson().
//
// Catches unbounded loops like
//     for (size_t s = 0; s < json_array_size(setsJ); ++s)
//         snapshots[s][i].id = ...;   // snapshots[] is a fixed [SETS] member
// which write past the end of fixed-size members when loading hand-edited or
// corrupted patches.
//
// Usage: same as testPresetNullGuards().
template <typename T>
static void testPresetOversizedArrays(T* module, json_t* rootJ) {
	REQUIRE(module != nullptr);
	REQUIRE(rootJ != nullptr);
	REQUIRE(json_is_object(rootJ));

	// Collect the path of every array node except the root itself, descending
	// into both objects and arrays.
	std::vector<std::vector<std::string>> paths;
	std::vector<std::string> currentPath;
	std::function<void(json_t*)> collectArrays = [&](json_t* node) {
		if (json_is_object(node)) {
			const char* key;
			json_t* value;
			json_object_foreach(node, key, value) {
				currentPath.push_back(key);
				if (json_is_array(value)) paths.push_back(currentPath);
				collectArrays(value);
				currentPath.pop_back();
			}
		}
		else if (json_is_array(node)) {
			for (size_t i = 0; i < json_array_size(node); i++) {
				json_t* value = json_array_get(node, i);
				currentPath.push_back(std::to_string(i));
				if (json_is_array(value)) paths.push_back(currentPath);
				collectArrays(value);
				currentPath.pop_back();
			}
		}
	};
	collectArrays(rootJ);

	// Number of elements appended past the original length - comfortably more
	// than every fixed-size destination in the codebase.
	const size_t oversizeBy = 64;

	for (const auto& path : paths) {
		CATCH_INFO("Array '" << formatJsonPath(path) << "' should tolerate being oversized in dataFromJson()");

		json_t* copyJ = json_deep_copy(rootJ);
		REQUIRE(copyJ != nullptr);

		// Grow the array by duplicating its first element; an empty array has
		// no element to duplicate and is left untouched.
		json_t* arr = resolveJsonPath(copyJ, path);
		if (json_is_array(arr) && json_array_size(arr) > 0) {
			json_t* firstJ = json_array_get(arr, 0);
			size_t target = json_array_size(arr) + oversizeBy;
			while (json_array_size(arr) < target) {
				if (json_array_append(arr, firstJ) != 0) break;
			}
		}

		REQUIRE_NOTHROW(module->dataFromJson(copyJ));
		json_decref(copyJ);
	}
}


} // namespace Test