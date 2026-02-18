#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "Infix.cpp"

using namespace StoermelderPackOne::Infix;

Test::TestContext<> testContext;

TEST_CASE("JSON serialization", "[Infix]") {
	auto module = Test::createModule<InfixModule<16>>("Infix");

	SECTION("Default state roundtrips") {
		json_t *rootJ = module->dataToJson();
		
		auto module2 = Test::createModule<InfixModule<16>>("Infix");
		module2->dataFromJson(rootJ);
		
		REQUIRE(module2->panelTheme == module->panelTheme);
		json_decref(rootJ);
	}

	Test::destroyModule(module);
}

TEST_CASE("InfixMicro variant", "[Infix]") {
	auto module = Test::createModule<InfixModule<8>>("InfixMicro");

	SECTION("InfixMicro initializes") {
		REQUIRE(module->panelTheme == StoermelderPackOne::pluginSettings.panelThemeDefault);
	}

	SECTION("InfixMicro has correct inputs") {
		// INPUT_POLY + 8 INPUT_MONO inputs
		REQUIRE(module->inputs.size() == 9);
	}

	SECTION("InfixMicro has correct lights") {
		REQUIRE(module->lights.size() == 8);
	}

	SECTION("InfixMicro JSON serialization") {
		module->panelTheme = 2;
		json_t *rootJ = module->dataToJson();
		
		auto module2 = Test::createModule<InfixModule<8>>("InfixMicro");
		module2->dataFromJson(rootJ);
		
		REQUIRE(module2->panelTheme == 2);
		json_decref(rootJ);
	}

	Test::destroyModule(module);
}

TEST_CASE("Channel count", "[Infix]") {
	auto module = Test::createModule<InfixModule<16>>("Infix");

	SECTION("Module has correct number of channels") {
		// 1 poly input, 16 mono inputs, 1 poly output, 16 lights
		REQUIRE(module->inputs.size() == 17);
		REQUIRE(module->outputs.size() == 1);
		REQUIRE(module->lights.size() == 16);
	}

	Test::destroyModule(module);
}

TEST_CASE("Processing without connections", "[Infix]") {
	auto module = Test::createModule<InfixModule<16>>("Infix");

	SECTION("Module processes safely without connections") {
		// Should not crash with no connections
		REQUIRE_NOTHROW(module->process(Test::makeProcessArgs(0)));
		
		// Output should have 0 channels when no input connected
		REQUIRE(module->outputs[InfixModule<16>::OUTPUT_POLY].getChannels() == 0);
	}

	Test::destroyModule(module);
}

TEST_CASE("Multiple independent serialization", "[Infix]") {
	auto module = Test::createModule<InfixModule<16>>("Infix");

	SECTION("Multiple save/load cycles preserve state") {
		module->panelTheme = 1;
		json_t *rootJ1 = module->dataToJson();
		
		auto module2 = Test::createModule<InfixModule<16>>("Infix");
		module2->dataFromJson(rootJ1);
		module2->panelTheme = 2;
		
		json_t *rootJ2 = module2->dataToJson();
		auto module3 = Test::createModule<InfixModule<16>>("Infix");
		module3->dataFromJson(rootJ2);
		
		REQUIRE(module3->panelTheme == 2);
		
		json_decref(rootJ1);
		json_decref(rootJ2);
		Test::destroyModule(module2);
		Test::destroyModule(module3);
	}

	Test::destroyModule(module);
}

TEST_CASE("Widget construction", "[UI][Infix]") {
	InfixWidget* w = Test::createWidget<InfixWidget>("Infix");
	REQUIRE(w != nullptr);
	REQUIRE(w->module == NULL);
	
	Test::destroyWidget(w);
}