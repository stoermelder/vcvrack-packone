// GLUE test cases. Included by Glue.test.cpp inside namespace __module.
// Not a standalone header: Glue.test.hpp supplies everything these cases use.

TEST_CASE("Construction and initialization", "[Glue]") {
	Test::ModuleScaffold<GlueModule> mods;
	GlueModule* m = mods.create("Glue");
	GlueWidget* mw = Test::createWidget<GlueWidget>("Glue");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
}

TEST_CASE("Preset JSON null-guards", "[Glue][JSON]") {
	Test::ModuleScaffold<GlueModule> mods;
	auto module = mods.create("Glue");

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

TEST_CASE("JSON round-trip preserves state", "[Glue][JSON]") {
	Test::ModuleScaffold<GlueModule> mods;
	GlueModule* m = mods.create("Glue");
	GlueModule* m2 = mods.create("Glue");

	SECTION("Default label settings round-trip") {
		// Distinct, non-default values for every scalar stored to JSON
		m->panelTheme = 1;
		m->defaultSize = 12.5f;
		m->defaultWidth = 34.5f;
		m->defaultAngle = 0.25f;
		m->defaultOpacity = 0.75f;
		NVGcolor c = color::fromHexString("#11223344");
		m->defaultColor = c;
		m->defaultFont = 2;
		NVGcolor fc = color::fromHexString("#55667788");
		m->defaultFontColor = fc;
		m->skewLabels = false;

		json_t* j = m->dataToJson();
		m2->dataFromJson(j);
		json_decref(j);

		REQUIRE(m2->panelTheme == 1);
		REQUIRE(m2->defaultSize == Catch::Approx(12.5f));
		REQUIRE(m2->defaultWidth == Catch::Approx(34.5f));
		REQUIRE(m2->defaultAngle == Catch::Approx(0.25f));
		REQUIRE(m2->defaultOpacity == Catch::Approx(0.75f));
		REQUIRE(color::toHexString(m2->defaultColor) == color::toHexString(c));
		REQUIRE(m2->defaultFont == 2);
		REQUIRE(color::toHexString(m2->defaultFontColor) == color::toHexString(fc));
		REQUIRE(m2->skewLabels == false);
	}

	SECTION("labels array round-trips (module labels)") {
		// Add three fully-populated module labels with distinctive values
		const int numLabels = 3;
		for (int i = 0; i < numLabels; i++) {
			ModuleLabel* l = m->addModuleLabel();
			l->moduleId = 100 + i;
			l->x = 1.1f * (i + 1);
			l->y = 2.2f * (i + 1);
			l->angle = 0.1f * i;
			l->skew = 0.2f * i;
			l->opacity = 0.3f * (i + 1);
			l->width = 4.4f * (i + 1);
			l->size = 5.5f * (i + 1);
			l->text = "label-" + std::to_string(i);
			l->color = color::fromHexString("#aabbccdd");
			l->font = i;
			l->fontColor = color::fromHexString("#eeff0011");
		}

		json_t* j = m->dataToJson();
		// The labels array must be serialized with one entry per label
		json_t* labelsJ = json_object_get(j, "labels");
		REQUIRE(labelsJ != nullptr);
		REQUIRE(json_array_size(labelsJ) == (size_t) numLabels);

		m2->dataFromJson(j);
		json_decref(j);

		REQUIRE(m2->moduleLabels.size() == (size_t) numLabels);
		int i = 0;
		for (ModuleLabel* l : m2->moduleLabels) {
			REQUIRE(l->moduleId == 100 + i);
			REQUIRE(l->x == Catch::Approx(1.1f * (i + 1)));
			REQUIRE(l->y == Catch::Approx(2.2f * (i + 1)));
			REQUIRE(l->angle == Catch::Approx(0.1f * i));
			REQUIRE(l->skew == Catch::Approx(0.2f * i));
			REQUIRE(l->opacity == Catch::Approx(0.3f * (i + 1)));
			REQUIRE(l->width == Catch::Approx(4.4f * (i + 1)));
			REQUIRE(l->size == Catch::Approx(5.5f * (i + 1)));
			REQUIRE(l->text == "label-" + std::to_string(i));
			REQUIRE(color::toHexString(l->color) == "#aabbccdd");
			REQUIRE(l->font == i);
			REQUIRE(color::toHexString(l->fontColor) == "#eeff0011");
			i++;
		}
	}

	SECTION("cableLabels array round-trips") {
		const int numLabels = 2;
		for (int i = 0; i < numLabels; i++) {
			CableLabel* cl = m->addCableLabel();
			cl->cableId = 200 + i;
			cl->atInput = (i % 2 == 0);
			cl->width = 7.7f * (i + 1);
			cl->size = 8.8f * (i + 1);
			cl->distance = 9.9f * (i + 1);
			cl->text = "cable-" + std::to_string(i);
			cl->font = i + 1;
		}

		json_t* j = m->dataToJson();
		json_t* cableLabelsJ = json_object_get(j, "cableLabels");
		REQUIRE(cableLabelsJ != nullptr);
		REQUIRE(json_array_size(cableLabelsJ) == (size_t) numLabels);

		m2->dataFromJson(j);
		json_decref(j);

		REQUIRE(m2->cableLabels.size() == (size_t) numLabels);
		int i = 0;
		for (CableLabel* cl : m2->cableLabels) {
			REQUIRE(cl->cableId == 200 + i);
			REQUIRE(cl->atInput == (i % 2 == 0));
			REQUIRE(cl->width == Catch::Approx(7.7f * (i + 1)));
			REQUIRE(cl->size == Catch::Approx(8.8f * (i + 1)));
			REQUIRE(cl->distance == Catch::Approx(9.9f * (i + 1)));
			REQUIRE(cl->text == "cable-" + std::to_string(i));
			REQUIRE(cl->font == i + 1);
			i++;
		}
	}

	SECTION("Wrong-typed cableLabels is ignored, not treated as an empty array") {
		// addModuleLabel() also touches the "labels" key, whose loader already guards with
		// json_is_array() (Glue.cpp:178) - mirror that here for "cableLabels" (Glue.cpp:181,
		// currently missing the guard). A wrong-typed value should leave existing state
		// untouched, exactly like the other type-guarded scalars in dataFromJson()
		// (e.g. defaultColorJ), not silently wipe it.
		CableLabel* cl = m->addCableLabel();
		cl->cableId = 300;
		cl->text = "should-survive";

		json_t* j = m->dataToJson();
		json_object_set_new(j, "cableLabels", json_string("wrong-type"));

		m->dataFromJson(j);
		json_decref(j);

		REQUIRE(m->cableLabels.size() == 1);
		REQUIRE(m->cableLabels.front()->cableId == 300);
		REQUIRE(m->cableLabels.front()->text == "should-survive");
	}
}


TEST_CASE("setCableLabelAtInput invalidates the placement cache", "[Glue]") {
	// Bug #3: the "At Input/Output Port" menu items used to write cableLabel->atInput
	// directly (Rack::createValuePtrMenuItem), leaving cacheValid untouched. The cache key
	// is only the two endpoint positions, so the label stayed at the old tFinal/angle/
	// offset until the cable itself moved. setCableLabelAtInput() is the fixed call site's
	// helper (GlueTypes.hpp) - both menu items now go through it.
	CableLabel cl;
	cl.atInput = true;
	cl.cacheValid = true;
	cl.cachedBoxPos = Vec(10.f, 20.f);

	setCableLabelAtInput(&cl, false);

	REQUIRE(cl.atInput == false);
	REQUIRE(cl.cacheValid == false);
}


TEST_CASE("consolidate() merges labels from other GLUE instances", "[Glue]") {
	MockHistoryAccess mockHistory;
	Test::mock::Guard<vcv::HistoryAccess> historyGuard{vcv::historyAccess, &mockHistory};

	GlueModule* survivorM = Test::createModule<GlueModule>("Glue");
	GlueWidget* survivorMw = Test::createWidget<GlueWidget>(survivorM);
	Test::registerModule(survivorM, survivorMw);

	GlueModule* victimM = Test::createModule<GlueModule>("Glue");
	GlueWidget* victimMw = Test::createWidget<GlueWidget>(victimM);
	Test::registerModule(victimM, victimMw);

	// Populate the victim with one module label and one cable label.
	ModuleLabel* ml = victimM->addModuleLabel();
	ml->moduleId = 42;
	ml->text = "victim-module-label";

	CableLabel* cl = victimM->addCableLabel();
	cl->cableId = 99;
	cl->text = "victim-cable-label";

	REQUIRE(survivorM->moduleLabels.size() == 0);
	REQUIRE(survivorM->cableLabels.size() == 0);
	REQUIRE(victimM->moduleLabels.size() == 1);
	REQUIRE(victimM->cableLabels.size() == 1);

	survivorMw->consolidate();

	// The victim widget is removed from the rack and deleted by consolidate() itself.
	SECTION("Module labels are moved to the surviving instance") {
		REQUIRE(survivorM->moduleLabels.size() == 1);
		REQUIRE(survivorM->moduleLabels.front()->text == "victim-module-label");
	}

	SECTION("Cable labels are moved to the surviving instance, not destroyed") {
		// Bug #4: consolidate() only moves moduleLabels; cableLabels are neither moved nor
		// cleared before the victim module is deleted, silently destroying them.
		REQUIRE(survivorM->cableLabels.size() == 1);
		if (survivorM->cableLabels.size() == 1) {
			REQUIRE(survivorM->cableLabels.front()->text == "victim-cable-label");
		}
	}

	Test::unregisterModule(survivorM, survivorMw);
}