#include "../../test/framework.hpp"
#include "Glue.cpp"

using namespace StoermelderPackOne::Glue;

SYNC_MODEL(modelGlue, "Glue");
Test::TestContext<> testContext;

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

}