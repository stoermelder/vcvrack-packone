#pragma once
// Test cases for AhabSim configuration and persistence: UDP/OSC destinations,
// port validation, and JSON serialization/deserialization. Included by
// AhabSim.test.cpp.

#include "Ahab.test.hpp"


TEST_CASE("UDP destination configuration", "[AhabSim]") {
	AhabSim sim;
	
	sim.setUdpDestination("192.168.1.1", "8000");
	REQUIRE(sim.getUdpAddress() == "192.168.1.1");
	REQUIRE(sim.getUdpPort() == "8000");
	
	// Test with whitespace (should be trimmed)
	sim.setUdpDestination("  10.0.0.1  ", "  9000  ");
	REQUIRE(sim.getUdpAddress() == "10.0.0.1");
	REQUIRE(sim.getUdpPort() == "9000");
}

TEST_CASE("OSC destination configuration", "[AhabSim]") {
	AhabSim sim;
	
	sim.setOscDestination("localhost", "9001");
	REQUIRE(sim.getOscAddress() == "localhost");
	REQUIRE(sim.getOscPort() == "9001");
	
	// Test with whitespace (should be trimmed)
	sim.setOscDestination("  127.0.0.1  ", "  9002  ");
	REQUIRE(sim.getOscAddress() == "127.0.0.1");
	REQUIRE(sim.getOscPort() == "9002");
}

TEST_CASE("Invalid port numbers rejected", "[AhabSim]") {
	AhabSim sim;
	
	sim.setUdpDestination("127.0.0.1", "8000");
	REQUIRE(sim.getUdpPort() == "8000");
	
	// Try to set invalid port (should be ignored)
	sim.setUdpDestination("127.0.0.1", "invalid");
	REQUIRE(sim.getUdpPort() == "8000"); // Should remain unchanged
	
	// Try to set out-of-range port
	sim.setUdpDestination("127.0.0.1", "99999");
	REQUIRE(sim.getUdpPort() == "8000"); // Should remain unchanged
}

TEST_CASE("Serialization to JSON", "[JSON][AhabSim]") {
	AhabSim sim;
	
	sim.setFieldSizeRequest(3, 4, false);
	sim.process();
	sim.setRandomSeed(999);
	sim.setUdpDestination("192.168.1.100", "7000");
	sim.setOscDestination("10.0.0.1", "8000");
	
	json_t* j = sim.toJson();
	REQUIRE(j != nullptr);
	
	// Check fields
	json_t* hJ = json_object_get(j, "height");
	json_t* wJ = json_object_get(j, "width");
	REQUIRE(json_integer_value(hJ) == 3);
	REQUIRE(json_integer_value(wJ) == 4);
	
	json_t* seedJ = json_object_get(j, "random_seed");
	REQUIRE(json_integer_value(seedJ) == 999);
	
	json_t* udpAddrJ = json_object_get(j, "udpAddress");
	REQUIRE(std::string(json_string_value(udpAddrJ)) == "192.168.1.100");
	
	json_t* oscPortJ = json_object_get(j, "oscPort");
	REQUIRE(std::string(json_string_value(oscPortJ)) == "8000");
	
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
	json_object_set_new(j, "udpAddress", json_string("172.16.0.1"));
	json_object_set_new(j, "udpPort", json_string("6000"));
	json_object_set_new(j, "oscAddress", json_string("192.168.0.1"));
	json_object_set_new(j, "oscPort", json_string("7000"));
	
	sim.fromJson(j);
	
	REQUIRE(sim.getFieldHeight() == 6);
	REQUIRE(sim.getFieldWidth() == 8);
	REQUIRE(sim.getTickNumber() == 5);
	REQUIRE(sim.getRandomSeed() == 777);
	REQUIRE(sim.getUdpAddress() == "172.16.0.1");
	REQUIRE(sim.getUdpPort() == "6000");
	REQUIRE(sim.getOscAddress() == "192.168.0.1");
	REQUIRE(sim.getOscPort() == "7000");
	
	Usz h, w;
	sim.getDisplayBuffer(h, w);
	Glyph const* buffer = sim.getFieldBuffer();
	REQUIRE(buffer[0] == 'A');
	REQUIRE(buffer[1] == 'B');
	
	json_decref(j);
}
