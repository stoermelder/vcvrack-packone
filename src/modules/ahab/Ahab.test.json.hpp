#pragma once
// Test cases for JSON serialization/deserialization across the Ahab subsystem:
// the module's dataToJson/dataFromJson (top-level keys, the "sim" sub-object,
// null-guards), AhabSim's field/tick/seed, and AhabOoscOutput's
// destination keys (which live inside the module's "sim" sub-object).
// Included by Ahab.test.cpp.

#include "Ahab.test.hpp"


TEST_CASE("JSON round-trip preserves state", "[JSON][Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	AhabModule* m2 = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);
	Test::registerModule(m2);

	SECTION("Serialized JSON structure") {
		m->panelTheme = 1;
		m->midiVirtualPortId = 2;
		m->midiOutEnabled = false;
		m->midiCcOffset = 80;
		m->simRunning = false;
		m->overwriteZeroNoteDuration = false;
		m->gridStepCol = 16;
		m->gridStepRow = 12;
		m->clkRatioSetting = AhabModule::CLK_RATIO_MUL4;
		m->midiOutPort.channel = 5;

		json_t* j = m->dataToJson();
		REQUIRE(j != nullptr);
		REQUIRE(json_is_object(j));
		// All 12 top-level keys must be present
		REQUIRE(json_object_get(j, "panelTheme") != nullptr);
		REQUIRE(json_object_get(j, "midiVirtualPortId") != nullptr);
		REQUIRE(json_object_get(j, "midiOutEnabled") != nullptr);
		REQUIRE(json_object_get(j, "midiOutPort") != nullptr);
		REQUIRE(json_object_get(j, "midiCcOffset") != nullptr);
		REQUIRE(json_object_get(j, "sim") != nullptr);
		REQUIRE(json_object_get(j, "simRunning") != nullptr);
		REQUIRE(json_object_get(j, "overwriteZeroNoteDuration") != nullptr);
		REQUIRE(json_object_get(j, "gridStepCol") != nullptr);
		REQUIRE(json_object_get(j, "gridStepRow") != nullptr);
		REQUIRE(json_object_get(j, "clkRatio") != nullptr);
		REQUIRE(json_object_get(j, "generator") != nullptr);
		// The two nested objects must themselves be objects
		REQUIRE(json_is_object(json_object_get(j, "midiOutPort")));
		REQUIRE(json_is_object(json_object_get(j, "sim")));
		REQUIRE(json_is_object(json_object_get(j, "generator")));
		json_decref(j);
	}

	SECTION("Top-level scalars round-trip") {
		// Distinctive non-default values for every scalar stored to JSON
		m->panelTheme = 1;
		m->midiVirtualPortId = 2;
		m->midiOutEnabled = false;
		m->midiCcOffset = 80;
		m->simRunning = false;
		m->overwriteZeroNoteDuration = false;
		m->gridStepCol = 16;
		m->gridStepRow = 12;
		m->clkRatioSetting = AhabModule::CLK_RATIO_MUL4;
		m->lastGenerator.seed = 123456;
		m->lastGenerator.density = 0.55f;
		m->lastGenerator.qualityGate = false;

		json_t* j = m->dataToJson();
		// Start m2 at defaults so dataFromJson() is genuinely exercised
		m2->panelTheme = 0;
		m2->midiVirtualPortId = 0;
		m2->midiOutEnabled = true;
		m2->midiCcOffset = 0;
		m2->simRunning = true;
		m2->overwriteZeroNoteDuration = true;
		m2->gridStepCol = 0;
		m2->gridStepRow = 0;
		m2->clkRatioSetting = AhabModule::CLK_RATIO_MUL1;
		m2->lastGenerator.seed = 0;
		m2->lastGenerator.density = 0.3f;
		m2->lastGenerator.qualityGate = true;
		m2->dataFromJson(j);
		json_decref(j);

		REQUIRE(m2->panelTheme == 1);
		REQUIRE(m2->midiVirtualPortId == 2);
		REQUIRE(m2->midiOutEnabled == false);
		REQUIRE(m2->midiCcOffset == 80);
		REQUIRE(m2->simRunning == false);
		REQUIRE(m2->overwriteZeroNoteDuration == false);
		REQUIRE(m2->gridStepCol == 16);
		REQUIRE(m2->gridStepRow == 12);
		REQUIRE(m2->clkRatioSetting == AhabModule::CLK_RATIO_MUL4);
		REQUIRE(m2->lastGenerator.seed == 123456u);
		REQUIRE(m2->lastGenerator.density == 0.55f);
		REQUIRE(m2->lastGenerator.qualityGate == false);
	}

	SECTION("Legacy flat lastRandomizerSeed loads into generator settings") {
		// Patches saved before the "generator" subobject existed carry a flat
		// lastRandomizerSeed; it must land in lastGenerator.seed, with density
		// keeping its default (it was session-only and cannot be recovered).
		m2->lastGenerator.seed = 999; // prove the stored value wins
		json_t* j = json_object();
		json_object_set_new(j, "lastRandomizerSeed", json_integer(777));
		m2->dataFromJson(j);
		json_decref(j);
		REQUIRE(m2->lastGenerator.seed == 777u);
	}
	SECTION("midiOutPort (nested object) round-trips") {
		// midiOutPort is a rack::midi::Output; its channel must survive a round-trip.
		m->midiOutPort.channel = 5;

		json_t* j = m->dataToJson();
		m2->midiOutPort.channel = -1; // default
		m2->dataFromJson(j);
		json_decref(j);

		REQUIRE(m2->midiOutPort.channel == 5);
	}

	SECTION("sim sub-object round-trips") {
		// Distinct module state.
		m->panelTheme = 2;
		m->midiVirtualPortId = 3;
		m->midiOutEnabled = false;
		m->midiCcOffset = 100;
		m->simRunning = false;
		m->overwriteZeroNoteDuration = false;
		m->gridStepCol = 4;
		m->gridStepRow = 6;

		// Distinct sim state: a small field, a non-zero tick, a seed and UDP/OSC
		// destinations, all persisted under the module's "sim" key.
		Usz h, w;
		REQUIRE(m->sim->loadRectFromOrcaRequest(":04C21\n*.....", 0, 0, h, w, true) == true);
		m->process({}); // drain the field load
		for (int i = 0; i < 5; ++i) stepSim(m); // tick → 5
		m->sim->setRandomSeed(777);
		m->udpOutput->setDestination("192.168.1.50", "7000");
		m->oscOutput->setDestination("10.0.0.5", "8000");
		REQUIRE(m->sim->getTickNumber() == 5);

		json_t* j = m->dataToJson();
		m2->dataFromJson(j);
		json_decref(j);

		// Module state restored.
		REQUIRE(m2->panelTheme == 2);
		REQUIRE(m2->midiVirtualPortId == 3);
		REQUIRE(m2->midiOutEnabled == false);
		REQUIRE(m2->midiCcOffset == 100);
		REQUIRE(m2->simRunning == false);
		REQUIRE(m2->overwriteZeroNoteDuration == false);
		REQUIRE(m2->gridStepCol == 4);
		REQUIRE(m2->gridStepRow == 6);

		// Sim state restored through the module.
		REQUIRE(m2->sim->getFieldHeight() == 2);
		REQUIRE(m2->sim->getFieldWidth() == 6);
		Glyph const* buf = m2->sim->getFieldBuffer();
		REQUIRE(buf[0] == ':');
		REQUIRE(m2->sim->getTickNumber() == 5);
		REQUIRE(m2->sim->getRandomSeed() == 777);
		REQUIRE(m2->udpOutput->getAddress() == "192.168.1.50");
		REQUIRE(m2->udpOutput->getPort() == "7000");
		REQUIRE(m2->oscOutput->getAddress() == "10.0.0.5");
		REQUIRE(m2->oscOutput->getPort() == "8000");
	}

	Test::unregisterModule(m);
	Test::destroyModule(m);
	Test::unregisterModule(m2);
	Test::destroyModule(m2);
}

