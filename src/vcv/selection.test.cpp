#include "../test/test_plugin.hpp"
#include "selection.hpp"

using namespace StoermelderPackOne;
using namespace StoermelderPackOne::vcv;
using Catch::Approx;

// ---- helpers ----------------------------------------------------------------

// Builds a module JSON object {plugin, model, id, pos}. json_pack is unavailable in this
// jansson build, so construct manually.
static json_t* moduleJson(const char* plugin, const char* model, int64_t id, double x = 0.0, double y = 0.0) {
	json_t* j = json_object();
	json_object_set_new(j, "plugin", json_string(plugin));
	json_object_set_new(j, "model", json_string(model));
	json_object_set_new(j, "id", json_integer(id));
	json_t* pos = json_array();
	json_array_append_new(pos, json_real(x));
	json_array_append_new(pos, json_real(y));
	json_object_set_new(j, "pos", pos);
	return j;
}

// Fixed slug→width table (px) for the WidthLookup tests; unknown models → 0.
static float widthOf(const ModuleRef& ref) {
	static const std::map<std::string, float> widths = {{"M1", 60.f}, {"M2", 30.f}, {"A", 30.f}, {"B", 30.f}};
	auto it = widths.find(ref.modelSlug);
	return it != widths.end() ? it->second : 0.f;
}

// modelExists stub for findUnavailableModules: only "KNOWN" exists.
static bool modelExists(const ModuleRef& ref) {
	return ref.modelSlug == "KNOWN";
}

// ---- readModuleRef ----------------------------------------------------------

TEST_CASE("readModuleRef reads plugin/model strings", "[selection]") {
	json_t* j = moduleJson("Core", "VCO", 1);
	ModuleRef ref;
	REQUIRE(readModuleRef(j, ref));
	CHECK(ref.pluginSlug == "Core");
	CHECK(ref.modelSlug == "VCO");
	json_decref(j);
}

TEST_CASE("readModuleRef rejects missing or non-string plugin/model", "[selection]") {
	ModuleRef ref;

	json_t* j = json_object();
	json_object_set_new(j, "model", json_string("VCO"));
	CHECK_FALSE(readModuleRef(j, ref));
	json_decref(j);

	j = json_object();
	json_object_set_new(j, "plugin", json_string("Core"));
	CHECK_FALSE(readModuleRef(j, ref));
	json_decref(j);

	j = json_object();
	json_object_set_new(j, "plugin", json_integer(42));
	json_object_set_new(j, "model", json_string("VCO"));
	CHECK_FALSE(readModuleRef(j, ref));
	json_decref(j);
}

// ---- readModuleId / readModulePos --------------------------------------------

TEST_CASE("readModuleId returns the id or -1", "[selection]") {
	json_t* j = moduleJson("Core", "VCO", 7);
	CHECK(readModuleId(j) == 7);
	json_decref(j);

	j = json_object();
	json_object_set_new(j, "plugin", json_string("Core"));
	CHECK(readModuleId(j) == -1);
	json_decref(j);
}

TEST_CASE("readModulePos reads grid units, false when absent", "[selection]") {
	json_t* j = moduleJson("Core", "VCO", 1, 3.0, 4.0);
	Vec pos;
	REQUIRE(readModulePos(j, pos));
	CHECK(pos.x == 3.f);
	CHECK(pos.y == 4.f);
	json_decref(j);

	j = json_object();
	json_object_set_new(j, "plugin", json_string("Core"));
	pos = Vec(9.f, 9.f);
	CHECK_FALSE(readModulePos(j, pos));
	CHECK(pos.x == 0.f);
	CHECK(pos.y == 0.f);
	json_decref(j);
}

// ---- layoutSelection ----------------------------------------------------------

