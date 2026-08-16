#pragma once
// Test cases for JSON serialization/deserialization across the Ahab subsystem:
// the module's dataToJson/dataFromJson (top-level keys, the "sim" sub-object,
// null-guards), AhabSim's field/tick/seed, and AhabOoscUdpOutput's four
// destination keys (which live inside the module's "sim" sub-object).
// Included by Ahab.test.cpp.

#include "Ahab.test.hpp"


TEST_CASE("JSON serialization", "[JSON][Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);
	
	// Set some values
	m->panelTheme = 1;
	m->midiVirtualPortId = 2;
	m->midiOutEnabled = false;
	m->midiCcOffset = 80;
	m->simRunning = false;
	m->overwriteZeroNoteDuration = false;
	m->gridStepCol = 16;
	m->gridStepRow = 12;
	
	// Serialize
	json_t* j = m->dataToJson();
	REQUIRE(j != nullptr);
	
	// Check values
	REQUIRE(json_integer_value(json_object_get(j, "panelTheme")) == 1);
	REQUIRE(json_integer_value(json_object_get(j, "midiVirtualPortId")) == 2);
	REQUIRE(json_boolean_value(json_object_get(j, "midiOutEnabled")) == false);
	REQUIRE(json_integer_value(json_object_get(j, "midiCcOffset")) == 80);
	REQUIRE(json_boolean_value(json_object_get(j, "simRunning")) == false);
	REQUIRE(json_boolean_value(json_object_get(j, "overwriteZeroNoteDuration")) == false);
	REQUIRE(json_integer_value(json_object_get(j, "gridStepCol")) == 16);
	REQUIRE(json_integer_value(json_object_get(j, "gridStepRow")) == 12);
	
	json_decref(j);
	
	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("JSON deserialization", "[JSON][Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);
	
	// Create JSON
	json_t* j = json_object();
	json_object_set_new(j, "panelTheme", json_integer(2));
	json_object_set_new(j, "midiVirtualPortId", json_integer(3));
	json_object_set_new(j, "midiOutEnabled", json_boolean(false));
	json_object_set_new(j, "midiCcOffset", json_integer(100));
	json_object_set_new(j, "simRunning", json_boolean(false));
	json_object_set_new(j, "overwriteZeroNoteDuration", json_boolean(false));
	json_object_set_new(j, "gridStepCol", json_integer(4));
	json_object_set_new(j, "gridStepRow", json_integer(6));
	
	// Create sim JSON
	json_t* simJ = m->sim->toJson();
	json_object_set_new(j, "sim", simJ);
	
	// Create midi port JSON
	json_object_set_new(j, "midiOutPort", json_object());
	
	// Deserialize
	m->dataFromJson(j);
	
	// Check values
	REQUIRE(m->panelTheme == 2);
	REQUIRE(m->midiVirtualPortId == 3);
	REQUIRE(m->midiOutEnabled == false);
	REQUIRE(m->midiCcOffset == 100);
	REQUIRE(m->simRunning == false);
	REQUIRE(m->overwriteZeroNoteDuration == false);
	REQUIRE(m->gridStepCol == 4);
	REQUIRE(m->gridStepRow == 6);
	
	json_decref(j);
	
	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("Module JSON round-trip includes the sim sub-object", "[JSON][Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);

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
	m->udpOutput->setUdpDestination("192.168.1.50", "7000");
	m->udpOutput->setOscDestination("10.0.0.5", "8000");
	REQUIRE(m->sim->getTickNumber() == 5);

	// Serialize, reset to defaults, then restore through the module.
	json_t* j = m->dataToJson();
	REQUIRE(j != nullptr);
	Module::ResetEvent e;
	m->onReset(e);
	m->dataFromJson(j);

	// Module state restored.
	REQUIRE(m->panelTheme == 2);
	REQUIRE(m->midiVirtualPortId == 3);
	REQUIRE(m->midiOutEnabled == false);
	REQUIRE(m->midiCcOffset == 100);
	REQUIRE(m->simRunning == false);
	REQUIRE(m->overwriteZeroNoteDuration == false);
	REQUIRE(m->gridStepCol == 4);
	REQUIRE(m->gridStepRow == 6);

	// Sim state restored through the module.
	REQUIRE(m->sim->getFieldHeight() == 2);
	REQUIRE(m->sim->getFieldWidth() == 6);
	Glyph const* buf = m->sim->getFieldBuffer();
	REQUIRE(buf[0] == ':');
	REQUIRE(m->sim->getTickNumber() == 5);
	REQUIRE(m->sim->getRandomSeed() == 777);
	REQUIRE(m->udpOutput->getUdpAddress() == "192.168.1.50");
	REQUIRE(m->udpOutput->getUdpPort() == "7000");
	REQUIRE(m->udpOutput->getOscAddress() == "10.0.0.5");
	REQUIRE(m->udpOutput->getOscPort() == "8000");

	json_decref(j);
	Test::unregisterModule(m);
	Test::destroyModule(m);
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
	// adds them via udpOutput->toJson(simJ) (see the AhabOoscUdpOutput JSON
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

TEST_CASE("AhabOoscUdpOutput JSON round-trip", "[JSON][AhabUdp]") {
	AhabOoscUdpOutput out;
	out.setUdpDestination("192.168.1.100", "7000");
	out.setOscDestination("10.0.0.1", "8000");

	// toJson writes the four keys into the passed (sim-shaped) object.
	json_t* simJ = json_object();
	out.toJson(simJ);
	REQUIRE(std::string(json_string_value(json_object_get(simJ, "udpAddress"))) == "192.168.1.100");
	REQUIRE(std::string(json_string_value(json_object_get(simJ, "udpPort"))) == "7000");
	REQUIRE(std::string(json_string_value(json_object_get(simJ, "oscAddress"))) == "10.0.0.1");
	REQUIRE(std::string(json_string_value(json_object_get(simJ, "oscPort"))) == "8000");
	json_decref(simJ);

	// fromJson reads the four keys back from a sim-shaped object.
	json_t* j2 = json_object();
	json_object_set_new(j2, "udpAddress", json_string("172.16.0.1"));
	json_object_set_new(j2, "udpPort", json_string("6000"));
	json_object_set_new(j2, "oscAddress", json_string("192.168.0.1"));
	json_object_set_new(j2, "oscPort", json_string("7000"));
	AhabOoscUdpOutput out2;
	out2.fromJson(j2);
	REQUIRE(out2.getUdpAddress() == "172.16.0.1");
	REQUIRE(out2.getUdpPort() == "6000");
	REQUIRE(out2.getOscAddress() == "192.168.0.1");
	REQUIRE(out2.getOscPort() == "7000");
	json_decref(j2);
}

TEST_CASE("AhabOoscUdpOutput fromJson with missing keys is a safe no-op", "[JSON][AhabUdp]") {
	// fromJson on an object without the four keys leaves the defaults untouched.
	json_t* j = json_object();
	json_object_set_new(j, "height", json_integer(3)); // unrelated key
	AhabOoscUdpOutput out;
	REQUIRE_NOTHROW(out.fromJson(j));
	REQUIRE(out.getUdpAddress() == "127.0.0.1");
	REQUIRE(out.getUdpPort() == "49161");
	REQUIRE(out.getOscAddress() == "127.0.0.1");
	REQUIRE(out.getOscPort() == "49162");
	json_decref(j);
}
