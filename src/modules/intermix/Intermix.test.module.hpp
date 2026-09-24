// INTERMIX test cases. Included by Intermix.test.cpp inside namespace __module.
// Not a standalone header: Intermix.test.hpp supplies everything these cases use.

TEST_CASE("Construction and initialization", "[Intermix]") {
	Test::ModuleScaffold<IntermixModule<8>> mods;
	IntermixModule<8>* m = mods.create("Intermix");
	IntermixWidget* mw = Test::createWidget<IntermixWidget>("Intermix");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	SECTION("Matrix pad drag start is safe on a module-less widget") {
		// A module-less widget (e.g. the module browser preview) has no
		// ParamQuantity behind its ParamWidgets. onDragStart() must not
		// dereference it unconditionally.
		ParamWidget* pad = mw->getParam(IntermixModule<8>::PARAM_MATRIX);
		REQUIRE(pad != nullptr);

		event::DragStart e;
		CHECK_NOTHROW(pad->onDragStart(e));
	}

	Test::destroyWidget(mw);
}

TEST_CASE("Preset JSON null-guards", "[Intermix][JSON]") {
	Test::ModuleScaffold<IntermixModule<8>> mods;
	auto module = mods.create("Intermix");

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

TEST_CASE("Preset JSON clamps out-of-range scalars", "[Intermix][JSON]") {
	Test::Harness h;
	auto module = h.addModule<IntermixModule<8>>("Intermix");

	json_t* rootJ = module->dataToJson();
	REQUIRE(rootJ != nullptr);
	Test::testPresetOutOfRangeScalars(h, module, rootJ);
	json_decref(rootJ);

	REQUIRE(module->sceneSelected >= 0);
	REQUIRE(module->sceneSelected < SCENE_MAX);
	REQUIRE(module->channelCount >= 1);
	REQUIRE(module->channelCount <= PORT_MAX_CHANNELS);
	REQUIRE(module->sceneCount >= 1);
	REQUIRE(module->sceneCount <= SCENE_MAX);
}

TEST_CASE("JSON round-trip preserves state", "[Intermix][JSON]") {
	Test::ModuleScaffold<IntermixModule<8>> mods;
	IntermixModule<8>* m = mods.create("Intermix");

	// Distinctive values across all 8 input modes
	for (int i = 0; i < 8; i++) {
		m->inputMode[i] = (i % 2 == 0) ? IM_DIRECT : IM_FADE;
	}
	
	// Distinctive values across all 8 scenes
	for (int s = 0; s < 8; s++) {
		for (int i = 0; i < 8; i++) {
			m->scenes[s].input[i] = (i % 2 == 0) ? IM_DIRECT : IM_FADE;
			m->scenes[s].output[i] = (i % 3 == 0) ? OM_OFF : OM_OUT;
			m->scenes[s].outputAt[i] = 0.1f * s + 0.01f * i;
			m->scenes[s].matrix[i][i] = 0.1f * s + 0.01f * i;
		}
	}

	json_t* j = m->dataToJson();

	IntermixModule<8>* m2 = mods.create("Intermix");
	m2->dataFromJson(j);
	json_decref(j);

	for (int i = 0; i < 8; i++) {
		REQUIRE(m2->inputMode[i] == ((i % 2 == 0) ? IM_DIRECT : IM_FADE));
	}

	for (int s = 0; s < 8; s++) {
		for (int i = 0; i < 8; i++) {
			REQUIRE(m2->scenes[s].input[i] == ((i % 2 == 0) ? IM_DIRECT : IM_FADE));
			REQUIRE(m2->scenes[s].output[i] == ((i % 3 == 0) ? OM_OFF : OM_OUT));
			REQUIRE(m2->scenes[s].outputAt[i] == Catch::Approx(0.1f * s + 0.01f * i).margin(0.01f));
			REQUIRE(m2->scenes[s].matrix[i][i] == Catch::Approx(0.1f * s + 0.01f * i).margin(0.01f));
		}
	}
}


TEST_CASE("Scene selection", "[Intermix]") {
	Test::ModuleScaffold<IntermixModule<8>> mods;
	auto module = mods.create("Intermix");

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
}

TEST_CASE("Scene copy", "[Intermix]") {
	Test::ModuleScaffold<IntermixModule<8>> mods;
	auto module = mods.create("Intermix");

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

	SECTION("sceneCopy survives a corrupted sceneSelected recovered via JSON clamping") {
		// sceneSelected only stays in [0, SCENE_MAX) because dataFromJson() clamps
		// it on load; sceneCopy() itself trusts it unconditionally as a source
		// index. Pin the interaction: a preset that once carried an out-of-range
		// sceneSelected must not leave sceneCopy() reading out of bounds afterwards.
		json_t* rootJ = module->dataToJson();
		json_object_set_new(rootJ, "sceneSelected", json_integer(4000));
		module->dataFromJson(rootJ);
		json_decref(rootJ);

		REQUIRE(module->sceneSelected >= 0);
		REQUIRE(module->sceneSelected < SCENE_MAX);

		module->scenes[module->sceneSelected].matrix[0][0] = 0.75f;
		module->sceneCopy(1);
		REQUIRE(module->scenes[1].matrix[0][0] == 0.75f);
	}
}

TEST_CASE("Scene reset", "[Intermix]") {
	Test::ModuleScaffold<IntermixModule<8>> mods;
	auto module = mods.create("Intermix");

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
}

TEST_CASE("Scene count", "[Intermix]") {
	Test::ModuleScaffold<IntermixModule<8>> mods;
	auto module = mods.create("Intermix");

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
}

TEST_CASE("Matrix processing", "[Intermix]") {
	Test::Harness h;
	auto module = h.addModule<IntermixModule<8>>("Intermix");

	SECTION("Matrix button changes matrix value") {
		module->params[IntermixModule<8>::PARAM_MATRIX + 0].setValue(1.f);
		h.dspStep();

		// Process multiple times to allow divider to update
		h.dspSteps(100);

		REQUIRE(module->scenes[0].matrix[0][0] == 1.f);
	}
}

TEST_CASE("Output processing", "[Intermix]") {
	Test::Harness h;
	auto module = h.addModule<IntermixModule<8>>("Intermix");

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
		h.dspSteps(130);
		
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
		
		h.dspSteps(130);
		
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
		
		h.dspSteps(130);
		
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
		
		h.dspSteps(130);
		
		// 4V * 1.0 * 0.5 = 2V
		REQUIRE(module->outputs[IntermixModule<8>::OUTPUT + 0].getVoltage() == Catch::Approx(2.f).margin(0.01f));
	}
}

