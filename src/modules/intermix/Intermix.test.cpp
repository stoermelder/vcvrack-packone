#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "Intermix.cpp"

using namespace StoermelderPackOne::Intermix;

SYNC_MODEL(modelIntermix, "Intermix");
Test::TestContext<> testContext;


TEST_CASE("Construction and initialization", "[Intermix]") {
	IntermixModule<8>* m = Test::createModule<IntermixModule<8>>("Intermix");
	IntermixWidget* mw = Test::createWidget<IntermixWidget>("Intermix");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}

TEST_CASE("Preset JSON null-guards", "[Intermix][JSON]") {
	auto module = Test::createModule<IntermixModule<8>>("Intermix");

	SECTION("All top-level properties are null-guarded in dataFromJson()") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetNullGuards(module, rootJ);
		json_decref(rootJ);
	}

	Test::destroyModule(module);
}


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

TEST_CASE("Scene CV modes basic", "[Intermix]") {
	auto module = Test::createModule<IntermixModule<8>>("Intermix");

	SECTION("Trigger forward mode") {
		module->sceneMode = SCENE_CV_MODE::TRIG_FWD;
		module->sceneCount = 8;
		module->sceneSet(0);

		// Connect the input
		module->inputs[IntermixModule<8>::INPUT_SCENE].channels = 1;

		// Accumulate resetTimer cooldown (>1ms at 44100Hz)
		for (int i = 0; i < 100; i++) {
			module->process(Test::makeProcessArgs(i + 10));
		}

		// Send trigger (low to high)
		module->inputs[IntermixModule<8>::INPUT_SCENE].setVoltage(0.f);
		module->process(Test::makeProcessArgs(200));

		module->inputs[IntermixModule<8>::INPUT_SCENE].setVoltage(10.f);
		module->process(Test::makeProcessArgs(201));

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

// Helper: process enough ticks for the guard (ts - fadeInTs[i] > division*2 = 128)
// to expire and for the sceneDivider to fire, guaranteeing setRise/setFall has run.
// With sceneDivider at 64, the first firing after guard expiry is at tick ~192.
static void runPastGuard(IntermixModule<8>* m, int ticks = 250) {
	for (int i = 0; i < ticks; i++)
		m->process(Test::makeProcessArgs(i));
}


TEST_CASE("Fade time: PARAM_FADEIN sets fader rise to param seconds", "[Intermix]") {
	// FadeLengthParamQuantity::getMaxValue() overrides the knob range to [0, maxFade],
	// so getValue() already returns seconds. Multiplying by getFadeLengthMax() again
	// gives param_seconds * maxFade (e.g. a 2s setting in 4s-mode becomes 8s).

	auto m = Test::createModule<IntermixModule<8>>("Intermix");
	m->channelCount = 1;

	SECTION("4s mode: PARAM_FADEIN of 2s gives fader rise of 2s") {
		m->fadeLengthMode = FADE_LENGTH_4S;
		m->params[IntermixModule<8>::PARAM_FADEIN].setValue(2.0f);
		runPastGuard(m);
		// Bug: fader.rise == 2.0 * 4 = 8.0. Correct: 2.0.
		REQUIRE(m->fader[0][0][0].rise == Catch::Approx(2.0f).margin(0.001f));
	}

	SECTION("15s mode: PARAM_FADEIN of 5s gives fader rise of 5s") {
		m->fadeLengthMode = FADE_LENGTH_15S;
		m->params[IntermixModule<8>::PARAM_FADEIN].setValue(5.0f);
		runPastGuard(m);
		// Bug: fader.rise == 5.0 * 15 = 75.0. Correct: 5.0.
		REQUIRE(m->fader[0][0][0].rise == Catch::Approx(5.0f).margin(0.001f));
	}

	SECTION("60s mode: PARAM_FADEIN of 10s gives fader rise of 10s") {
		m->fadeLengthMode = FADE_LENGTH_60S;
		m->params[IntermixModule<8>::PARAM_FADEIN].setValue(10.0f);
		runPastGuard(m);
		// Bug: fader.rise == 10.0 * 60 = 600.0. Correct: 10.0.
		REQUIRE(m->fader[0][0][0].rise == Catch::Approx(10.0f).margin(0.001f));
	}

	SECTION("4s mode: PARAM_FADEOUT of 3s gives fader fall of 3s") {
		m->fadeLengthMode = FADE_LENGTH_4S;
		m->params[IntermixModule<8>::PARAM_FADEOUT].setValue(3.0f);
		runPastGuard(m);
		// Bug: fader.fall == 3.0 * 4 = 12.0. Correct: 3.0.
		REQUIRE(m->fader[0][0][0].fall == Catch::Approx(3.0f).margin(0.001f));
	}

	Test::destroyModule(m);
}


TEST_CASE("Data race: expSetFade and process() share fader state without synchronization", "[Intermix]") {
	// Both expSetFade() (called by the IntermixFade expander) and the main
	// process() sceneDivider block write to fader[i][j][c].rise and read/write
	// fadeInTs[i]. These are plain non-atomic types. In VCV Rack's multi-threaded
	// engine, modules may run on separate worker threads, making these unsynchronised
	// accesses a C++ data race (undefined behaviour).
	//
	// The fadeInTs guard is meant to prevent the main module from overriding the
	// expander's fade time: after expSetFade sets fadeInTs[i] = ts, the main module
	// skips setRise for the next ~128 ticks. But the guard itself is read and written
	// without atomics, so in concurrent execution the read and write can interleave.

	auto m = Test::createModule<IntermixModule<8>>("Intermix");
	m->channelCount = 1;
	m->fadeLengthMode = FADE_LENGTH_4S;
	m->params[IntermixModule<8>::PARAM_FADEIN].setValue(0.0f); // main wants 0s fade

	// Verify the shared fields are plain (non-atomic) types.
	// If these static_asserts ever fail, the race has been fixed.
	static_assert(std::is_same<std::remove_reference_t<decltype(m->fadeInTs[0])>, uint32_t>::value,
		"fadeInTs should be uint32_t; make it std::atomic<uint32_t> to fix the race");
	static_assert(std::is_same<decltype(m->fader[0][0][0].rise), float>::value,
		"fader.rise should be float; linearFade needs explicit synchronisation to fix the race");

	SECTION("expSetFade writes fader.rise; process() writes the same field once guard expires") {
		float fadeIn[8] = {};
		for (int j = 0; j < 8; j++) fadeIn[j] = 3.0f;
		m->expSetFade(0, fadeIn, nullptr);

		// expSetFade wrote all columns of row 0, channel 0 to 3s
		REQUIRE(m->fader[0][0][0].rise == Catch::Approx(3.0f).margin(0.001f));

		// Run past the guard window so that process() overwrites with f1.
		// PARAM_FADEIN = 0.0, fadeLengthMode = 4s, so f1 = 0.0 * 4.0 = 0.0
		// (or just 0.0 with corrected code). Either way, it is not 3s.
		runPastGuard(m);

		// Main has overwritten expander's 3s. Both paths touch the same fader.rise
		// without any lock — the data race.
		REQUIRE(m->fader[0][0][0].rise != Catch::Approx(3.0f).margin(0.001f));
	}

	SECTION("guard uses plain uint32_t ts arithmetic, with no atomic fence between writers") {
		// Record ts at the moment expSetFade fires (= what gets stored in fadeInTs).
		m->process(Test::makeProcessArgs(0)); // ts becomes 1
		uint32_t tsBeforeSet = m->ts;

		float fadeIn[8] = {};
		for (int j = 0; j < 8; j++) fadeIn[j] = 7.0f;
		m->expSetFade(0, fadeIn, nullptr);

		// fadeInTs[0] was stamped with the current ts value
		REQUIRE(m->fadeInTs[0] == tsBeforeSet);

		// Guard check in process(): ts - fadeInTs[0] > division * 2 (= 128).
		// Since ts keeps incrementing with every process() call and fadeInTs[0]
		// is only refreshed when expSetFade fires, the guard expires after ~128
		// ticks without an expander call — a window where both modules are racing
		// to write the same fader field.
		REQUIRE(m->ts - m->fadeInTs[0] <= 128u); // guard still active after 1 tick

		// Simulate the guard expiry (no expander re-fire) and verify main overrides
		runPastGuard(m);
		REQUIRE(m->ts - m->fadeInTs[0] > 128u); // guard has expired
		// Main has now written fader.rise with f1 (PARAM_FADEIN=0 → f1=0),
		// overwriting expander's 7s. Either way, it is no longer 7s.
		REQUIRE(m->fader[0][0][0].rise != Catch::Approx(7.0f).margin(0.001f));
	}

	SECTION("wrong execution order: main reads stale fadeInTs before expander updates it") {
		// In a concurrent engine, main may read fadeInTs[0] in the same tick that
		// expSetFade is about to write it. If main reads the stale (expired) value
		// first, it calls setRise(f1); then expSetFade writes setRise(expFade).
		// The reverse can also happen. We simulate both orderings explicitly.

		float fadeIn[8] = {};
		for (int j = 0; j < 8; j++) fadeIn[j] = 6.0f;

		// Let guard expire so both modules would want to write in the same tick.
		runPastGuard(m);
		// [Order A: expander writes first, then main reads fresh fadeInTs → guard holds]
		m->expSetFade(0, fadeIn, nullptr);           // expander fires: rise=6, fadeInTs=ts
		m->process(Test::makeProcessArgs(300));       // main sees fresh fadeInTs → guard holds
		REQUIRE(m->fader[0][0][0].rise == Catch::Approx(6.0f).margin(0.001f));

		// [Order B: main reads stale fadeInTs first (guard expired), THEN expander fires]
		// Simulate by manually expiring the guard before the next process() tick.
		m->fadeInTs[0] = 0;                           // revert to expired state
		m->process(Test::makeProcessArgs(301));        // main sees guard expired → calls setRise(f1)
		// The main's setRise(f1) ran. Now expander would fire:
		m->expSetFade(0, fadeIn, nullptr);             // expander overwrites with 6s
		// In single-threaded (expander runs after main), expander wins the final write.
		// In multi-threaded (concurrent), either value may "win" — that is the race.
		// The test just documents that both writes happen in the same logical tick:
		REQUIRE(m->fader[0][0][0].rise == Catch::Approx(6.0f).margin(0.001f));
	}

	Test::destroyModule(m);
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


TEST_CASE("Scene CV modes with reset", "[Intermix]") {
	auto module = Test::createModule<IntermixModule<8>>("Intermix");

	// Initialize inputs and accumulate resetTimer > 1ms (similar to Transit pattern)
	auto initializeInputs = [&]() {
		module->inputs[IntermixModule<8>::INPUT_RESET].channels = 1;
		module->inputs[IntermixModule<8>::INPUT_RESET].setVoltage(0.0f);
		module->inputs[IntermixModule<8>::INPUT_SCENE].channels = 1;
		module->inputs[IntermixModule<8>::INPUT_SCENE].setVoltage(0.0f);
		for (int i = 0; i < 100; i++) {
			module->process(Test::makeProcessArgs(i + 10));
		}
	};

	auto triggerCv = [&](int frame) {
		module->inputs[IntermixModule<8>::INPUT_SCENE].setVoltage(10.0f);
		module->process(Test::makeProcessArgs(frame));
		module->inputs[IntermixModule<8>::INPUT_SCENE].setVoltage(0.0f);
		module->process(Test::makeProcessArgs(frame + 1));
	};

	auto triggerReset = [&](int frame) {
		module->inputs[IntermixModule<8>::INPUT_RESET].setVoltage(10.0f);
		module->process(Test::makeProcessArgs(frame));
		module->inputs[IntermixModule<8>::INPUT_RESET].setVoltage(0.0f);
		module->process(Test::makeProcessArgs(frame + 1));
	};

	SECTION("TRIG_FWD reset goes to scene 0") {
		module->sceneMode = SCENE_CV_MODE::TRIG_FWD;
		module->sceneCount = 4;
		module->sceneSet(3);
		initializeInputs();
		triggerReset(200);
		REQUIRE(module->sceneSelected == 0);
	}

	SECTION("TRIG_FWD trigger advances within boundaries") {
		module->sceneMode = SCENE_CV_MODE::TRIG_FWD;
		module->sceneCount = 4;
		initializeInputs();
		module->sceneSet(0);
		triggerCv(200);
		REQUIRE(module->sceneSelected == 1);
		triggerCv(300);
		REQUIRE(module->sceneSelected == 2);
	}

	SECTION("TRIG_FWD trigger wraps from last to first") {
		module->sceneMode = SCENE_CV_MODE::TRIG_FWD;
		module->sceneCount = 4;
		initializeInputs();
		module->sceneSet(3); // At last
		triggerCv(200);
		REQUIRE(module->sceneSelected == 0); // Wrapped to first
	}

	SECTION("TRIG_REV reset goes to last scene") {
		module->sceneMode = SCENE_CV_MODE::TRIG_REV;
		module->sceneCount = 4;
		module->sceneSet(1);
		initializeInputs();
		triggerReset(200);
		REQUIRE(module->sceneSelected == 3); // Last scene
	}

	SECTION("TRIG_REV trigger reverses within boundaries") {
		module->sceneMode = SCENE_CV_MODE::TRIG_REV;
		module->sceneCount = 4;
		initializeInputs();
		module->sceneSet(3); // At last
		triggerCv(200);
		REQUIRE(module->sceneSelected == 2);
		triggerCv(300);
		REQUIRE(module->sceneSelected == 1);
	}

	SECTION("TRIG_REV trigger wraps from first to last") {
		module->sceneMode = SCENE_CV_MODE::TRIG_REV;
		module->sceneCount = 4;
		initializeInputs();
		module->sceneSet(0); // At first
		triggerCv(200);
		REQUIRE(module->sceneSelected == 3); // Wrapped to last
	}

	SECTION("TRIG_PINGPONG reset resets direction") {
		module->sceneMode = SCENE_CV_MODE::TRIG_PINGPONG;
		module->sceneCount = 4;
		module->sceneCvModeDir = -1;
		module->sceneSet(2);
		initializeInputs();
		triggerReset(200);
		REQUIRE(module->sceneSelected == 0);
		REQUIRE(module->sceneCvModeDir == 1); // Direction reset
	}

	SECTION("TRIG_PINGPONG advances forward from first") {
		module->sceneMode = SCENE_CV_MODE::TRIG_PINGPONG;
		module->sceneCount = 4;
		module->sceneCvModeDir = 1;
		module->sceneSet(0);
		initializeInputs();
		triggerCv(200);
		REQUIRE(module->sceneSelected == 1);
		triggerCv(300);
		REQUIRE(module->sceneSelected == 2);
	}

	SECTION("TRIG_PINGPONG bounces at last and reverses direction") {
		module->sceneMode = SCENE_CV_MODE::TRIG_PINGPONG;
		module->sceneCount = 4;
		module->sceneCvModeDir = 1;
		module->sceneSet(3); // At last
		initializeInputs();
		triggerCv(200);
		REQUIRE(module->sceneSelected == 3);
		REQUIRE(module->sceneCvModeDir == -1); // Direction reversed
		triggerCv(300);
		REQUIRE(module->sceneSelected == 2);
	}

	SECTION("TRIG_ALT reset goes to scene 0 and resets direction and alt") {
		module->sceneMode = SCENE_CV_MODE::TRIG_ALT;
		module->sceneCount = 4;
		module->sceneCvModeDir = -1;
		module->sceneCvModeAlt = 2;
		module->sceneSet(3);
		initializeInputs();
		triggerReset(200);
		REQUIRE(module->sceneSelected == 0);
		REQUIRE(module->sceneCvModeDir == 1);
		REQUIRE(module->sceneCvModeAlt == 0);
	}

	SECTION("TRIG_ALT alternates between first and advancing secondary") {
		module->sceneMode = SCENE_CV_MODE::TRIG_ALT;
		module->sceneCount = 4;
		module->sceneCvModeDir = 1;
		module->sceneCvModeAlt = 2; // secondary at first
		module->sceneSet(0); // Start at first
		initializeInputs();

		// First trigger: at first (0), advance secondary.
		// alt=2, dir=1 → s = 2+1 = 3. Since 3 >= sceneCount-1=3, dir flips to -1. alt→3.
		triggerCv(200);
		int secondary = module->sceneSelected;
		REQUIRE(secondary == 3);
		REQUIRE(module->sceneCvModeDir == -1);
		REQUIRE(module->sceneCvModeAlt == 3);

		// Second trigger: not at first, return to first
		triggerCv(300);
		REQUIRE(module->sceneSelected == 0);
	}

	SECTION("TRIG_RANDOM reset has no effect") {
		module->sceneMode = SCENE_CV_MODE::TRIG_RANDOM;
		module->sceneCount = 4;
		module->sceneSet(3);
		initializeInputs();
		triggerReset(200);
		REQUIRE(module->sceneSelected == 3);
	}

	SECTION("TRIG_RANDOM selection stays within boundaries") {
		module->sceneMode = SCENE_CV_MODE::TRIG_RANDOM;
		module->sceneCount = 4;
		module->sceneSet(0);
		initializeInputs();

		for (int i = 0; i < 50; i++) {
			triggerCv(200 + i * 100);
			REQUIRE(module->sceneSelected >= 0);
			REQUIRE(module->sceneSelected < 4);
		}
	}

	SECTION("TRIG_RANDOM_WO_REPEAT reset has no effect") {
		module->sceneMode = SCENE_CV_MODE::TRIG_RANDOM_WO_REPEAT;
		module->sceneCount = 4;
		module->sceneSet(3);
		initializeInputs();
		triggerReset(200);
		REQUIRE(module->sceneSelected == 3);
	}

	SECTION("TRIG_RANDOM_WO_REPEAT never selects same scene twice") {
		module->sceneMode = SCENE_CV_MODE::TRIG_RANDOM_WO_REPEAT;
		module->sceneCount = 4;
		module->sceneSet(2);
		initializeInputs();

		int prevScene = module->sceneSelected;
		for (int i = 0; i < 60; i++) {
			triggerCv(200 + i * 100);
			REQUIRE((module->sceneSelected != prevScene || module->sceneCount <= 1));
			REQUIRE(module->sceneSelected >= 0);
			REQUIRE(module->sceneSelected < 4);
			prevScene = module->sceneSelected;
		}
	}

	SECTION("TRIG_RANDOM_WALK reset has no effect") {
		module->sceneMode = SCENE_CV_MODE::TRIG_RANDOM_WALK;
		module->sceneCount = 4;
		module->sceneSet(3);
		initializeInputs();
		triggerReset(200);
		REQUIRE(module->sceneSelected == 3);
	}

	SECTION("TRIG_RANDOM_WALK steps up or down by 1") {
		module->sceneMode = SCENE_CV_MODE::TRIG_RANDOM_WALK;
		module->sceneCount = 4;
		module->sceneSet(1);
		initializeInputs();
		triggerCv(200);
		// Should step up or down by 1
		REQUIRE((module->sceneSelected == 0 || module->sceneSelected == 2));
	}

	SECTION("TRIG_SHUFFLE reset reshuffles and selects within range") {
		module->sceneMode = SCENE_CV_MODE::TRIG_SHUFFLE;
		module->sceneCount = 4;
		initializeInputs();

		// Reset initializes the shuffle
		triggerReset(5);
		REQUIRE(module->sceneSelected >= 0);
		REQUIRE(module->sceneSelected < 4);

		// Second reset re-shuffles
		triggerReset(100);
		REQUIRE(module->sceneSelected >= 0);
		REQUIRE(module->sceneSelected < 4);
	}

	SECTION("TRIG_SHUFFLE visits all scenes before repeating") {
		module->sceneMode = SCENE_CV_MODE::TRIG_SHUFFLE;
		module->sceneCount = 4;
		initializeInputs();

		// Reset to initialize shuffle
		triggerReset(5);

		std::set<int> visited;
		visited.insert(module->sceneSelected);

		// Trigger remaining 3 times to complete a full cycle of 4 scenes
		for (int i = 0; i < 3; i++) {
			triggerCv(100 + i * 50);
			visited.insert(module->sceneSelected);
		}

		// All 4 scenes in range must have been visited
		REQUIRE(visited.size() == 4);
		for (int s : visited) {
			REQUIRE(s >= 0);
			REQUIRE(s < 4);
		}
	}

	SECTION("ARM mode loads queued scene on trigger") {
		module->sceneMode = SCENE_CV_MODE::ARM;
		module->sceneCount = 4;
		module->inputs[IntermixModule<8>::INPUT_RESET].channels = 1;
		module->inputs[IntermixModule<8>::INPUT_SCENE].channels = 1;
		module->sceneSet(0);

		// Queue scene 2
		module->sceneNext = 2;
		initializeInputs();

		// Trigger should load the queued scene
		triggerCv(200);
		REQUIRE(module->sceneSelected == 2);
	}

	Test::destroyModule(module);
}

TEST_CASE("Scene CV modes voltage-based", "[Intermix]") {
	auto module = Test::createModule<IntermixModule<8>>("Intermix");

	SECTION("VOLT mode maps voltage to scene") {
		module->sceneMode = SCENE_CV_MODE::VOLT;
		module->sceneCount = 8;

		module->inputs[IntermixModule<8>::INPUT_SCENE].channels = 1;

		module->inputs[IntermixModule<8>::INPUT_SCENE].setVoltage(0.f);
		module->process(Test::makeProcessArgs(1));
		REQUIRE(module->sceneSelected == 0);

		module->inputs[IntermixModule<8>::INPUT_SCENE].setVoltage(5.f);
		module->process(Test::makeProcessArgs(2));
		// 5V (50% of 10V) maps to floor(rescale(5, 0, 10, 0, 7.999)) = floor(3.999) = 3
		REQUIRE(module->sceneSelected == 3);

		module->inputs[IntermixModule<8>::INPUT_SCENE].setVoltage(10.f);
		module->process(Test::makeProcessArgs(3));
		REQUIRE(module->sceneSelected == 7);
	}

	SECTION("C4 mode maps CV to scene") {
		module->sceneMode = SCENE_CV_MODE::C4;
		module->sceneCount = 8;

		module->inputs[IntermixModule<8>::INPUT_SCENE].channels = 1;

		module->inputs[IntermixModule<8>::INPUT_SCENE].setVoltage(0.f); // C4 = 0V
		module->process(Test::makeProcessArgs(1));
		REQUIRE(module->sceneSelected == 0);

		module->inputs[IntermixModule<8>::INPUT_SCENE].setVoltage(1.f); // 1V * 12 = 12, clamped to 7
		module->process(Test::makeProcessArgs(2));
		REQUIRE(module->sceneSelected == 7);
	}

	Test::destroyModule(module);
}