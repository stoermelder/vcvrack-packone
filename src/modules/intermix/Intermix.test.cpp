#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "Intermix.cpp"

using namespace StoermelderPackOne::Intermix;

SYNC_MODEL(modelIntermix, "Intermix");
Test::TestContext<> testContext;

TEST_CASE("Scene selection", "[Intermix]") {
	auto module = Test::createModule<IntermixModule<8>>("Intermix");

	SECTION("sceneSet changes scene correctly") {
		module->sceneSet(3);
		REQUIRE(module->sceneSelected == 3);
		REQUIRE(module->params[IntermixModule<8>::PARAM_SCENE + 3].getValue() == 1.f);
		REQUIRE(module->params[IntermixModule<8>::PARAM_SCENE + 0].getValue() == 0.f);
	}

	SECTION("sceneSet clamps to sceneCount") {
		module->sceneCount = 4;
		module->sceneSet(10);
		REQUIRE(module->sceneSelected == 3); // sceneCount - 1
	}

	SECTION("sceneSet ignores negative values") {
		module->sceneSet(5);
		REQUIRE(module->sceneSelected == 5);
		module->sceneSet(-1);
		REQUIRE(module->sceneSelected == 5); // Unchanged
	}

	SECTION("sceneSet ignores same scene") {
		module->sceneSet(2);
		module->scenes[2].matrix[0][0] = 1.f;
		module->sceneSet(2); // Same scene
		REQUIRE(module->sceneSelected == 2);
	}

	Test::destroyModule(module);
}

TEST_CASE("Scene copy", "[Intermix]") {
	auto module = Test::createModule<IntermixModule<8>>("Intermix");

	SECTION("sceneCopy duplicates all scene data") {
		// Setup source scene
		module->sceneSet(0);
		module->scenes[0].matrix[0][0] = 1.f;
		module->scenes[0].matrix[1][2] = 0.5f;
		module->scenes[0].output[0] = OM_OFF;
		module->scenes[0].outputAt[0] = 0.75f;
		module->scenes[0].input[0] = IM_OFF;
		
		// Copy to scene 1
		module->sceneCopy(1);
		
		REQUIRE(module->scenes[1].matrix[0][0] == 1.f);
		REQUIRE(module->scenes[1].matrix[1][2] == 0.5f);
		REQUIRE(module->scenes[1].output[0] == OM_OFF);
		REQUIRE(module->scenes[1].outputAt[0] == 0.75f);
		REQUIRE(module->scenes[1].input[0] == IM_OFF);
	}

	SECTION("sceneCopy ignores same scene") {
		module->scenes[0].matrix[0][0] = 1.f;
		module->sceneCopy(0);
		REQUIRE(module->scenes[0].matrix[0][0] == 1.f);
	}

	Test::destroyModule(module);
}

TEST_CASE("Scene reset", "[Intermix]") {
	auto module = Test::createModule<IntermixModule<8>>("Intermix");

	SECTION("sceneReset clears current scene") {
		module->sceneSet(2);
		module->scenes[2].matrix[0][0] = 1.f;
		module->scenes[2].output[0] = OM_OFF;
		module->scenes[2].outputAt[0] = 0.5f;
		module->scenes[2].input[0] = IM_FADE;
		
		module->sceneReset();
		
		REQUIRE(module->scenes[2].matrix[0][0] == 0.f);
		REQUIRE(module->scenes[2].output[0] == OM_OUT);
		REQUIRE(module->scenes[2].outputAt[0] == 1.f);
		REQUIRE(module->scenes[2].input[0] == IM_DIRECT);
		REQUIRE(module->currentMatrix[0][0] == 0.f);
	}

	Test::destroyModule(module);
}

TEST_CASE("Scene count", "[Intermix]") {
	auto module = Test::createModule<IntermixModule<8>>("Intermix");

	SECTION("sceneSetCount limits scene selection") {
		module->sceneSet(7);
		REQUIRE(module->sceneSelected == 7);
		
		module->sceneSetCount(5);
		REQUIRE(module->sceneCount == 5);
		REQUIRE(module->sceneSelected == 4); // Clamped down
	}

	SECTION("sceneSetCount with current scene in range") {
		module->sceneSet(3);
		module->sceneSetCount(6);
		REQUIRE(module->sceneSelected == 3); // Unchanged
	}

	Test::destroyModule(module);
}