TEST_CASE("Input modes", "[Intermix]") {
	Test::Harness h;
	auto module = h.addModule<IntermixModule<8>>("Intermix");

	SECTION("Off mode produces no output") {
		module->inputMode[0] = IM_OFF;
		module->params[IntermixModule<8>::PARAM_MATRIX + 0].setValue(1.f);
		module->params[IntermixModule<8>::PARAM_OUTPUT + 0].setValue(0.f);
		module->channelCount = 1;
		
		module->inputs[IntermixModule<8>::INPUT + 0].channels = 1;
		module->inputs[IntermixModule<8>::INPUT + 0].setVoltage(5.f);
		
		h.dspSteps(130);
		
		REQUIRE(module->outputs[IntermixModule<8>::OUTPUT + 0].getVoltage() == 0.f);
	}

	SECTION("Constant voltage mode") {
		module->inputMode[0] = IM_ADD_01C; // +1 cent = +1/12V
		module->params[IntermixModule<8>::PARAM_MATRIX + 0].setValue(1.f);
		module->params[IntermixModule<8>::PARAM_OUTPUT + 0].setValue(0.f);
		module->channelCount = 1;

		h.dspSteps(130);

		float expected = 1.f / 12.f;
		REQUIRE(module->outputs[IntermixModule<8>::OUTPUT + 0].getVoltage() == Catch::Approx(expected).margin(0.001f));
	}

	SECTION("Constant voltage mode with subtract") {
		module->inputMode[0] = IM_SUB_05C; // -5 cents = -5/12V
		module->params[IntermixModule<8>::PARAM_MATRIX + 0].setValue(1.f);
		module->params[IntermixModule<8>::PARAM_OUTPUT + 0].setValue(0.f);
		module->channelCount = 1;

		h.dspSteps(130);

		float expected = -5.f / 12.f;
		REQUIRE(module->outputs[IntermixModule<8>::OUTPUT + 0].getVoltage() == Catch::Approx(expected).margin(0.001f));
	}
}

TEST_CASE("centsOf and isValidInMode", "[Intermix]") {
	SECTION("centsOf decodes the full subtract/add range") {
		REQUIRE(centsOf(IM_SUB_12C) == -12);
		REQUIRE(centsOf(IM_SUB_01C) == -1);
		REQUIRE(centsOf(IM_ADD_01C) == 1);
		REQUIRE(centsOf(IM_ADD_12C) == 12);
	}

	SECTION("isValidInMode accepts every named enumerator") {
		REQUIRE(isValidInMode(IM_OFF));
		REQUIRE(isValidInMode(IM_DIRECT));
		REQUIRE(isValidInMode(IM_FADE));
		for (int m = IM_SUB_12C; m <= IM_SUB_01C; m++) {
			REQUIRE(isValidInMode(m));
		}
		for (int m = IM_ADD_01C; m <= IM_ADD_12C; m++) {
			REQUIRE(isValidInMode(m));
		}
	}

	SECTION("isValidInMode rejects IM_CONST_ZERO and out-of-range values") {
		REQUIRE_FALSE(isValidInMode(IM_CONST_ZERO));
		REQUIRE_FALSE(isValidInMode(-1));
		REQUIRE_FALSE(isValidInMode(3));
		REQUIRE_FALSE(isValidInMode(11));
		REQUIRE_FALSE(isValidInMode(37));
		REQUIRE_FALSE(isValidInMode(4000));
	}
}

TEST_CASE("Preset JSON rejects invalid IN_MODE values", "[Intermix][JSON]") {
	// dataFromJson() casts JSON integers straight into IN_MODE; a corrupt or
	// hand-edited preset must fall back to IM_DIRECT rather than hitting the
	// DSP switch's default: branch with an arbitrary, unvalidated mode.
	Test::ModuleScaffold<IntermixModule<8>> mods;
	auto module = mods.create("Intermix");

	SECTION("inputMode falls back to IM_DIRECT") {
		json_t* rootJ = module->dataToJson();
		json_t* inputsJ = json_object_get(rootJ, "inputMode");
		json_array_set_new(inputsJ, 0, json_integer(IM_CONST_ZERO));
		json_array_set_new(inputsJ, 1, json_integer(4000));
		module->dataFromJson(rootJ);
		json_decref(rootJ);

		REQUIRE(module->inputMode[0] == IM_DIRECT);
		REQUIRE(module->inputMode[1] == IM_DIRECT);
	}

	SECTION("scenes[i].input falls back to IM_DIRECT") {
		json_t* rootJ = module->dataToJson();
		json_t* scenesJ = json_object_get(rootJ, "scenes");
		json_t* sceneJ = json_array_get(scenesJ, 0);
		json_t* inputJ = json_object_get(sceneJ, "input");
		json_array_set_new(inputJ, 0, json_integer(IM_CONST_ZERO));
		json_array_set_new(inputJ, 1, json_integer(-1));
		module->dataFromJson(rootJ);
		json_decref(rootJ);

		REQUIRE(module->scenes[0].input[0] == IM_DIRECT);
		REQUIRE(module->scenes[0].input[1] == IM_DIRECT);
	}
}