TEST_CASE("layoutSelection normalizes to the array's top-left and offsets by origin", "[selection]") {
	// grid (5,6) and (1,2); RACK_GRID_SIZE = (15, 380); origin (100,200)
	json_t* modulesJ = json_array();
	json_array_append_new(modulesJ, moduleJson("A", "M1", 1, 5.0, 6.0));
	json_array_append_new(modulesJ, moduleJson("B", "M2", 2, 1.0, 2.0));

	auto placements = layoutSelection(modulesJ, Vec(100, 200));
	REQUIRE(placements.size() == 2);

	// (1,2) is the min → lands exactly on the origin
	CHECK(placements[0].oldId == 1);
	CHECK(placements[0].pos.x == Approx(160.f));   // (5-1)*15 + 100
	CHECK(placements[0].pos.y == Approx(1720.f));  // (6-2)*380 + 200
	CHECK(placements[1].oldId == 2);
	CHECK(placements[1].pos.x == Approx(100.f));
	CHECK(placements[1].pos.y == Approx(200.f));

	json_decref(modulesJ);
}

TEST_CASE("layoutSelection returns empty for no array", "[selection]") {
	json_t* modulesJ = json_object();  // not an array
	CHECK(layoutSelection(modulesJ, Vec()).empty());
	json_decref(modulesJ);

	CHECK(layoutSelection(nullptr, Vec()).empty());
}

// ---- layoutStrip ---------------------------------------------------------------

TEST_CASE("layoutStrip places rightModules rightward of the anchor", "[selection]") {
	json_t* rootJ = json_object();
	json_t* rightJ = json_array();
	json_array_append_new(rightJ, moduleJson("A", "M1", 1));  // w=60
	json_array_append_new(rightJ, moduleJson("B", "M2", 2));  // w=30
	json_object_set_new(rootJ, "rightModules", rightJ);

	auto placements = layoutStrip(rootJ, Rect(Vec(10, 20), Vec(30, 100)), StripSide::RIGHT, widthOf);
	REQUIRE(placements.size() == 2);
	CHECK(placements[0].pos.x == Approx(40.f));  // 10 + 30
	CHECK(placements[0].pos.y == Approx(20.f));
	CHECK(placements[1].pos.x == Approx(100.f)); // 40 + 60
	CHECK(placements[1].pos.y == Approx(20.f));

	json_decref(rootJ);
}

TEST_CASE("layoutStrip places leftModules leftward of the anchor", "[selection]") {
	json_t* rootJ = json_object();
	json_t* leftJ = json_array();
	json_array_append_new(leftJ, moduleJson("A", "M1", 1));  // w=60
	json_array_append_new(leftJ, moduleJson("B", "M2", 2));  // w=30
	json_object_set_new(rootJ, "leftModules", leftJ);

	auto placements = layoutStrip(rootJ, Rect(Vec(10, 20), Vec(30, 100)), StripSide::LEFT, widthOf);
	REQUIRE(placements.size() == 2);
	CHECK(placements[0].pos.x == Approx(-50.f)); // 10 - 60
	CHECK(placements[0].pos.y == Approx(20.f));
	CHECK(placements[1].pos.x == Approx(-80.f)); // -50 - 30

	json_decref(rootJ);
}

TEST_CASE("layoutStrip returns empty when the side key is absent", "[selection]") {
	json_t* rootJ = json_object();
	CHECK(layoutStrip(rootJ, Rect(), StripSide::RIGHT, widthOf).empty());
	CHECK(layoutStrip(rootJ, Rect(), StripSide::LEFT, widthOf).empty());
	json_decref(rootJ);
}

TEST_CASE("layoutStrip treats unknown models as zero width", "[selection]") {
	json_t* rootJ = json_object();
	json_t* rightJ = json_array();
	json_array_append_new(rightJ, moduleJson("X", "UNKNOWN", 1));  // width 0
	json_array_append_new(rightJ, moduleJson("A", "M2", 2));       // w=30
	json_object_set_new(rootJ, "rightModules", rightJ);

	auto placements = layoutStrip(rootJ, Rect(Vec(0, 0), Vec(0, 0)), StripSide::RIGHT, widthOf);
	REQUIRE(placements.size() == 2);
	CHECK(placements[0].pos.x == Approx(0.f));   // unknown → 0 width, no advance
	CHECK(placements[1].pos.x == Approx(0.f));   // 0 + 0, still at 0

	json_decref(rootJ);
}

