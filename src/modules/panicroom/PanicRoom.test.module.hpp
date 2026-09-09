// PANICROOM test cases. Included by PanicRoom.test.cpp inside namespace __module.
// Not a standalone header: PanicRoom.test.hpp supplies everything these cases use.

TEST_CASE("Construction and initialization", "[PanicRoom]") {
	Test::ModuleScaffold<PanicRoomModule> mods;
	PanicRoomModule* m = mods.create("PanicRoom");
	PanicRoomWidget* mw = Test::createWidget<PanicRoomWidget>("PanicRoom");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
}

TEST_CASE("Preset JSON null-guards", "[PanicRoom][JSON]") {
	Test::ModuleScaffold<PanicRoomModule> mods;
	auto module = mods.create("PanicRoom");

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


TEST_CASE("JSON round-trip preserves state", "[PanicRoom][JSON]") {
	Test::ModuleScaffold<PanicRoomModule> mods;
	PanicRoomModule* m = mods.create("PanicRoom");

	m->outsideColor = nvgRGBf(0.2f, 0.4f, 0.6f);
	m->outsideAlpha = 0.75f;
	m->restrictionEnabled = true;

	json_t* j = m->dataToJson();

	PanicRoomModule* m2 = mods.create("PanicRoom");
	m2->dataFromJson(j);
	json_decref(j);

	REQUIRE(m2->outsideColor.r == Catch::Approx(0.2f).margin(0.01));
	REQUIRE(m2->outsideColor.g == Catch::Approx(0.4f).margin(0.01));
	REQUIRE(m2->outsideColor.b == Catch::Approx(0.6f).margin(0.01));
	REQUIRE(m2->outsideAlpha == Catch::Approx(0.75f));
	REQUIRE(m2->restrictionEnabled == true);
}


TEST_CASE("Module limit reads modules through the module access", "[PanicRoom][module]") {
	Test::ModuleScaffold<PanicRoomModule> mods;
	ModuleMock mock;
	auto module = mods.create("PanicRoom");
	auto widget = Test::createWidget<PanicRoomWidget>(module);

	SECTION("No removal when at the limit") {
		rack::app::ModuleWidget* mw1 = makeFakeWidget(1);
		rack::app::ModuleWidget* mw2 = makeFakeWidget(2);
		rack::app::ModuleWidget* mw3 = makeFakeWidget(3);
		mock.modules.widgets = {mw1, mw2, mw3};

		module->moduleLimitEnabled = true;
		module->moduleLimit = 3;

		int callsBefore = mock.modules.getModuleWidgetsCalls;
		widget->step();

		// step() consults getModuleWidgets() through the mock, but nothing is removed.
		REQUIRE(mock.modules.getModuleWidgetsCalls > callsBefore);
		CHECK(mock.modules.removedIds.empty());

		destroyFakeWidget(mw1);
		destroyFakeWidget(mw2);
		destroyFakeWidget(mw3);
	}

	Test::destroyWidget(widget);
}

TEST_CASE("Module limit removes excess modules through the module access", "[PanicRoom][module]") {
	Test::ModuleScaffold<PanicRoomModule> mods;
	ModuleMock mock;
	auto module = mods.create("PanicRoom");
	auto widget = Test::createWidget<PanicRoomWidget>(module);

	SECTION("Removes the most recently added modules down to the limit") {
		rack::app::ModuleWidget* mw1 = makeFakeWidget(1);
		rack::app::ModuleWidget* mw2 = makeFakeWidget(2);
		rack::app::ModuleWidget* mw3 = makeFakeWidget(3);
		mock.modules.widgets = {mw1, mw2, mw3};

		module->moduleLimitEnabled = true;
		module->moduleLimit = 3;
		widget->step();  // 3 <= 3 → enforcement arms, nothing removed

		module->moduleLimit = 1;
		widget->step();  // 3 > 1 → remove the two most recently added (ids 3, 2)

		REQUIRE(mock.modules.removedIds.size() == 2);
		CHECK(mock.modules.removedIds[0] == 3);
		CHECK(mock.modules.removedIds[1] == 2);

		destroyFakeWidget(mw1);
		destroyFakeWidget(mw2);
		destroyFakeWidget(mw3);
	}

	Test::destroyWidget(widget);
}

TEST_CASE("Cable limit removes excess cables through the cable access", "[PanicRoom][cable]") {
	Test::ModuleScaffold<PanicRoomModule> mods;
	CableMock mock;
	auto module = mods.create("PanicRoom");
	auto widget = Test::createWidget<PanicRoomWidget>(module);

	SECTION("Removes the most recently added cables down to the limit") {
		rack::app::CableWidget* cw1 = makeFakeCable();
		rack::app::CableWidget* cw2 = makeFakeCable();
		rack::app::CableWidget* cw3 = makeFakeCable();
		mock.cables.cables = {cw1, cw2, cw3};

		module->cableLimitEnabled = true;
		module->cableLimit = 3;
		widget->step();  // 3 <= 3 → enforcement arms, nothing removed

		module->cableLimit = 1;
		widget->step();  // 3 > 1 → remove the two most recently added (cw3, cw2)

		REQUIRE(mock.cables.removed.size() == 2);
		CHECK(mock.cables.removed[0] == cw3);
		CHECK(mock.cables.removed[1] == cw2);

		destroyFakeCable(cw1);
		destroyFakeCable(cw2);
		destroyFakeCable(cw3);
	}

	Test::destroyWidget(widget);
}