TEST_CASE("Scene CV modes basic", "[Intermix]") {
	Test::Harness h;
	auto module = h.addModule<IntermixModule<8>>("Intermix");

	SECTION("Trigger forward mode") {
		module->sceneMode = SCENE_CV_MODE::TRIG_FWD;
		module->sceneCount = 8;
		module->sceneSet(0);

		// Connect the input
		module->inputs[IntermixModule<8>::INPUT_SCENE].channels = 1;

		// Accumulate resetTimer cooldown (>1ms at 44100Hz)
		h.dspSteps(100);

		// Send trigger (low to high)
		module->inputs[IntermixModule<8>::INPUT_SCENE].setVoltage(0.f);
		h.dspStep();

		module->inputs[IntermixModule<8>::INPUT_SCENE].setVoltage(10.f);
		h.dspStep();

		// After one trigger, should advance from 0 to 1
		REQUIRE(module->sceneSelected == 1);
	}

	SECTION("Voltage mode 0-10V") {
		module->sceneMode = SCENE_CV_MODE::VOLT;
		module->sceneCount = 8;

		module->inputs[IntermixModule<8>::INPUT_SCENE].channels = 1;
		module->inputs[IntermixModule<8>::INPUT_SCENE].setVoltage(5.f);
		h.dspStep();

		// 5V (50% of 10V) maps to floor(rescale(5, 0, 10, 0, 7.999)) = floor(3.999) = 3
		REQUIRE(module->sceneSelected == 3);
	}

	SECTION("C4 mode") {
		module->sceneMode = SCENE_CV_MODE::C4;
		module->sceneCount = 8;

		module->inputs[IntermixModule<8>::INPUT_SCENE].channels = 1;
		module->inputs[IntermixModule<8>::INPUT_SCENE].setVoltage(2.f / 12.f); // 2 semitones
		h.dspStep();

		REQUIRE(module->sceneSelected == 2);
	}
}

TEST_CASE("X/Y map buttons toggle every selected column", "[Intermix]") {
	// mapTrigger[j] must be evaluated once per row and applied to every selected
	// X column, not re-evaluated per column: a SchmittTrigger only fires once per
	// rising edge, so calling process() a second time for a second selected
	// column would see an already-consumed edge and silently no-op.
	Test::Harness h;
	auto module = h.addModule<IntermixModule<8>>("Intermix");

	module->params[IntermixModule<8>::PARAM_X_MAP + 0].setValue(1.f);
	module->params[IntermixModule<8>::PARAM_X_MAP + 1].setValue(1.f);
	module->params[IntermixModule<8>::PARAM_Y_MAP + 0].setValue(0.f);
	// Clear the SchmittTrigger's initial UNINITIALIZED state before arming the edge.
	h.dspSteps(100);

	bool before0 = module->params[IntermixModule<8>::PARAM_MATRIX + 0 * 8 + 0].getValue() > 0.f;
	bool before1 = module->params[IntermixModule<8>::PARAM_MATRIX + 0 * 8 + 1].getValue() > 0.f;

	module->params[IntermixModule<8>::PARAM_Y_MAP + 0].setValue(1.f);
	h.dspSteps(100);

	bool after0 = module->params[IntermixModule<8>::PARAM_MATRIX + 0 * 8 + 0].getValue() > 0.f;
	bool after1 = module->params[IntermixModule<8>::PARAM_MATRIX + 0 * 8 + 1].getValue() > 0.f;

	REQUIRE(after0 != before0);
	REQUIRE(after1 != before1);
}

