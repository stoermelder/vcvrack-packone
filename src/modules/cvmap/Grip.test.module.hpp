// GRIP test cases. Included by Grip.test.cpp inside namespace __module.
// Not a standalone header: Grip.test.hpp supplies everything these cases use.

TEST_CASE("Construction and initialization", "[Grip]") {
	Test::ModuleScaffold<GripModule> mods;
	GripModule* m = mods.create("Grip");
	GripWidget* mw = Test::createWidget<GripWidget>("Grip");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
}

TEST_CASE("Preset JSON null-guards", "[Grip][JSON]") {
	Test::ModuleScaffold<GripModule> mods;
	auto module = mods.create("Grip");

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