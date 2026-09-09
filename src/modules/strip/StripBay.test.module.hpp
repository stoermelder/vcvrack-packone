// STRIPBAY test cases. Included by StripBay.test.cpp inside namespace __module.
// Not a standalone header: StripBay.test.hpp supplies everything these cases use.

TEST_CASE("Construction and initialization", "[StripBay]") {
	Test::ModuleScaffold<StripBayModule<4>> mods;
	StripBayModule<4>* m = mods.create("StripBay4");
	StripBay4Widget* mw = Test::createWidget<StripBay4Widget>("StripBay4");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
}

TEST_CASE("Preset JSON null-guards", "[StripBay][JSON]") {
	Test::ModuleScaffold<StripBayModule<4>> mods;
	auto module = mods.create("StripBay4");

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