TEST_CASE("IntermixCv expander overrides pad values on its selected row", "[Intermix][IntermixCv]") {
	Test::Harness h;
	auto module = h.addModule<IntermixModule<8>>("Intermix");
	auto cv = h.addModule<IntermixCvModule<8>>("IntermixCv");
	h.connectExpander(module, cv);

	module->inputMode[0] = IM_DIRECT;
	module->params[IntermixModule<8>::PARAM_OUTPUT + 0].setValue(0.f);
	module->params[IntermixModule<8>::PARAM_OUTPUT + 1].setValue(0.f);
	module->params[IntermixModule<8>::PARAM_AT + 0].setValue(1.f);
	module->params[IntermixModule<8>::PARAM_AT + 1].setValue(1.f);
	module->channelCount = 1;
	module->inputs[IntermixModule<8>::INPUT + 0].channels = 1;
	module->inputs[IntermixModule<8>::INPUT + 0].setVoltage(10.f);

	cv->input = 0;

	SECTION("Connected CV input overrides the pad button's 0/1 value") {
		// Pad button is off (0), but a fully-patched CV input demands 1.
		module->params[IntermixModule<8>::PARAM_MATRIX + 0 * 8 + 0].setValue(0.f);
		cv->inputs[IntermixCvModule<8>::INPUT_CV + 0].channels = 1;
		cv->inputs[IntermixCvModule<8>::INPUT_CV + 0].setVoltage(10.f);

		h.dspSteps(130);

		REQUIRE(module->outputs[IntermixModule<8>::OUTPUT + 0].getVoltage() == Catch::Approx(10.f).margin(0.01f));
	}

	SECTION("0..10V maps to a pad value of 0..1") {
		module->params[IntermixModule<8>::PARAM_MATRIX + 0 * 8 + 0].setValue(0.f);
		cv->inputs[IntermixCvModule<8>::INPUT_CV + 0].channels = 1;
		cv->inputs[IntermixCvModule<8>::INPUT_CV + 0].setVoltage(5.f);

		h.dspSteps(130);

		REQUIRE(module->outputs[IntermixModule<8>::OUTPUT + 0].getVoltage() == Catch::Approx(5.f).margin(0.01f));
	}

	SECTION("CV voltage is clamped to 0..10V") {
		module->params[IntermixModule<8>::PARAM_MATRIX + 0 * 8 + 0].setValue(0.f);
		cv->inputs[IntermixCvModule<8>::INPUT_CV + 0].channels = 1;
		cv->inputs[IntermixCvModule<8>::INPUT_CV + 0].setVoltage(-5.f);

		h.dspSteps(130);

		REQUIRE(module->outputs[IntermixModule<8>::OUTPUT + 0].getVoltage() == Catch::Approx(0.f).margin(0.01f));
	}

	SECTION("Disconnected CV input on the selected row leaves the pad button in control") {
		// Row 0 (targeted by the CV expander), column 1: CV input 1 is left
		// disconnected, so the button (on) still applies. PARAM_MATRIX indexes
		// as [j * PORTS + i] for column j, row i (see Intermix.cpp process()).
		module->params[IntermixModule<8>::PARAM_MATRIX + 1 * 8 + 0].setValue(1.f);

		h.dspSteps(130);

		REQUIRE(module->outputs[IntermixModule<8>::OUTPUT + 1].getVoltage() == Catch::Approx(10.f).margin(0.01f));
	}

	SECTION("CV only affects the row it targets") {
		// input=0 targets row 0. Feed row 1 into a different column (1) with
		// its button off; if CV leaked into row 1 it would turn column 1 on
		// too, even though only row 0's CV input is patched.
		module->params[IntermixModule<8>::PARAM_MATRIX + 0 * 8 + 0].setValue(0.f);
		module->inputMode[1] = IM_DIRECT;
		module->inputs[IntermixModule<8>::INPUT + 1].channels = 1;
		module->inputs[IntermixModule<8>::INPUT + 1].setVoltage(10.f);
		module->params[IntermixModule<8>::PARAM_MATRIX + 1 * 8 + 1].setValue(0.f);
		cv->inputs[IntermixCvModule<8>::INPUT_CV + 0].channels = 1;
		cv->inputs[IntermixCvModule<8>::INPUT_CV + 0].setVoltage(10.f);

		h.dspSteps(130);

		// Row 0 -> column 0 is forced to 1 by CV, confirming CV took effect.
		REQUIRE(module->outputs[IntermixModule<8>::OUTPUT + 0].getVoltage() == Catch::Approx(10.f).margin(0.01f));
		// Row 1 -> column 1's button is off and CV does not target row 1, so
		// column 1 must stay silent.
		REQUIRE(module->outputs[IntermixModule<8>::OUTPUT + 1].getVoltage() == Catch::Approx(0.f).margin(0.01f));
	}
}

TEST_CASE("IntermixCv removal falls back to the pad button", "[Intermix][IntermixCv]") {
	Test::Harness h;
	auto module = h.addModule<IntermixModule<8>>("Intermix");
	auto cv = h.addModule<IntermixCvModule<8>>("IntermixCv");
	h.connectExpander(module, cv);

	module->inputMode[0] = IM_DIRECT;
	module->params[IntermixModule<8>::PARAM_MATRIX + 0].setValue(0.f);
	module->params[IntermixModule<8>::PARAM_OUTPUT + 0].setValue(0.f);
	module->params[IntermixModule<8>::PARAM_AT + 0].setValue(1.f);
	module->channelCount = 1;
	module->inputs[IntermixModule<8>::INPUT + 0].channels = 1;
	module->inputs[IntermixModule<8>::INPUT + 0].setVoltage(10.f);

	cv->input = 0;
	cv->inputs[IntermixCvModule<8>::INPUT_CV + 0].channels = 1;
	cv->inputs[IntermixCvModule<8>::INPUT_CV + 0].setVoltage(10.f);
	h.dspSteps(130);
	REQUIRE(module->outputs[IntermixModule<8>::OUTPUT + 0].getVoltage() == Catch::Approx(10.f).margin(0.01f));

	h.disconnectExpander(module, Test::Harness::SIDE_RIGHT);
	h.dspSteps(130);

	// The button (still 0) is back in control now that the CV expander is gone.
	REQUIRE(module->outputs[IntermixModule<8>::OUTPUT + 0].getVoltage() == Catch::Approx(0.f).margin(0.01f));
}