TEST_CASE("Missing or invalid sim JSON is ignored safely", "[JSON][Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);

	// Set up sim state that must be preserved.
	m->sim->setRandomSeed(123);
	Usz h, w;
	REQUIRE(m->sim->loadRectFromOrcaRequest(":04C21\n*.....", 0, 0, h, w, true) == true);
	m->process({});
	Usz fh = m->sim->getFieldHeight();
	Usz fw = m->sim->getFieldWidth();

	// JSON with no "sim" key at all: sim state must be untouched.
	json_t* j = json_object();
	json_object_set_new(j, "panelTheme", json_integer(2));
	REQUIRE_NOTHROW(m->dataFromJson(j));
	REQUIRE(m->sim->getFieldHeight() == fh);
	REQUIRE(m->sim->getFieldWidth() == fw);
	REQUIRE(m->sim->getRandomSeed() == 123);
	json_decref(j);

	// JSON with a non-object "sim" (json_null()): must be ignored — the guard is
	// `simJ && json_is_object(simJ)`, so json_null() fails it even though the
	// pointer is non-null.
	json_t* j2 = json_object();
	json_object_set_new(j2, "sim", json_null());
	REQUIRE_NOTHROW(m->dataFromJson(j2));
	REQUIRE(m->sim->getFieldHeight() == fh);
	REQUIRE(m->sim->getFieldWidth() == fw);
	REQUIRE(m->sim->getRandomSeed() == 123);
	json_decref(j2);

	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("Preset JSON null-guards", "[Ahab][JSON]") {
	auto module = Test::createModule<AhabModule>("Ahab");

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

	Test::destroyModule(module);
}

TEST_CASE("Serialization to JSON", "[JSON][AhabSim]") {
	AhabSim sim;
	
	sim.setFieldSizeRequest(3, 4, false);
	sim.process();
	sim.setRandomSeed(999);
	
	json_t* j = sim.toJson();
	REQUIRE(j != nullptr);
	
	// Check fields
	json_t* hJ = json_object_get(j, "height");
	json_t* wJ = json_object_get(j, "width");
	REQUIRE(json_integer_value(hJ) == 3);
	REQUIRE(json_integer_value(wJ) == 4);
	
	json_t* seedJ = json_object_get(j, "random_seed");
	REQUIRE(json_integer_value(seedJ) == 999);
	
	// The sim no longer serializes the UDP/OSC destination keys — the module
	// adds them via udpOutput->toJson(simJ) (see the AhabOoscOutput JSON
	// tests below).
	REQUIRE(json_object_get(j, "udpAddress") == nullptr);
	
	json_decref(j);
}

TEST_CASE("Deserialization from JSON", "[JSON][AhabSim]") {
	AhabSim sim;
	
	// Create JSON manually
	json_t* j = json_object();
	json_object_set_new(j, "height", json_integer(6));
	json_object_set_new(j, "width", json_integer(8));
	
	// Create a simple field
	std::vector<uint8_t> cells(6 * 8, '.');
	cells[0] = 'A';
	cells[1] = 'B';
	std::string encoded = rack::string::toBase64(cells.data(), cells.size());
	json_object_set_new(j, "cells", json_string(encoded.c_str()));
	
	json_object_set_new(j, "tick", json_integer(5));
	json_object_set_new(j, "random_seed", json_integer(777));
	
	sim.fromJson(j);
	
	REQUIRE(sim.getFieldHeight() == 6);
	REQUIRE(sim.getFieldWidth() == 8);
	REQUIRE(sim.getTickNumber() == 5);
	REQUIRE(sim.getRandomSeed() == 777);
	
	Usz h, w;
	sim.getDisplayBuffer(h, w);
	Glyph const* buffer = sim.getFieldBuffer();
	REQUIRE(buffer[0] == 'A');
	REQUIRE(buffer[1] == 'B');
	
	json_decref(j);
}

TEST_CASE("fromJson clamps oversized fields", "[JSON][AhabSim]") {
	AhabSim sim;

	// height/width above MAX_FIELD_HEIGHT/WIDTH must clamp so the fixed
	// DSP-thread scratch buffer (sized 100x100) can't overflow.
	json_t* j = json_object();
	json_object_set_new(j, "height", json_integer(500));
	json_object_set_new(j, "width", json_integer(500));
	std::vector<uint8_t> cells(500 * 500, '.');
	cells[0] = 'A';
	cells[99] = 'Z'; // last cell of the clamped row 0
	cells[499 * 500 + 499] = 'Q'; // bottom-right of the oversized field
	std::string encoded = rack::string::toBase64(cells.data(), cells.size());
	json_object_set_new(j, "cells", json_string(encoded.c_str()));

	sim.fromJson(j);
	json_decref(j);

	REQUIRE(sim.getFieldHeight() == AhabSim::MAX_FIELD_HEIGHT);
	REQUIRE(sim.getFieldWidth() == AhabSim::MAX_FIELD_WIDTH);

	Glyph const* buf = sim.getFieldBuffer();
	REQUIRE(buf[0] == 'A');
	REQUIRE(buf[99] == 'Z');
	// Bytes past the clamped area are truncated, not wrapped anywhere
	for (Usz i = 0; i < AhabSim::MAX_FIELD_HEIGHT * AhabSim::MAX_FIELD_WIDTH; ++i) {
		REQUIRE(buf[i] != 'Q');
	}

	// Stepping the clamped max-size field is safe.
	for (int i = 0; i < 4; ++i) sim.step();
}

TEST_CASE("fromJson rejects non-positive dimensions", "[JSON][AhabSim]") {
	AhabSim sim;

	// Establish known state that must remain untouched on rejection.
	sim.setFieldSizeRequest(3, 4, false);
	sim.process();

	SECTION("Zero dimensions") {
		json_t* j = json_object();
		json_object_set_new(j, "height", json_integer(0));
		json_object_set_new(j, "width", json_integer(8));
		json_object_set_new(j, "cells", json_string(""));
		sim.fromJson(j);
		json_decref(j);

		REQUIRE(sim.getFieldHeight() == 3);
		REQUIRE(sim.getFieldWidth() == 4);
	}

	SECTION("Negative dimensions") {
		json_t* j = json_object();
		json_object_set_new(j, "height", json_integer(-5));
		json_object_set_new(j, "width", json_integer(8));
		json_object_set_new(j, "cells", json_string(""));
		sim.fromJson(j);
		json_decref(j);

		REQUIRE(sim.getFieldHeight() == 3);
		REQUIRE(sim.getFieldWidth() == 4);
	}
}

TEST_CASE("Module-level sim JSON clamps oversized fields", "[JSON][Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	AhabModule* m2 = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);
	Test::registerModule(m2);

	// Tamper with a real preset: force the "sim" sub-object to oversized dims.
	json_t* j = m->dataToJson();
	json_t* simJ = json_object_get(j, "sim");
	REQUIRE(json_is_object(simJ));
	json_object_set_new(simJ, "height", json_integer(500));
	json_object_set_new(simJ, "width", json_integer(500));

	m2->dataFromJson(j);
	json_decref(j);

	REQUIRE(m2->sim->getFieldHeight() == AhabSim::MAX_FIELD_HEIGHT);
	REQUIRE(m2->sim->getFieldWidth() == AhabSim::MAX_FIELD_WIDTH);

	Test::unregisterModule(m);
	Test::destroyModule(m);
	Test::unregisterModule(m2);
	Test::destroyModule(m2);
}

TEST_CASE("AhabOoscOutput JSON round-trip (UDP)", "[JSON][AhabUdp]") {
	AhabOoscOutput out(AhabOoscOutput::Kind::UDP);
	out.setDestination("192.168.1.100", "7000");

	// toJson writes the udpAddress / udpPort keys into the passed (sim-shaped) object.
	json_t* simJ = json_object();
	out.toJson(simJ);
	REQUIRE(std::string(json_string_value(json_object_get(simJ, "udpAddress"))) == "192.168.1.100");
	REQUIRE(std::string(json_string_value(json_object_get(simJ, "udpPort"))) == "7000");
	json_decref(simJ);

	// fromJson reads the udpAddress / udpPort keys back from a sim-shaped object.
	json_t* j2 = json_object();
	json_object_set_new(j2, "udpAddress", json_string("172.16.0.1"));
	json_object_set_new(j2, "udpPort", json_string("6000"));
	AhabOoscOutput out2(AhabOoscOutput::Kind::UDP);
	out2.fromJson(j2);
	REQUIRE(out2.getAddress() == "172.16.0.1");
	REQUIRE(out2.getPort() == "6000");
	json_decref(j2);
}

TEST_CASE("AhabOoscOutput JSON round-trip (OSC)", "[JSON][AhabUdp]") {
	AhabOoscOutput out(AhabOoscOutput::Kind::OSC);
	out.setDestination("10.0.0.1", "8000");

	json_t* simJ = json_object();
	out.toJson(simJ);
	REQUIRE(std::string(json_string_value(json_object_get(simJ, "oscAddress"))) == "10.0.0.1");
	REQUIRE(std::string(json_string_value(json_object_get(simJ, "oscPort"))) == "8000");
	json_decref(simJ);

	json_t* j2 = json_object();
	json_object_set_new(j2, "oscAddress", json_string("192.168.0.1"));
	json_object_set_new(j2, "oscPort", json_string("7000"));
	AhabOoscOutput out2(AhabOoscOutput::Kind::OSC);
	out2.fromJson(j2);
	REQUIRE(out2.getAddress() == "192.168.0.1");
	REQUIRE(out2.getPort() == "7000");
	json_decref(j2);
}

TEST_CASE("AhabOoscOutput fromJson with missing keys is a safe no-op", "[JSON][AhabUdp]") {
	// fromJson on an object without the relevant keys leaves the defaults untouched.
	json_t* j = json_object();
	json_object_set_new(j, "height", json_integer(3)); // unrelated key
	AhabOoscOutput out(AhabOoscOutput::Kind::UDP);
	REQUIRE_NOTHROW(out.fromJson(j));
	REQUIRE(out.getAddress() == "127.0.0.1");
	REQUIRE(out.getPort() == "49161");
	json_decref(j);
}
