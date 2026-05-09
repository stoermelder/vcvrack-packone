#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "Sipo.cpp"

using namespace StoermelderPackOne::Sipo;

SYNC_MODEL(modelSipo, "Sipo");
Test::TestContext<> testContext;

TEST_CASE("Construction and initialization", "[Sipo]") {
	SipoModule* m = Test::createModule<SipoModule>("Sipo");
	SipoWidget* mw = Test::createWidget<SipoWidget>("Sipo");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}

TEST_CASE("JSON serialization", "[Sipo]") {
	auto module = Test::createModule<SipoModule>("Sipo");

	SECTION("Theme persists through JSON") {
		module->panelTheme = 2;
		json_t* rootJ = module->dataToJson();
		
		auto moduleNew = Test::createModule<SipoModule>("Sipo");
		moduleNew->dataFromJson(rootJ);
		
		REQUIRE(moduleNew->panelTheme == 2);
		json_decref(rootJ);
		Test::destroyModule(moduleNew);
	}

	Test::destroyModule(module);
}

TEST_CASE("Data serialization", "[Sipo]") {
	auto module = Test::createModule<SipoModule>("Sipo");

	SECTION("Buffer data persists through JSON") {
		module->data[0] = 1.0f;
		module->data[1] = 2.5f;
		module->data[2] = -3.7f;
		module->dataPtr = 2;
		module->dataUsed = 3;
		
		json_t* rootJ = module->dataToJson();
		
		auto moduleNew = Test::createModule<SipoModule>("Sipo");
		moduleNew->dataFromJson(rootJ);
		
		REQUIRE(moduleNew->dataPtr == 2);
		REQUIRE(moduleNew->dataUsed == 3);
		REQUIRE(moduleNew->data[0] == Catch::Approx(1.0f));
		REQUIRE(moduleNew->data[1] == Catch::Approx(2.5f));
		REQUIRE(moduleNew->data[2] == Catch::Approx(-3.7f));
		
		json_decref(rootJ);
		Test::destroyModule(moduleNew);
	}

	Test::destroyModule(module);
}

TEST_CASE("Buffer bounds", "[Sipo]") {
	auto module = Test::createModule<SipoModule>("Sipo");

	SECTION("Can access all buffer positions") {
		for (int i = 0; i < 100; i++) {
			module->data[i] = (float)i;
		}
		
		for (int i = 0; i < 100; i++) {
			REQUIRE(module->data[i] == Catch::Approx((float)i));
		}
	}
	
	SECTION("Buffer is large enough") {
		// MAX_DATA should be at least 4096
		module->data[4095] = 42.0f;
		REQUIRE(module->data[4095] == 42.0f);
	}

	Test::destroyModule(module);
}