TEST_CASE("IntermixCv reassigned to a different row live takes effect without an expander topology change", "[Intermix][IntermixCv]") {
	// PARAM_MATRIX indexes as [j * PORTS + i] for column j (-> output j), row
	// i (<- input i). CV's column-0 input stays patched throughout; only the
	// row it targets changes. Output 0 (fed by column 0) reflects whichever
	// row currently owns that column's override — pure output-voltage
	// behavior, no internal state involved.
	Test::Harness h;
	auto module = h.addModule<IntermixModule<8>>("Intermix");
	auto cv = h.addModule<IntermixCvModule<8>>("IntermixCv");
	h.connectExpander(module, cv);

	module->inputMode[0] = IM_DIRECT;
	module->inputMode[1] = IM_DIRECT;
	module->params[IntermixModule<8>::PARAM_MATRIX + 0 * 8 + 0].setValue(0.f);
	module->params[IntermixModule<8>::PARAM_MATRIX + 0 * 8 + 1].setValue(0.f);
	module->params[IntermixModule<8>::PARAM_OUTPUT + 0].setValue(0.f);
	module->params[IntermixModule<8>::PARAM_AT + 0].setValue(1.f);
	module->channelCount = 1;
	// Row 0 and row 1 carry different voltages so which one is currently
	// reaching output 0 is unambiguous.
	module->inputs[IntermixModule<8>::INPUT + 0].channels = 1;
	module->inputs[IntermixModule<8>::INPUT + 0].setVoltage(10.f);
	module->inputs[IntermixModule<8>::INPUT + 1].channels = 1;
	module->inputs[IntermixModule<8>::INPUT + 1].setVoltage(6.f);

	cv->input = 0;
	cv->inputs[IntermixCvModule<8>::INPUT_CV + 0].channels = 1;
	cv->inputs[IntermixCvModule<8>::INPUT_CV + 0].setVoltage(10.f);
	h.dspSteps(130);
	// CV overrides row 0's column-0 pad: output 0 sees row 0's 10V input.
	REQUIRE(module->outputs[IntermixModule<8>::OUTPUT + 0].getVoltage() == Catch::Approx(10.f).margin(0.01f));

	// Retarget the same expander to row 1 without touching the chain topology.
	cv->input = 1;
	h.dspSteps(200);

	// CV now overrides row 1's column-0 pad instead: row 0's button (off)
	// takes row 0 back out of the mix, while row 1 is now forced on, so
	// output 0 switches to row 1's 6V input.
	REQUIRE(module->outputs[IntermixModule<8>::OUTPUT + 0].getVoltage() == Catch::Approx(6.f).margin(0.01f));
}

TEST_CASE("Expander interface", "[Intermix]") {
	Test::Harness h;
	auto module = h.addModule<IntermixModule<8>>("Intermix");

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
		h.dspStep();
		uint32_t tsBase = module->ts;
		
		module->expSetFade(0, fadeIn, fadeOut);
		
		// Check that timestamps are updated (they're set to current ts)
		REQUIRE(module->fadeInTs[0] == tsBase);
		REQUIRE(module->fadeOutTs[0] == tsBase);
	}

	SECTION("expSetFade ignores out-of-range rows") {
		// expSetFade() is a public virtual any expander can call; the row index
		// must be validated at the receiving end rather than trusted from callers.
		float v[8] = {1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f};
		CHECK_NOTHROW(module->expSetFade(-1, v, v));
		CHECK_NOTHROW(module->expSetFade(8, v, v));
		CHECK_NOTHROW(module->expSetFade(5000, v, v));
	}
}

TEST_CASE("JSON serialization", "[Intermix]") {
	Test::ModuleScaffold<IntermixModule<8>> mods;
	auto module = mods.create("Intermix");

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
		
		auto moduleNew = mods.create("Intermix");
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
	}
}


TEST_CASE("Fade time: PARAM_FADEIN sets fader rise to param seconds", "[Intermix]") {
	Test::Harness h;
	// FadeLengthParamQuantity::getMaxValue() overrides the knob range to [0, maxFade],
	// so getValue() already returns seconds. Multiplying by getFadeLengthMax() again
	// gives param_seconds * maxFade (e.g. a 2s setting in 4s-mode becomes 8s).

	auto m = h.addModule<IntermixModule<8>>("Intermix");
	m->channelCount = 1;

	SECTION("4s mode: PARAM_FADEIN of 2s gives fader rise of 2s") {
		m->fadeLengthMode = FADE_LENGTH_4S;
		m->params[IntermixModule<8>::PARAM_FADEIN].setValue(2.0f);
		h.dspSteps(250);
		// Bug: fader.rise == 2.0 * 4 = 8.0. Correct: 2.0.
		REQUIRE(m->fader[0][0][0].rise == Catch::Approx(2.0f).margin(0.001f));
	}

	SECTION("15s mode: PARAM_FADEIN of 5s gives fader rise of 5s") {
		m->fadeLengthMode = FADE_LENGTH_15S;
		m->params[IntermixModule<8>::PARAM_FADEIN].setValue(5.0f);
		h.dspSteps(250);
		// Bug: fader.rise == 5.0 * 15 = 75.0. Correct: 5.0.
		REQUIRE(m->fader[0][0][0].rise == Catch::Approx(5.0f).margin(0.001f));
	}

	SECTION("60s mode: PARAM_FADEIN of 10s gives fader rise of 10s") {
		m->fadeLengthMode = FADE_LENGTH_60S;
		m->params[IntermixModule<8>::PARAM_FADEIN].setValue(10.0f);
		h.dspSteps(250);
		// Bug: fader.rise == 10.0 * 60 = 600.0. Correct: 10.0.
		REQUIRE(m->fader[0][0][0].rise == Catch::Approx(10.0f).margin(0.001f));
	}

	SECTION("4s mode: PARAM_FADEOUT of 3s gives fader fall of 3s") {
		m->fadeLengthMode = FADE_LENGTH_4S;
		m->params[IntermixModule<8>::PARAM_FADEOUT].setValue(3.0f);
		h.dspSteps(250);
		// Bug: fader.fall == 3.0 * 4 = 12.0. Correct: 3.0.
		REQUIRE(m->fader[0][0][0].fall == Catch::Approx(3.0f).margin(0.001f));
	}
}