TEST_CASE("Matrix processing", "[Intermix]") {
	auto module = Test::createModule<IntermixModule<8>>("Intermix");

	SECTION("Matrix button changes matrix value") {
		module->params[IntermixModule<8>::PARAM_MATRIX + 0].setValue(1.f);
		module->process(Test::makeProcessArgs(1));
		
		// Process multiple times to allow divider to update
		for (int i = 0; i < 100; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		REQUIRE(module->scenes[0].matrix[0][0] == 1.f);
	}

	Test::destroyModule(module);
}

TEST_CASE("Output processing", "[Intermix]") {
	auto module = Test::createModule<IntermixModule<8>>("Intermix");

	SECTION("Direct mode passes input through matrix") {
		// Set up scene data
		module->inputMode[0] = IM_DIRECT;
		module->params[IntermixModule<8>::PARAM_MATRIX + 0].setValue(1.f);
		module->params[IntermixModule<8>::PARAM_OUTPUT + 0].setValue(0.f); // OM_OUT
		module->channelCount = 1;
		
		// Set input voltage
		module->inputs[IntermixModule<8>::INPUT + 0].channels = 1;
		module->inputs[IntermixModule<8>::INPUT + 0].setVoltage(5.f);
		
		// Process enough samples for scene divider to trigger (64+)
		// Need to process more to ensure divider triggers and settles
		for (int i = 0; i < 130; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		// Output should be 5V (1.0 * 5V)
		REQUIRE(module->outputs[IntermixModule<8>::OUTPUT + 0].getVoltage() == Catch::Approx(5.f).margin(0.01f));
	}

	SECTION("Output clamping works") {
		module->outputClamp = true;
		module->inputMode[0] = IM_DIRECT;
		module->params[IntermixModule<8>::PARAM_MATRIX + 0].setValue(1.f);
		module->params[IntermixModule<8>::PARAM_OUTPUT + 0].setValue(0.f);
		module->channelCount = 1;
		
		module->inputs[IntermixModule<8>::INPUT + 0].channels = 1;
		module->inputs[IntermixModule<8>::INPUT + 0].setVoltage(15.f);
		
		for (int i = 0; i < 130; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		// Should be clamped to 10V
		REQUIRE(module->outputs[IntermixModule<8>::OUTPUT + 0].getVoltage() == Catch::Approx(10.f).margin(0.01f));
	}

	SECTION("Output disable works") {
		module->inputMode[0] = IM_DIRECT;
		module->params[IntermixModule<8>::PARAM_MATRIX + 0].setValue(1.f);
		module->params[IntermixModule<8>::PARAM_OUTPUT + 0].setValue(1.f); // OM_OFF
		module->channelCount = 1;
		
		module->inputs[IntermixModule<8>::INPUT + 0].channels = 1;
		module->inputs[IntermixModule<8>::INPUT + 0].setVoltage(5.f);
		
		for (int i = 0; i < 130; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		REQUIRE(module->outputs[IntermixModule<8>::OUTPUT + 0].getVoltage() == 0.f);
	}

	SECTION("Output attenuverter works") {
		module->inputMode[0] = IM_DIRECT;
		module->params[IntermixModule<8>::PARAM_MATRIX + 0].setValue(1.f);
		module->params[IntermixModule<8>::PARAM_OUTPUT + 0].setValue(0.f);
		module->params[IntermixModule<8>::PARAM_AT + 0].setValue(0.5f);
		module->channelCount = 1;
		
		module->inputs[IntermixModule<8>::INPUT + 0].channels = 1;
		module->inputs[IntermixModule<8>::INPUT + 0].setVoltage(4.f);
		
		for (int i = 0; i < 130; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		// 4V * 1.0 * 0.5 = 2V
		REQUIRE(module->outputs[IntermixModule<8>::OUTPUT + 0].getVoltage() == Catch::Approx(2.f).margin(0.01f));
	}

	Test::destroyModule(module);
}

TEST_CASE("Input modes", "[Intermix]") {
	auto module = Test::createModule<IntermixModule<8>>("Intermix");

	SECTION("Off mode produces no output") {
		module->inputMode[0] = IM_OFF;
		module->params[IntermixModule<8>::PARAM_MATRIX + 0].setValue(1.f);
		module->params[IntermixModule<8>::PARAM_OUTPUT + 0].setValue(0.f);
		module->channelCount = 1;
		
		module->inputs[IntermixModule<8>::INPUT + 0].channels = 1;
		module->inputs[IntermixModule<8>::INPUT + 0].setVoltage(5.f);
		
		for (int i = 0; i < 130; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		REQUIRE(module->outputs[IntermixModule<8>::OUTPUT + 0].getVoltage() == 0.f);
	}

	SECTION("Constant voltage mode") {
		module->inputMode[0] = IM_ADD_01C; // +1 cent = +1/12V
		module->params[IntermixModule<8>::PARAM_MATRIX + 0].setValue(1.f);
		module->params[IntermixModule<8>::PARAM_OUTPUT + 0].setValue(0.f);
		module->channelCount = 1;
		
		for (int i = 0; i < 130; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		float expected = 1.f / 12.f;
		REQUIRE(module->outputs[IntermixModule<8>::OUTPUT + 0].getVoltage() == Catch::Approx(expected).margin(0.001f));
	}

	Test::destroyModule(module);
}

TEST_CASE("Scene CV modes", "[Intermix]") {
	auto module = Test::createModule<IntermixModule<8>>("Intermix");

	SECTION("Trigger forward mode") {
		module->sceneMode = SCENE_CV_MODE::TRIG_FWD;
		module->sceneCount = 8;
		module->sceneSet(0);
		
		// Connect the input
		module->inputs[IntermixModule<8>::INPUT_SCENE].channels = 1;
		
		// Send trigger (low to high)
		module->inputs[IntermixModule<8>::INPUT_SCENE].setVoltage(0.f);
		module->process(Test::makeProcessArgs(1));
		
		module->inputs[IntermixModule<8>::INPUT_SCENE].setVoltage(10.f);
		module->process(Test::makeProcessArgs(1));
		
		// After one trigger, should advance from 0 to 1
		REQUIRE(module->sceneSelected == 1);
	}

	SECTION("Voltage mode 0-10V") {
		module->sceneMode = SCENE_CV_MODE::VOLT;
		module->sceneCount = 8;
		
		module->inputs[IntermixModule<8>::INPUT_SCENE].channels = 1;
		module->inputs[IntermixModule<8>::INPUT_SCENE].setVoltage(5.f);
		module->process(Test::makeProcessArgs(1));
		
		// 5V (50% of 10V) maps to floor(rescale(5, 0, 10, 0, 7.999)) = floor(3.999) = 3
		REQUIRE(module->sceneSelected == 3);
	}

	SECTION("C4 mode") {
		module->sceneMode = SCENE_CV_MODE::C4;
		module->sceneCount = 8;
		
		module->inputs[IntermixModule<8>::INPUT_SCENE].channels = 1;
		module->inputs[IntermixModule<8>::INPUT_SCENE].setVoltage(2.f / 12.f); // 2 semitones
		module->process(Test::makeProcessArgs(1));
		
		REQUIRE(module->sceneSelected == 2);
	}

	Test::destroyModule(module);
}

TEST_CASE("Expander interface", "[Intermix]") {
	auto module = Test::createModule<IntermixModule<8>>("Intermix");

	SECTION("expGetCurrentMatrix returns current matrix") {
		module->currentMatrix[0][0] = 0.5f;
		module->currentMatrix[1][2] = 0.75f;
		
		auto matrix = module->expGetCurrentMatrix();
		REQUIRE(matrix[0][0] == 0.5f);
		REQUIRE(matrix[1][2] == 0.75f);
	}

	SECTION("expGetChannelCount returns channel count") {
		module->channelCount = 4;
		REQUIRE(module->expGetChannelCount() == 4);
	}

	SECTION("expSetFade sets fade parameters") {
		float fadeIn[8] = {1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f};
		float fadeOut[8] = {2.f, 2.f, 2.f, 2.f, 2.f, 2.f, 2.f, 2.f};
		
		module->channelCount = 1;
		
		// Process to increment timestamp
		module->process(Test::makeProcessArgs(1));
		uint32_t tsBase = module->ts;
		
		module->expSetFade(0, fadeIn, fadeOut);
		
		// Check that timestamps are updated (they're set to current ts)
		REQUIRE(module->fadeInTs[0] == tsBase);
		REQUIRE(module->fadeOutTs[0] == tsBase);
	}

	Test::destroyModule(module);
}

TEST_CASE("JSON serialization", "[Intermix]") {
	auto module = Test::createModule<IntermixModule<8>>("Intermix");

	SECTION("Module state is serialized and deserialized") {
		module->panelTheme = 1;
		module->padBrightness = 0.5f;
		module->inputVisualize = true;
		module->outputClamp = false;
		module->channelCount = 4;
		module->sceneSelected = 3;
		module->sceneMode = SCENE_CV_MODE::VOLT;
		module->sceneInputMode = true;
		module->sceneAtMode = false;
		module->sceneCount = 6;
		module->sceneLock = true;
		module->inputMode[0] = IM_FADE;
		module->scenes[0].matrix[0][0] = 1.f;
		module->scenes[0].output[0] = OM_OFF;
		module->scenes[0].outputAt[0] = 0.75f;
		module->scenes[0].input[0] = IM_OFF;
		
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		
		auto moduleNew = Test::createModule<IntermixModule<8>>("Intermix");
		moduleNew->dataFromJson(rootJ);
		
		REQUIRE(moduleNew->panelTheme == 1);
		REQUIRE(moduleNew->padBrightness == Catch::Approx(0.5f).margin(0.01f));
		REQUIRE(moduleNew->inputVisualize == true);
		REQUIRE(moduleNew->outputClamp == false);
		REQUIRE(moduleNew->channelCount == 4);
		REQUIRE(moduleNew->sceneSelected == 3);
		REQUIRE(moduleNew->sceneMode == SCENE_CV_MODE::VOLT);
		REQUIRE(moduleNew->sceneInputMode == true);
		REQUIRE(moduleNew->sceneAtMode == false);
		REQUIRE(moduleNew->sceneCount == 6);
		REQUIRE(moduleNew->sceneLock == true);
		REQUIRE(moduleNew->inputMode[0] == IM_FADE);
		REQUIRE(moduleNew->scenes[0].matrix[0][0] == 1.f);
		REQUIRE(moduleNew->scenes[0].output[0] == OM_OFF);
		REQUIRE(moduleNew->scenes[0].outputAt[0] == Catch::Approx(0.75f).margin(0.01f));
		REQUIRE(moduleNew->scenes[0].input[0] == IM_OFF);
		
		json_decref(rootJ);
		Test::destroyModule(moduleNew);
	}

	Test::destroyModule(module);
}

TEST_CASE("Polyphonic processing", "[Intermix]") {
	auto module = Test::createModule<IntermixModule<8>>("Intermix");

	SECTION("Multiple channels processed correctly") {
		module->channelCount = 4;
		module->inputMode[0] = IM_DIRECT;
		module->params[IntermixModule<8>::PARAM_MATRIX + 0].setValue(1.f);
		module->params[IntermixModule<8>::PARAM_OUTPUT + 0].setValue(0.f);
		
		// Set polyphonic input
		module->inputs[IntermixModule<8>::INPUT + 0].channels = 4;
		module->inputs[IntermixModule<8>::INPUT + 0].setVoltage(1.f, 0);
		module->inputs[IntermixModule<8>::INPUT + 0].setVoltage(2.f, 1);
		module->inputs[IntermixModule<8>::INPUT + 0].setVoltage(3.f, 2);
		module->inputs[IntermixModule<8>::INPUT + 0].setVoltage(4.f, 3);
		
		for (int i = 0; i < 130; i++) {
			module->process(Test::makeProcessArgs(1));
		}
		
		REQUIRE(module->outputs[IntermixModule<8>::OUTPUT + 0].getVoltage(0) == Catch::Approx(1.f).margin(0.01f));
		REQUIRE(module->outputs[IntermixModule<8>::OUTPUT + 0].getVoltage(1) == Catch::Approx(2.f).margin(0.01f));
		REQUIRE(module->outputs[IntermixModule<8>::OUTPUT + 0].getVoltage(2) == Catch::Approx(3.f).margin(0.01f));
		REQUIRE(module->outputs[IntermixModule<8>::OUTPUT + 0].getVoltage(3) == Catch::Approx(4.f).margin(0.01f));
	}

	Test::destroyModule(module);
}

TEST_CASE("Widget construction", "[UI][Intermix]") {
	IntermixWidget* w = Test::createWidget<IntermixWidget>("Intermix");
	REQUIRE(w != nullptr);
	REQUIRE(w->module == NULL);
	
	Test::destroyWidget(w);
}
