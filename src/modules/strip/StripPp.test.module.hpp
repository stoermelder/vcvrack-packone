// STRIPPP test cases. Included by StripPp.test.cpp inside namespace __module.
// Not a standalone header: StripPp.test.hpp supplies everything these cases use.

TEST_CASE("Construction and initialization", "[StripPp]") {
	Test::ModuleScaffold<StripPpModule> mods;
	StripPpModule* m = mods.create("StripPp");
	StripPpWidget* mw = Test::createWidget<StripPpWidget>("StripPp");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
}

TEST_CASE("Preset JSON null-guards", "[StripPp][JSON]") {
	Test::ModuleScaffold<StripPpModule> mods;
	auto module = mods.create("StripPp");

	SECTION("All top-level properties are null-guarded in dataFromJson()") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetNullGuards(module, rootJ);
		json_decref(rootJ);
	}

	SECTION("All properties tolerate wrong-typed values") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetTypeConfusion(module, rootJ);
		json_decref(rootJ);
	}

	SECTION("All arrays tolerate being oversized") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetOversizedArrays(module, rootJ);
		json_decref(rootJ);
	}
}