TEST_CASE("FadeLengthParamQuantity setValue clamps and reaches the full range per mode", "[Intermix]") {
	// PARAM_FADEIN/PARAM_FADEOUT are configured with configParam(..., 0.f, 4.f, ...),
	// but ParamQuantity::setValue()/setImmediateValue() (the path real UI interactions
	// use) clamp against the virtual getMaxValue() instead, which tracks fadeLengthMode.
	// So the configParam() literal does not cap the reachable range.
	Test::ModuleScaffold<IntermixModule<8>> mods;
	auto m = mods.create("Intermix");
	auto* pq = m->paramQuantities[IntermixModule<8>::PARAM_FADEIN];

	SECTION("FADE_LENGTH_4S reaches 4s and clamps above it") {
		m->fadeLengthMode = FADE_LENGTH_4S;
		pq->setValue(100.f);
		REQUIRE(pq->getValue() == Catch::Approx(4.0f).margin(0.001f));
	}

	SECTION("FADE_LENGTH_15S reaches 15s, above the configParam() literal of 4") {
		m->fadeLengthMode = FADE_LENGTH_15S;
		pq->setValue(100.f);
		REQUIRE(pq->getValue() == Catch::Approx(15.0f).margin(0.001f));
	}

	SECTION("FADE_LENGTH_60S reaches the full 60s, above the configParam() literal of 4") {
		m->fadeLengthMode = FADE_LENGTH_60S;
		pq->setValue(50.f);
		REQUIRE(pq->getValue() == Catch::Approx(50.0f).margin(0.001f));
	}
}


TEST_CASE("Data race: expSetFade and process() share fader state without synchronization", "[Intermix]") {
	Test::Harness h;
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

	auto m = h.addModule<IntermixModule<8>>("Intermix");
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
		h.dspSteps(250);

		// Main has overwritten expander's 3s. Both paths touch the same fader.rise
		// without any lock — the data race.
		REQUIRE(m->fader[0][0][0].rise != Catch::Approx(3.0f).margin(0.001f));
	}

	SECTION("guard uses plain uint32_t ts arithmetic, with no atomic fence between writers") {
		// Record ts at the moment expSetFade fires (= what gets stored in fadeInTs).
		h.dspStep(); // ts becomes 1
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
		h.dspSteps(250);
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
		h.dspSteps(250);
		// [Order A: expander writes first, then main reads fresh fadeInTs → guard holds]
		m->expSetFade(0, fadeIn, nullptr);           // expander fires: rise=6, fadeInTs=ts
		h.dspStep();       // main sees fresh fadeInTs → guard holds
		REQUIRE(m->fader[0][0][0].rise == Catch::Approx(6.0f).margin(0.001f));

		// [Order B: main reads stale fadeInTs first (guard expired), THEN expander fires]
		// Simulate by manually expiring the guard before the next process() tick.
		m->fadeInTs[0] = 0;                           // revert to expired state
		h.dspStep();        // main sees guard expired → calls setRise(f1)
		// The main's setRise(f1) ran. Now expander would fire:
		m->expSetFade(0, fadeIn, nullptr);             // expander overwrites with 6s
		// In single-threaded (expander runs after main), expander wins the final write.
		// In multi-threaded (concurrent), either value may "win" — that is the race.
		// The test just documents that both writes happen in the same logical tick:
		REQUIRE(m->fader[0][0][0].rise == Catch::Approx(6.0f).margin(0.001f));
	}
}


TEST_CASE("Polyphonic processing", "[Intermix]") {
	Test::Harness h;
	auto module = h.addModule<IntermixModule<8>>("Intermix");

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
		
		h.dspSteps(130);
		
		REQUIRE(module->outputs[IntermixModule<8>::OUTPUT + 0].getVoltage(0) == Catch::Approx(1.f).margin(0.01f));
		REQUIRE(module->outputs[IntermixModule<8>::OUTPUT + 0].getVoltage(1) == Catch::Approx(2.f).margin(0.01f));
		REQUIRE(module->outputs[IntermixModule<8>::OUTPUT + 0].getVoltage(2) == Catch::Approx(3.f).margin(0.01f));
		REQUIRE(module->outputs[IntermixModule<8>::OUTPUT + 0].getVoltage(3) == Catch::Approx(4.f).margin(0.01f));
	}
}


