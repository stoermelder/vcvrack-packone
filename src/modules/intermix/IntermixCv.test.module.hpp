// INTERMIXCV test cases. Included by IntermixCv.test.cpp inside namespace __module.
// Not a standalone header: IntermixCv.test.hpp supplies everything these cases use.

TEST_CASE("Construction and initialization", "[IntermixCv]") {
	Test::ModuleScaffold<IntermixCvModule<8>> mods;
	IntermixCvModule<8>* m = mods.create("IntermixCv");
	IntermixCvWidget* mw = Test::createWidget<IntermixCvWidget>("IntermixCv");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
}

TEST_CASE("Preset JSON null-guards", "[IntermixCv][JSON]") {
	Test::ModuleScaffold<IntermixCvModule<8>> mods;
	auto module = mods.create("IntermixCv");

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
}

TEST_CASE("JSON round-trip preserves state", "[IntermixCv]") {
	Test::ModuleScaffold<IntermixCvModule<8>> mods;
	IntermixCvModule<8>* m = mods.create("IntermixCv");
	IntermixCvModule<8>* m2 = mods.create("IntermixCv");

	m->panelTheme = 1;
	m->input = 5;

	json_t* j = m->dataToJson();
	m2->panelTheme = 0;
	m2->input = 0;
	m2->dataFromJson(j);
	json_decref(j);

	REQUIRE(m2->panelTheme == 1);
	REQUIRE(m2->input == 5);
}

TEST_CASE("Preset JSON clamps out-of-range input", "[IntermixCv][JSON]") {
	Test::ModuleScaffold<IntermixCvModule<8>> mods;
	auto module = mods.create("IntermixCv");

	json_t* rootJ = module->dataToJson();
	json_object_set_new(rootJ, "input", json_integer(4000));
	module->dataFromJson(rootJ);
	json_decref(rootJ);

	REQUIRE(module->input >= 0);
	REQUIRE(module->input < 8);
}

TEST_CASE("Reset behavior", "[IntermixCv]") {
	Test::ModuleScaffold<IntermixCvModule<8>> mods;
	auto module = mods.create("IntermixCv");

	module->input = 5;

	rack::engine::Module::ResetEvent re;
	module->onReset(re);

	REQUIRE(module->input == 0);
}

TEST_CASE("getInput, isConnected and getValue", "[IntermixCv]") {
	Test::Harness h;
	auto module = h.addModule<IntermixCvModule<8>>("IntermixCv");

	SECTION("getInput reflects the selected row") {
		module->input = 3;
		REQUIRE(module->getInput() == 3);
	}

	SECTION("Disconnected input reports not connected") {
		REQUIRE_FALSE(module->isConnected(0));
	}

	SECTION("Connected input reports connected and maps voltage to a pad value") {
		module->inputs[IntermixCvModule<8>::INPUT_CV + 3].channels = 1;
		module->inputs[IntermixCvModule<8>::INPUT_CV + 3].setVoltage(0.f);
		REQUIRE(module->isConnected(3));
		REQUIRE(module->getValue(3) == Catch::Approx(0.f).margin(0.001f));

		module->inputs[IntermixCvModule<8>::INPUT_CV + 3].setVoltage(10.f);
		REQUIRE(module->getValue(3) == Catch::Approx(1.f).margin(0.001f));

		module->inputs[IntermixCvModule<8>::INPUT_CV + 3].setVoltage(5.f);
		REQUIRE(module->getValue(3) == Catch::Approx(0.5f).margin(0.001f));
	}

	SECTION("Value clamps to 0..1 outside 0..10V") {
		module->inputs[IntermixCvModule<8>::INPUT_CV + 0].channels = 1;
		module->inputs[IntermixCvModule<8>::INPUT_CV + 0].setVoltage(50.f);
		REQUIRE(module->getValue(0) == Catch::Approx(1.f).margin(0.001f));

		module->inputs[IntermixCvModule<8>::INPUT_CV + 0].setVoltage(-50.f);
		REQUIRE(module->getValue(0) == Catch::Approx(0.f).margin(0.001f));
	}

	SECTION("Columns are independent") {
		module->inputs[IntermixCvModule<8>::INPUT_CV + 0].channels = 1;
		module->inputs[IntermixCvModule<8>::INPUT_CV + 0].setVoltage(10.f);

		REQUIRE(module->isConnected(0));
		REQUIRE_FALSE(module->isConnected(1));
	}
}
