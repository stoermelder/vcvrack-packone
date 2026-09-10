// CVPAM test cases. Included by CVPam.test.cpp inside namespace __module.
// Not a standalone header: CVPam.test.hpp supplies everything these cases use.

TEST_CASE("Construction and initialization", "[CVPam]") {
	Test::ModuleScaffold<CVPamModule> mods;
	CVPamModule* m = mods.create("CVPam");
	CVPamWidget* mw = Test::createWidget<CVPamWidget>("CVPam");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
}

TEST_CASE("Preset JSON null-guards", "[CVPam][JSON]") {
	Test::ModuleScaffold<CVPamModule> mods;
	auto module = mods.create("CVPam");

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

TEST_CASE("JSON round-trip preserves state", "[CVPam][JSON]") {
	Test::ModuleScaffold<CVPamModule> mods;
	CVPamModule* m = mods.create("CVPam");
	CVPamModule* m2 = mods.create("CVPam");

	SECTION("Scalar settings round-trip") {
		// Distinct, non-default values for every CVPam-specific scalar stored to JSON
		m->panelTheme = 1;
		m->bipolarOutput = true;  // default false
		m->audioRate = false;     // default true
		m->locked = true;         // default false

		json_t* j = m->dataToJson();
		m2->dataFromJson(j);
		json_decref(j);

		REQUIRE(m2->panelTheme == 1);
		REQUIRE(m2->bipolarOutput == true);
		REQUIRE(m2->audioRate == false);
		REQUIRE(m2->locked == true);
	}

	SECTION("Mapping slots (maps array) round-trip") {
		// Harness::addModule registers the target with the engine, which a mapping needs:
		// updateParamHandle() resolves the target by id, so an unregistered module would
		// leave moduleId at -1 and the mapping could not round-trip. See the mapping section
		// in test_harness.hpp.
		Test::Harness h;
		rack::Module* target = h.addModule<rack::Module>("Glue");

		// Map three slots to distinctive paramIds on the target module
		m->learnParam(0, target->id, 0);
		m->learnParam(1, target->id, 2);
		m->learnParam(2, target->id, 4);

		// learnParam must persist the mapping on m before serialization. requireMapped also
		// checks handle->module, which a mapping onto an unregistered target silently leaves
		// null while moduleId still looks right.
		h.requireMapped(&m->paramHandles[0], target, 0);

		json_t* j = m->dataToJson();
		// The maps array must be serialized with one entry per map slot
		json_t* mapsJ = json_object_get(j, "maps");
		REQUIRE(mapsJ != nullptr);
		REQUIRE(json_array_size(mapsJ) == (size_t) m->mapLen);

		// Rack allows only ONE ParamHandle per (moduleId, paramId): with
		// overwrite=false, updateParamHandle_NoLock() resets the *new* handle
		// when another handle still claims the param. Release m's claims so m2
		// can take them — mirrors a real preset load, where the old module
		// instance (and its handles) is destroyed before dataFromJson() runs.
		int mapLen = m->mapLen;
		m->clearMaps_WithLock();

		m2->dataFromJson(j);
		json_decref(j);

		// Mapped slots must round-trip moduleId and paramId exactly — and resolve to the live
		// target, which is what makes the reloaded mapping actually drive anything.
		h.requireMapped(&m2->paramHandles[0], target, 0);
		h.requireMapped(&m2->paramHandles[1], target, 2);
		h.requireMapped(&m2->paramHandles[2], target, 4);

		// mapLen (derived from the last mapped slot) must round-trip
		REQUIRE(m2->mapLen == mapLen);
		// Unmapped slots stay unmapped
		h.requireUnmapped(&m2->paramHandles[3]);
	}
}

TEST_CASE("Reset restores defaults", "[CVPam]") {
	Test::ModuleScaffold<CVPamModule> mods;
	CVPamModule* m = mods.create("CVPam");

	m->bipolarOutput = true;
	m->audioRate = false;
	m->locked = true;

	Module::ResetEvent re;
	m->onReset(re);

	REQUIRE(m->bipolarOutput == false);
	REQUIRE(m->audioRate == true);
	REQUIRE(m->locked == false);
	REQUIRE(m->mapLen == 0);
}

TEST_CASE("process() drives POLY_OUTPUT1/2 from mapped targets", "[CVPam]") {
	Test::Harness h;
	CVPamModule* m = h.addModule<CVPamModule>("CVPam");
	rack::Module* target = h.addModule<rack::Module>("Glue");
	// setChannels() is a no-op on a disconnected port (Port::setChannels), so the channel
	// counts below only become observable once a cable is modeled as present.
	h.connectOutput(m, CVPamModule::POLY_OUTPUT1);
	h.connectOutput(m, CVPamModule::POLY_OUTPUT2);

	SECTION("Mapped slot value 0..1 is scaled to 0..10V unipolar output on bank 1") {
		h.mapParam(&m->paramHandles[0], target, 0);
		m->updateMapLen();
		h.setMappedValue(&m->paramHandles[0], 1.f);

		h.dspStep();

		REQUIRE(m->outputs[CVPamModule::POLY_OUTPUT1].getVoltage(0) == Catch::Approx(10.f));
		REQUIRE(m->outputs[CVPamModule::POLY_OUTPUT1].getChannels() == 1);
		// Bank 2 has nothing mapped: Port::setChannels(0) still forces at least 1 channel
		// once connected, so an idle bank reads as connected-but-silent, not disconnected.
		REQUIRE(m->outputs[CVPamModule::POLY_OUTPUT2].getChannels() == 1);
		REQUIRE(m->outputs[CVPamModule::POLY_OUTPUT2].getVoltage(0) == Catch::Approx(0.f));
	}

	SECTION("Bipolar output shifts scaled value by -5V") {
		m->bipolarOutput = true;
		h.mapParam(&m->paramHandles[0], target, 0);
		m->updateMapLen();
		h.setMappedValue(&m->paramHandles[0], 1.f);

		h.dspStep();

		REQUIRE(m->outputs[CVPamModule::POLY_OUTPUT1].getVoltage(0) == Catch::Approx(5.f));
	}

	SECTION("A slot mapped at index 16 or above drives POLY_OUTPUT2, not POLY_OUTPUT1") {
		h.mapParam(&m->paramHandles[16], target, 0);
		m->updateMapLen();
		h.setMappedValue(&m->paramHandles[16], 1.f);

		h.dspStep();

		REQUIRE(m->outputs[CVPamModule::POLY_OUTPUT2].getVoltage(0) == Catch::Approx(10.f));
		REQUIRE(m->outputs[CVPamModule::POLY_OUTPUT2].getChannels() == 1);
		// Channel count only reflects the highest mapped index within the bank, not slot 0
		REQUIRE(m->outputs[CVPamModule::POLY_OUTPUT1].getChannels() == 1);
		REQUIRE(m->outputs[CVPamModule::POLY_OUTPUT1].getVoltage(0) == Catch::Approx(0.f));
	}

	SECTION("Channel count on each bank tracks the highest mapped slot within that bank") {
		h.mapParam(&m->paramHandles[0], target, 0);
		h.mapParam(&m->paramHandles[3], target, 1);
		h.mapParam(&m->paramHandles[17], target, 2);
		m->updateMapLen();

		h.dspStep();

		// Highest mapped index in bank 1 is 3 -> 4 channels
		REQUIRE(m->outputs[CVPamModule::POLY_OUTPUT1].getChannels() == 4);
		// Highest mapped index in bank 2 is 17 (local index 1) -> 2 channels
		REQUIRE(m->outputs[CVPamModule::POLY_OUTPUT2].getChannels() == 2);
	}

	SECTION("Unmapped slots are skipped and do not affect other channels' voltages") {
		h.mapParam(&m->paramHandles[0], target, 0);
		h.mapParam(&m->paramHandles[2], target, 1);
		m->updateMapLen();
		h.setMappedValue(&m->paramHandles[0], 1.f);
		h.setMappedValue(&m->paramHandles[2], 0.f);
		// Slot 1 stays unmapped, sitting between two mapped slots

		h.dspStep();

		REQUIRE(m->outputs[CVPamModule::POLY_OUTPUT1].getVoltage(0) == Catch::Approx(10.f));
		REQUIRE(m->outputs[CVPamModule::POLY_OUTPUT1].getVoltage(2) == Catch::Approx(0.f));
	}
}

TEST_CASE("process() respects audioRate", "[CVPam]") {
	Test::Harness h;
	CVPamModule* m = h.addModule<CVPamModule>("CVPam");
	rack::Module* target = h.addModule<rack::Module>("Glue");

	h.mapParam(&m->paramHandles[0], target, 0);
	m->updateMapLen();
	h.setMappedValue(&m->paramHandles[0], 1.f);

	SECTION("With audioRate disabled, output only updates once the process divider fires") {
		m->audioRate = false;

		h.dspStep();
		// The divider (division 32) has not fired yet: output is still at its initial 0V
		REQUIRE(m->outputs[CVPamModule::POLY_OUTPUT1].getVoltage(0) == Catch::Approx(0.f));

		h.dspSteps(32);
		REQUIRE(m->outputs[CVPamModule::POLY_OUTPUT1].getVoltage(0) == Catch::Approx(10.f));
	}

	SECTION("With audioRate enabled (default), output updates on every step") {
		h.dspStep();
		REQUIRE(m->outputs[CVPamModule::POLY_OUTPUT1].getVoltage(0) == Catch::Approx(10.f));
	}
}