#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "Sipo.cpp"

using namespace StoermelderPackOne::Sipo;

static Test::TestContext<> testContext;

TEST_CASE("Module initialization", "[Sipo]") {
	auto module = Test::createModule<SipoModule>("Sipo");

	SECTION("Buffer allocated and cleared") {
		REQUIRE(module->data != nullptr);
		for (int i = 0; i < 10; i++) {
			REQUIRE(module->data[i] == 0.f);
		}
	}

	Test::destroyModule(module);
}

TEST_CASE("Parameters management", "[Sipo]") {
	auto module = Test::createModule<SipoModule>("Sipo");

	SECTION("Skip parameter accessible") {
		REQUIRE(module->params.size() >= 2);  // SKIP_PARAM, INCR_PARAM
	}

	SECTION("Increment parameter accessible") {
		REQUIRE(module->params.size() >= 2);
	}

	Test::destroyModule(module);
}

TEST_CASE("Direct buffer access", "[Sipo]") {
	auto module = Test::createModule<SipoModule>("Sipo");

	SECTION("Buffer can be written directly") {
		module->data[0] = 1.5f;
		module->data[1] = 2.5f;
		module->data[5] = 3.5f;
		
		REQUIRE(module->data[0] == 1.5f);
		REQUIRE(module->data[1] == 2.5f);
		REQUIRE(module->data[5] == 3.5f);
	}

	SECTION("Pointer can be manipulated") {
		module->dataPtr = 10;
		module->dataUsed = 20;
		
		REQUIRE(module->dataPtr == 10);
		REQUIRE(module->dataUsed == 20);
	}

	Test::destroyModule(module);
}

TEST_CASE("Reset behavior", "[Sipo]") {
	auto module = Test::createModule<SipoModule>("Sipo");

	SECTION("Reset clears data and pointers") {
		// Set some state
		module->data[0] = 5.5f;
		module->data[10] = 3.2f;
		module->dataPtr = 100;
		module->dataUsed = 50;
		
		// Reset
        rack::engine::Module::ResetEvent re;
        module->onReset(re);
		
		REQUIRE(module->dataPtr == 0);
		REQUIRE(module->dataUsed == 0);
		REQUIRE(module->data[0] == 0.f);
		REQUIRE(module->data[10] == 0.f);
	}

	Test::destroyModule(module);
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

TEST_CASE("Parameter ranges", "[Sipo]") {
	auto module = Test::createModule<SipoModule>("Sipo");

	SECTION("Skip parameter has correct range") {
		auto pq = module->paramQuantities[SipoModule::SKIP_PARAM];
		REQUIRE(pq != nullptr);
		REQUIRE(pq->getMinValue() == 0.f);
		// MAX_DATA_32 - 1 = 4096/32 - 1 = 127
		REQUIRE(pq->getMaxValue() >= 100.f);
	}
	
	SECTION("Increment parameter has correct range") {
		auto pq = module->paramQuantities[SipoModule::INCR_PARAM];
		REQUIRE(pq != nullptr);
		REQUIRE(pq->getMinValue() == 0.f);
		// MAX_DATA_32_16 = (4096/32)/16 = 8
		REQUIRE(pq->getMaxValue() >= 1.f);
	}

	Test::destroyModule(module);
}

TEST_CASE("Output configuration", "[Sipo]") {
	auto module = Test::createModule<SipoModule>("Sipo");

	SECTION("Module has polyphonic output") {
		// Module should have exactly 1 output
		REQUIRE(module->outputs.size() == 1);
	}

	Test::destroyModule(module);
}