// ---- convertVcvssToVcvs ---------------------------------------------------------

TEST_CASE("convertVcvssToVcvs combines left and right modules", "[selection]") {
	// left stored right-to-left: first element is the rightmost; emit left-to-right
	json_t* vcvssJ = json_object();
	json_t* leftJ = json_array();
	json_array_append_new(leftJ, moduleJson("A", "A", 1));  // w=30 → 2 grid units
	json_array_append_new(leftJ, moduleJson("B", "B", 2));  // w=30 → 2 grid units
	json_object_set_new(vcvssJ, "leftModules", leftJ);
	json_t* rightJ = json_array();
	json_array_append_new(rightJ, moduleJson("A", "M2", 3));  // w=30 → 2 grid units
	json_object_set_new(vcvssJ, "rightModules", rightJ);

	json_t* vcvsJ = convertVcvssToVcvs(vcvssJ, widthOf);
	REQUIRE(vcvsJ);

	json_t* modulesJ = json_object_get(vcvsJ, "modules");
	REQUIRE(json_array_size(modulesJ) == 3);

	// left emitted right-to-left reversed: B at 0, A at 2, then right M2 at 4
	json_t* m0 = json_array_get(modulesJ, 0);
	json_t* m1 = json_array_get(modulesJ, 1);
	json_t* m2 = json_array_get(modulesJ, 2);
	CHECK(strcmp(json_string_value(json_object_get(m0, "model")), "B") == 0);
	CHECK(json_real_value(json_array_get(json_object_get(m0, "pos"), 0)) == Approx(0.0));
	CHECK(strcmp(json_string_value(json_object_get(m1, "model")), "A") == 0);
	CHECK(json_real_value(json_array_get(json_object_get(m1, "pos"), 0)) == Approx(2.0));
	CHECK(strcmp(json_string_value(json_object_get(m2, "model")), "M2") == 0);
	CHECK(json_real_value(json_array_get(json_object_get(m2, "pos"), 0)) == Approx(4.0));

	json_decref(vcvsJ);
	json_decref(vcvssJ);
}

TEST_CASE("convertVcvssToVcvs passes cables through unchanged", "[selection]") {
	json_t* vcvssJ = json_object();
	json_t* rightJ = json_array();
	json_array_append_new(rightJ, moduleJson("A", "M2", 1));
	json_object_set_new(vcvssJ, "rightModules", rightJ);
	json_t* cablesJ = json_array();
	json_array_append_new(cablesJ, json_string("cable0"));
	json_object_set_new(vcvssJ, "cables", cablesJ);

	json_t* vcvsJ = convertVcvssToVcvs(vcvssJ, widthOf);
	REQUIRE(vcvsJ);
	json_t* outCables = json_object_get(vcvsJ, "cables");
	REQUIRE(json_array_size(outCables) == 1);
	CHECK(strcmp(json_string_value(json_array_get(outCables, 0)), "cable0") == 0);

	json_decref(vcvsJ);
	json_decref(vcvssJ);
}

TEST_CASE("convertVcvssToVcvs returns nullptr for non-object input", "[selection]") {
	CHECK(convertVcvssToVcvs(nullptr, widthOf) == nullptr);
	json_t* arr = json_array();
	CHECK(convertVcvssToVcvs(arr, widthOf) == nullptr);
	json_decref(arr);
}

// ---- fixParamMappings ----------------------------------------------------------