TEST_CASE("Scene CV modes with reset", "[Intermix]") {
	Test::Harness h;
	auto module = h.addModule<IntermixModule<8>>("Intermix");

	// Initialize inputs and accumulate resetTimer > 1ms (similar to Transit pattern)
	auto initializeInputs = [&]() {
		module->inputs[IntermixModule<8>::INPUT_RESET].channels = 1;
		module->inputs[IntermixModule<8>::INPUT_RESET].setVoltage(0.0f);
		module->inputs[IntermixModule<8>::INPUT_SCENE].channels = 1;
		module->inputs[IntermixModule<8>::INPUT_SCENE].setVoltage(0.0f);
		h.dspSteps(100);
	};

	auto triggerCv = [&]() {
		module->inputs[IntermixModule<8>::INPUT_SCENE].setVoltage(10.0f);
		h.dspStep();
		module->inputs[IntermixModule<8>::INPUT_SCENE].setVoltage(0.0f);
		h.dspStep();
	};

	auto triggerReset = [&]() {
		module->inputs[IntermixModule<8>::INPUT_RESET].setVoltage(10.0f);
		h.dspStep();
		module->inputs[IntermixModule<8>::INPUT_RESET].setVoltage(0.0f);
		h.dspStep();
	};

	SECTION("TRIG_FWD reset goes to scene 0") {
		module->sceneMode = SCENE_CV_MODE::TRIG_FWD;
		module->sceneCount = 4;
		module->sceneSet(3);
		initializeInputs();
		triggerReset();
		REQUIRE(module->sceneSelected == 0);
	}

	SECTION("TRIG_FWD trigger advances within boundaries") {
		module->sceneMode = SCENE_CV_MODE::TRIG_FWD;
		module->sceneCount = 4;
		initializeInputs();
		module->sceneSet(0);
		triggerCv();
		REQUIRE(module->sceneSelected == 1);
		triggerCv();
		REQUIRE(module->sceneSelected == 2);
	}

	SECTION("TRIG_FWD trigger wraps from last to first") {
		module->sceneMode = SCENE_CV_MODE::TRIG_FWD;
		module->sceneCount = 4;
		initializeInputs();
		module->sceneSet(3); // At last
		triggerCv();
		REQUIRE(module->sceneSelected == 0); // Wrapped to first
	}

	SECTION("TRIG_REV reset goes to last scene") {
		module->sceneMode = SCENE_CV_MODE::TRIG_REV;
		module->sceneCount = 4;
		module->sceneSet(1);
		initializeInputs();
		triggerReset();
		REQUIRE(module->sceneSelected == 3); // Last scene
	}

	SECTION("TRIG_REV trigger reverses within boundaries") {
		module->sceneMode = SCENE_CV_MODE::TRIG_REV;
		module->sceneCount = 4;
		initializeInputs();
		module->sceneSet(3); // At last
		triggerCv();
		REQUIRE(module->sceneSelected == 2);
		triggerCv();
		REQUIRE(module->sceneSelected == 1);
	}

	SECTION("TRIG_REV trigger wraps from first to last") {
		module->sceneMode = SCENE_CV_MODE::TRIG_REV;
		module->sceneCount = 4;
		initializeInputs();
		module->sceneSet(0); // At first
		triggerCv();
		REQUIRE(module->sceneSelected == 3); // Wrapped to last
	}

	SECTION("TRIG_PINGPONG reset resets direction") {
		module->sceneMode = SCENE_CV_MODE::TRIG_PINGPONG;
		module->sceneCount = 4;
		module->sceneCvModeDir = -1;
		module->sceneSet(2);
		initializeInputs();
		triggerReset();
		REQUIRE(module->sceneSelected == 0);
		REQUIRE(module->sceneCvModeDir == 1); // Direction reset
	}

	SECTION("TRIG_PINGPONG advances forward from first") {
		module->sceneMode = SCENE_CV_MODE::TRIG_PINGPONG;
		module->sceneCount = 4;
		module->sceneCvModeDir = 1;
		module->sceneSet(0);
		initializeInputs();
		triggerCv();
		REQUIRE(module->sceneSelected == 1);
		triggerCv();
		REQUIRE(module->sceneSelected == 2);
	}

	SECTION("TRIG_PINGPONG bounces at last and reverses direction") {
		module->sceneMode = SCENE_CV_MODE::TRIG_PINGPONG;
		module->sceneCount = 4;
		module->sceneCvModeDir = 1;
		module->sceneSet(3); // At last
		initializeInputs();
		triggerCv();
		REQUIRE(module->sceneSelected == 3);
		REQUIRE(module->sceneCvModeDir == -1); // Direction reversed
		triggerCv();
		REQUIRE(module->sceneSelected == 2);
	}

	SECTION("TRIG_ALT reset goes to scene 0 and resets direction and alt") {
		module->sceneMode = SCENE_CV_MODE::TRIG_ALT;
		module->sceneCount = 4;
		module->sceneCvModeDir = -1;
		module->sceneCvModeAlt = 2;
		module->sceneSet(3);
		initializeInputs();
		triggerReset();
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
		triggerCv();
		int secondary = module->sceneSelected;
		REQUIRE(secondary == 3);
		REQUIRE(module->sceneCvModeDir == -1);
		REQUIRE(module->sceneCvModeAlt == 3);

		// Second trigger: not at first, return to first
		triggerCv();
		REQUIRE(module->sceneSelected == 0);
	}

	SECTION("TRIG_RANDOM reset has no effect") {
		module->sceneMode = SCENE_CV_MODE::TRIG_RANDOM;
		module->sceneCount = 4;
		module->sceneSet(3);
		initializeInputs();
		triggerReset();
		REQUIRE(module->sceneSelected == 3);
	}

	SECTION("TRIG_RANDOM selection stays within boundaries") {
		module->sceneMode = SCENE_CV_MODE::TRIG_RANDOM;
		module->sceneCount = 4;
		module->sceneSet(0);
		initializeInputs();

		for (int i = 0; i < 50; i++) {
			triggerCv();
			REQUIRE(module->sceneSelected >= 0);
			REQUIRE(module->sceneSelected < 4);
		}
	}

	SECTION("TRIG_RANDOM_WO_REPEAT reset has no effect") {
		module->sceneMode = SCENE_CV_MODE::TRIG_RANDOM_WO_REPEAT;
		module->sceneCount = 4;
		module->sceneSet(3);
		initializeInputs();
		triggerReset();
		REQUIRE(module->sceneSelected == 3);
	}

	SECTION("TRIG_RANDOM_WO_REPEAT never selects same scene twice") {
		module->sceneMode = SCENE_CV_MODE::TRIG_RANDOM_WO_REPEAT;
		module->sceneCount = 4;
		module->sceneSet(2);
		initializeInputs();

		int prevScene = module->sceneSelected;
		for (int i = 0; i < 60; i++) {
			triggerCv();
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
		triggerReset();
		REQUIRE(module->sceneSelected == 3);
	}

	SECTION("TRIG_RANDOM_WALK steps up or down by 1") {
		module->sceneMode = SCENE_CV_MODE::TRIG_RANDOM_WALK;
		module->sceneCount = 4;
		module->sceneSet(1);
		initializeInputs();
		triggerCv();
		// Should step up or down by 1
		REQUIRE((module->sceneSelected == 0 || module->sceneSelected == 2));
	}

	SECTION("TRIG_SHUFFLE reset reshuffles and selects within range") {
		module->sceneMode = SCENE_CV_MODE::TRIG_SHUFFLE;
		module->sceneCount = 4;
		initializeInputs();

		// Reset initializes the shuffle
		triggerReset();
		REQUIRE(module->sceneSelected >= 0);
		REQUIRE(module->sceneSelected < 4);

		// Second reset re-shuffles
		triggerReset();
		REQUIRE(module->sceneSelected >= 0);
		REQUIRE(module->sceneSelected < 4);
	}

	SECTION("TRIG_SHUFFLE visits all scenes before repeating") {
		module->sceneMode = SCENE_CV_MODE::TRIG_SHUFFLE;
		module->sceneCount = 4;
		initializeInputs();

		// Reset to initialize shuffle
		triggerReset();

		std::set<int> visited;
		visited.insert(module->sceneSelected);

		// Trigger remaining 3 times to complete a full cycle of 4 scenes
		for (int i = 0; i < 3; i++) {
			triggerCv();
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
		triggerCv();
		REQUIRE(module->sceneSelected == 2);
	}

	SECTION("TRIG_PINGPONG walks the full 0..7..0 sequence") {
		module->sceneMode = SCENE_CV_MODE::TRIG_PINGPONG;
		module->sceneCount = 8;
		module->sceneCvModeDir = 1;
		module->sceneSet(0);
		initializeInputs();

		// Expected ping-pong walk: 0,1,2,3,4,5,6,7,6,5,4,3,2,1,0,1,2,...
		// Both endpoints are visited exactly once per traversal (reflect), and
		// the walk reverses symmetrically at 7 and at 0.
		std::vector<int> expected = {1, 2, 3, 4, 5, 6, 7, 6, 5, 4, 3, 2, 1, 0, 1, 2};
		for (size_t i = 0; i < expected.size(); i++) {
			triggerCv();
			REQUIRE(module->sceneSelected == expected[i]);
		}
	}

	SECTION("TRIG_PINGPONG bounces symmetrically at both endpoints") {
		module->sceneMode = SCENE_CV_MODE::TRIG_PINGPONG;
		module->sceneCount = 8;
		// Distinct matrices so we can observe whether the endpoint's routing is
		// re-applied when the walk bounces off an endpoint.
		for (int s = 0; s < 8; s++)
			for (int i = 0; i < 8; i++)
				for (int j = 0; j < 8; j++)
					module->scenes[s].matrix[i][j] = (float)(s * 100 + i * 10 + j);
		initializeInputs();

		// Top bounce: start at last (7), dir = 1 -> stay at 7, reverse direction.
		module->sceneCvModeDir = 1;
		module->sceneSet(7);
		module->params[IntermixModule<8>::PARAM_MATRIX + 0].setValue(999.f); // corrupt
		triggerCv();
		REQUIRE(module->sceneSelected == 7);
		REQUIRE(module->sceneCvModeDir == -1);
		REQUIRE(module->params[IntermixModule<8>::PARAM_MATRIX + 0].getValue() == Catch::Approx(700.f));

		// Bottom bounce: start at first (0), dir = -1 -> stay at 0, reverse direction.
		module->sceneCvModeDir = -1;
		module->sceneSet(0);
		module->params[IntermixModule<8>::PARAM_MATRIX + 0].setValue(999.f); // corrupt
		triggerCv();
		REQUIRE(module->sceneSelected == 0);
		REQUIRE(module->sceneCvModeDir == 1);
		REQUIRE(module->params[IntermixModule<8>::PARAM_MATRIX + 0].getValue() == Catch::Approx(0.f));
	}
}

TEST_CASE("Scene CV modes voltage-based", "[Intermix]") {
	Test::Harness h;
	auto module = h.addModule<IntermixModule<8>>("Intermix");

	SECTION("VOLT mode maps voltage to scene") {
		module->sceneMode = SCENE_CV_MODE::VOLT;
		module->sceneCount = 8;

		module->inputs[IntermixModule<8>::INPUT_SCENE].channels = 1;

		module->inputs[IntermixModule<8>::INPUT_SCENE].setVoltage(0.f);
		h.dspStep();
		REQUIRE(module->sceneSelected == 0);

		module->inputs[IntermixModule<8>::INPUT_SCENE].setVoltage(5.f);
		h.dspStep();
		// 5V (50% of 10V) maps to floor(rescale(5, 0, 10, 0, 7.999)) = floor(3.999) = 3
		REQUIRE(module->sceneSelected == 3);

		module->inputs[IntermixModule<8>::INPUT_SCENE].setVoltage(10.f);
		h.dspStep();
		REQUIRE(module->sceneSelected == 7);
	}

	SECTION("C4 mode maps CV to scene") {
		module->sceneMode = SCENE_CV_MODE::C4;
		module->sceneCount = 8;

		module->inputs[IntermixModule<8>::INPUT_SCENE].channels = 1;

		module->inputs[IntermixModule<8>::INPUT_SCENE].setVoltage(0.f); // C4 = 0V
		h.dspStep();
		REQUIRE(module->sceneSelected == 0);

		module->inputs[IntermixModule<8>::INPUT_SCENE].setVoltage(1.f); // 1V * 12 = 12, clamped to 7
		h.dspStep();
		REQUIRE(module->sceneSelected == 7);
	}
}
