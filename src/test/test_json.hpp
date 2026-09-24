#pragma once
#include "test_plugin.hpp"
#include "test_harness.hpp"
#include <vector>
#include <string>
#include <functional>
#include <climits>

namespace Test {

// Renders a JSON path as a dotted string for CATCH_INFO annotations
// (e.g. {"a", "b", "c"} -> "a.b.c"). Array indices are encoded as their
// decimal representation.
inline std::string formatJsonPath(const std::vector<std::string>& path) {
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
inline json_t* resolveJsonPath(json_t* root, const std::vector<std::string>& path) {
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
inline void testPresetNullGuards(T* module, json_t* rootJ) {
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
		std::string pathStr = formatJsonPath(path);
		CATCH_INFO("Property '" << pathStr << "' should be null-guarded in dataFromJson()");

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
inline void testPresetTypeConfusion(T* module, json_t* rootJ) {
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
		std::string pathStr = formatJsonPath(visit.path);
		CATCH_INFO("Property '" << pathStr << "' should tolerate a wrong-typed value in dataFromJson()");

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
inline void testPresetOversizedArrays(T* module, json_t* rootJ) {
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
		std::string pathStr = formatJsonPath(path);
		CATCH_INFO("Array '" << pathStr << "' should tolerate being oversized in dataFromJson()");

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


// Verifies that every integer-valued scalar (at any nesting depth, including
// inside arrays) tolerates being replaced with an out-of-range value without
// crashing the module - neither in dataFromJson() itself nor a few process()
// calls later.
//
// This targets a defect shape the other three helpers structurally cannot see:
// an integer read straight from JSON into a member that is later used as an
// array index, with no clamp. testPresetTypeConfusion() looks like it should
// catch this, but jansson's json_integer_value() returns 0 for a wrong-typed
// node, and 0 is in range for most such fields - the perturbation is silently
// benign for exactly the fields at risk. testPresetOversizedArrays() only
// touches arrays, not scalars.
//
// For every integer leaf in the tree:
//   1. A deep copy of the JSON is created.
//   2. The leaf is replaced with each of -1, INT_MAX, and a large positive
//      value (4000), in turn.
//   3. The copy is loaded via the module's dataFromJson().
//   4. The harness steps the engine a few hundred samples.
//
// Step 4 is not decoration: an unclamped index used only inside process()
// (not in dataFromJson() itself) will not fault until the engine actually
// runs - a helper that stopped at dataFromJson() would silently miss it.
//
// Common bugs caught by this helper:
//     json_t* selectedJ = json_object_get(rootJ, "selected");
//     if (selectedJ) selected = json_integer_value(selectedJ);   // unclamped
//     ...
//     float v = scenes[selected].matrix[i][j];                  // OOB read/write
//
// Usage:
//   Test::Harness h;
//   auto module = h.addModule<MyModule>("MySlug");
//   json_t* rootJ = module->dataToJson();
//   REQUIRE(rootJ != nullptr);
//   Test::testPresetOutOfRangeScalars(h, module, rootJ);
//   json_decref(rootJ);
template <typename T>
inline void testPresetOutOfRangeScalars(Harness& h, T* module, json_t* rootJ) {
	REQUIRE(module != nullptr);
	REQUIRE(rootJ != nullptr);
	REQUIRE(json_is_object(rootJ));

	// Recursive collector for the path of every integer-valued leaf, descending
	// into both objects and arrays. Booleans are excluded (JSON_TRUE/JSON_FALSE
	// are a distinct json_type from JSON_INTEGER) since they are not used as
	// indices.
	std::vector<std::vector<std::string>> paths;
	std::vector<std::string> currentPath;
	std::function<void(json_t*)> collectIntegers = [&](json_t* node) {
		if (json_is_object(node)) {
			const char* key;
			json_t* value;
			json_object_foreach(node, key, value) {
				currentPath.push_back(key);
				if (json_is_integer(value)) paths.push_back(currentPath);
				collectIntegers(value);
				currentPath.pop_back();
			}
		}
		else if (json_is_array(node)) {
			for (size_t i = 0; i < json_array_size(node); i++) {
				json_t* value = json_array_get(node, i);
				currentPath.push_back(std::to_string(i));
				if (json_is_integer(value)) paths.push_back(currentPath);
				collectIntegers(value);
				currentPath.pop_back();
			}
		}
	};
	collectIntegers(rootJ);

	// Number of engine steps run after each load - enough to clear a
	// clock-divider gate (the largest in this codebase divides by 64) plus
	// its random initial phase.
	const int64_t dspStepsAfterLoad = 256;
	const json_int_t candidates[] = {-1, (json_int_t)INT_MAX, 4000};

	for (const auto& path : paths) {
		std::string pathStr = formatJsonPath(path);

		for (json_int_t candidate : candidates) {
			CATCH_INFO("Property '" << pathStr << "' should clamp an out-of-range value (" << candidate << ") in dataFromJson()");

			json_t* copyJ = json_deep_copy(rootJ);
			REQUIRE(copyJ != nullptr);

			json_t* parent = resolveJsonPath(copyJ, std::vector<std::string>(path.begin(), path.end() - 1));
			if (parent) {
				const std::string& last = path.back();
				json_t* replacement = json_integer(candidate);
				if (json_is_array(parent))
					json_array_set_new(parent, (size_t)std::stoul(last), replacement);
				else
					json_object_set_new(parent, last.c_str(), replacement);
			}

			REQUIRE_NOTHROW(module->dataFromJson(copyJ));
			json_decref(copyJ);

			// The fault is often in process(), not the loader - step the engine
			// so an unclamped index used only there gets exercised too.
			REQUIRE_NOTHROW(h.dspSteps(dspStepsAfterLoad));
		}
	}
}

} // namespace Test