TEST_CASE("fixParamMappings rewrites moduleId through the id map", "[selection]") {
	json_t* j = json_object();
	json_object_set_new(j, "plugin", json_string("Core"));
	json_object_set_new(j, "model", json_string("MIDI-Map"));
	json_t* maps = json_array();
	json_t* map1 = json_object();
	json_object_set_new(map1, "moduleId", json_integer(2));
	json_array_append_new(maps, map1);
	json_t* data = json_object();
	json_object_set_new(data, "maps", maps);
	json_object_set_new(j, "data", data);

	std::map<int64_t, int64_t> idMap;
	idMap[2] = 99;
	fixParamMappings(j, idMap);
	CHECK(json_integer_value(json_object_get(json_array_get(json_object_get(json_object_get(j, "data"), "maps"), 0), "moduleId")) == 99);

	json_decref(j);
}

TEST_CASE("fixParamMappings is a no-op for other modules", "[selection]") {
	json_t* j = moduleJson("Core", "VCO", 1);
	std::map<int64_t, int64_t> idMap;
	idMap[2] = 99;
	fixParamMappings(j, idMap);  // must not crash / touch anything
	CHECK(readModuleId(j) == 1);
	json_decref(j);
}

TEST_CASE("fixParamMappings sets -1 for ids missing from the map", "[selection]") {
	json_t* j = json_object();
	json_object_set_new(j, "plugin", json_string("Core"));
	json_object_set_new(j, "model", json_string("MIDI-Map"));
	json_t* maps = json_array();
	json_t* map1 = json_object();
	json_object_set_new(map1, "moduleId", json_integer(5));
	json_array_append_new(maps, map1);
	json_t* data = json_object();
	json_object_set_new(data, "maps", maps);
	json_object_set_new(j, "data", data);

	std::map<int64_t, int64_t> idMap;
	idMap[2] = 99;
	fixParamMappings(j, idMap);
	CHECK(json_integer_value(json_object_get(json_array_get(json_object_get(json_object_get(j, "data"), "maps"), 0), "moduleId")) == -1);

	json_decref(j);
}

// ---- jsonStripIds ---------------------------------------------------------------

TEST_CASE("jsonStripIds removes engine-runtime binding fields, keeps id", "[selection]") {
	json_t* j = moduleJson("Core", "VCO", 1);
	json_object_set_new(j, "leftModuleId", json_integer(2));
	json_object_set_new(j, "rightModuleId", json_integer(3));
	json_object_set_new(j, "automId", json_integer(4));

	jsonStripIds(j);
	CHECK_FALSE(json_object_get(j, "leftModuleId"));
	CHECK_FALSE(json_object_get(j, "rightModuleId"));
	CHECK_FALSE(json_object_get(j, "automId"));
	CHECK(readModuleId(j) == 1);

	json_decref(j);
}

// ---- findUnavailableModules -----------------------------------------------------

TEST_CASE("findUnavailableModules collects models that do not exist", "[selection]") {
	json_t* rootJ = json_object();
	json_t* modulesJ = json_array();
	json_array_append_new(modulesJ, moduleJson("A", "KNOWN", 1));
	json_array_append_new(modulesJ, moduleJson("X", "MISSING", 2));
	json_object_set_new(rootJ, "modules", modulesJ);

	auto slugs = findUnavailableModules(rootJ, modelExists);
	REQUIRE(slugs.size() == 1);
	CHECK(*slugs.begin() == "X/MISSING");

	json_decref(rootJ);
}

TEST_CASE("findUnavailableModules skips modules without a readable ref", "[selection]") {
	json_t* rootJ = json_object();
	json_t* modulesJ = json_array();
	json_t* bad = json_object();
	json_object_set_new(bad, "model", json_string("NO_PLUGIN"));
	json_array_append_new(modulesJ, bad);
	json_array_append_new(modulesJ, moduleJson("A", "KNOWN", 1));
	json_object_set_new(rootJ, "modules", modulesJ);

	auto slugs = findUnavailableModules(rootJ, modelExists);
	CHECK(slugs.empty());

	json_decref(rootJ);
}
