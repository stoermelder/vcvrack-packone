#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "AhabSim.hpp"

using namespace StoermelderPackOne;
using namespace StoermelderPackOne::Ahab;

// Define the single instance used by tests
static Test::TestContext<> testContext;


extern "C" Usz custom_vcvin(void* ptr, Usz port_num, Usz a, Usz b);
extern "C" void custom_vcvout(void* ptr, Usz port_index, Usz a, Usz b, Usz value);


TEST_CASE("Op vcvin ports 1-4 map to voltage range", "[AhabSim]") {
	AhabSim sim;
	size_t in_port = 0.f; float in_voltage = 10.0f;

	// Port 1 maps to dsp input index 0
	sim.setDspInputReader([&](size_t p){ return p == in_port ? in_voltage : 5.0f; });
	void* ptr = (void*)sim.getEvents();
	// port 1: 10v, mapping should saturate to upper bound -> expect 15 for range [5,15]
	REQUIRE(custom_vcvin(ptr, 1, 5, 15) == 15);
	// port 2, 3, 4: 5v input -> expect mid value 12 for range [5,15]
	REQUIRE(custom_vcvin(ptr, 2, 5, 15) == 10);
	REQUIRE(custom_vcvin(ptr, 3, 5, 15) == 10);
	REQUIRE(custom_vcvin(ptr, 4, 5, 15) == 10);

	// With input=0v, expect lower bound
	sim.setDspInputReader([](size_t p){ return 0.0f; });
	REQUIRE(custom_vcvin(ptr, 1, 5, 15) == 5);
	REQUIRE(custom_vcvin(ptr, 2, 5, 15) == 5);
	REQUIRE(custom_vcvin(ptr, 3, 5, 15) == 5);
	REQUIRE(custom_vcvin(ptr, 4, 5, 15) == 5);
}

TEST_CASE("Op vcvin ports A-D map to semitone mod12", "[AhabSim]") {
	AhabSim sim;
	size_t in_port = 0; float in_voltage = 0.75f;

	// Letter port 10 maps to dsp input index 0
	sim.setDspInputReader([&](size_t p){ return p == in_port ? in_voltage : 0.0f; });
	void* ptr = (void*)sim.getEvents();
	// port A (10): 0.75 * 12 = 9 -> rounds to 9
	REQUIRE(custom_vcvin(ptr, 10, 0, 0) == 9);
	// port B, C, D (11-13): 0.0 * 12 = 0
	REQUIRE(custom_vcvin(ptr, 11, 0, 0) == 0);
	REQUIRE(custom_vcvin(ptr, 12, 0, 0) == 0);
	REQUIRE(custom_vcvin(ptr, 13, 0, 0) == 0);
}

TEST_CASE("Op vcvin with missing max defaults to 35 #425", "[AhabSim]") {
	AhabSim sim;

	// Set port 1 to full-scale 10V to exercise 0-35 mapping.
	sim.setDspInputReader([](size_t p){ return 10.0f; });

	Usz out_h, out_w;
	REQUIRE(sim.loadRectFromOrcaRequest(".<.1..\n.*....", 0, 0, out_h, out_w, true) == true);
	REQUIRE(out_h == 2);
	REQUIRE(out_w == 6);
	// Apply the loaded field
	sim.process();

	// Execute one simulation tick (requires step request/process cycle)
	sim.stepRequest();
	sim.process();

	// The operator should interpret missing max as 35, giving output value 35 -> 'z'
	Usz h, w;
	sim.getDisplayBuffer(h, w);
	REQUIRE(h == 2);
	REQUIRE(w == 6);
	Glyph const* buffer = sim.getFieldBuffer();
	REQUIRE(buffer[1 * w + 1] == 'z');
}

TEST_CASE("Op vcvout ports 1-4 write scaled voltages", "[AhabSim]") {
	AhabSim sim;
	size_t out_port = 99; float out_voltage = 0.0f; int out_gate_ticks = -1;
	void* ptr = (void*)sim.getEvents();
	sim.setDspOutputWriter([&](size_t p, float v, int g){ out_port = p; out_voltage = v; out_gate_ticks = g; });

	// Clamp value=35 in range [0,35] -> voltage should be 10.0f
	custom_vcvout(ptr, 1, 0, 35, 35);
	REQUIRE(out_port == 0);
	REQUIRE(out_voltage == Catch::Approx(10.0f));
	REQUIRE(out_gate_ticks == 0);

	// Test mid value: value=20 in range [10,30] -> voltage should be 5v
	custom_vcvout(ptr, 2, 10, 30, 20);
	REQUIRE(out_port == 1);
	REQUIRE(out_voltage == Catch::Approx(5.0f));
	REQUIRE(out_gate_ticks == 0);

	// Test lower bound: value=0 in range [5,25] -> voltage should be 0v
	custom_vcvout(ptr, 3, 5, 25, 0);
	REQUIRE(out_port == 2);
	REQUIRE(out_voltage == Catch::Approx(0.0f));
	REQUIRE(out_gate_ticks == 0);

	// Test upper bound: value=20 in range [5,15] -> voltage should be 10v
	custom_vcvout(ptr, 4, 5, 15, 20);
	REQUIRE(out_port == 3);
	REQUIRE(out_voltage == Catch::Approx(10.0f));
	REQUIRE(out_gate_ticks == 0);
}

TEST_CASE("Op vcvout ports A-D write v/oct conversion", "[AhabSim]") {
	AhabSim sim;
	size_t out_port = 99; float out_voltage = 0.0f; int out_gate_ticks = -1;
	void* ptr = (void*)sim.getEvents();
	sim.setDspOutputWriter([&](size_t p, float v, int g){ out_port = p; out_voltage = v; out_gate_ticks = g; });
	
	// Letter port A (10) -> port 0. For a=1, value=3 -> (3 + 1*12)/12 = 1.25
	custom_vcvout(ptr, 10, 1, 4, 3);
	REQUIRE(out_port == 0);
	REQUIRE(out_voltage == Catch::Approx(1.f + 3 * 1.f / 12.f));
	REQUIRE(out_gate_ticks == 4);

	// Letter port B (11) -> port 1. For a=0, value=0 -> (0 + 0*12)/12 = 0.0
	custom_vcvout(ptr, 11, 0, 7, 0);
	REQUIRE(out_port == 1);
	REQUIRE(out_voltage == Catch::Approx(0.0f));
	REQUIRE(out_gate_ticks == 7);

	// Letter port C (12) -> port 2. For a=2, value=6 -> (6 + 2*12)/12 = 2.5
	custom_vcvout(ptr, 12, 2, 9, 12);
	REQUIRE(out_port == 2);
	REQUIRE(out_voltage == Catch::Approx(3.f));
	REQUIRE(out_gate_ticks == 9);
}

TEST_CASE("Op vcvout letter-port gate length uses b parameter", "[AhabSim]") {
	AhabSim sim;
	size_t out_port = 99;
	float out_voltage = 0.0f;
	int out_gate_ticks = -1;
	void* ptr = (void*)sim.getEvents();
	sim.setDspOutputWriter([&](size_t p, float v, int g){ out_port = p; out_voltage = v; out_gate_ticks = g; });

	// Letter port D (13) -> output port index 3.
	// a = octave, b = gate ticks.
	custom_vcvout(ptr, 13, 3, 11, 6);
	REQUIRE(out_port == 3);
	REQUIRE(out_voltage == Catch::Approx((6.f + 36.f) / 12.f));
	REQUIRE(out_gate_ticks == 11);
}

TEST_CASE("Static convertRectToOrca serializes an arbitrary field", "[AhabSim]") {
	// The widget serializes its display_field snapshot through the static
	// overload, so verify it works on a standalone Field.
	Field f;
	field_init_fill(&f, 2, 3, '.');
	DEFER({ field_deinit(&f); });
	f.buffer[0] = 'A'; f.buffer[1] = 'B'; f.buffer[2] = 'C';
	f.buffer[3] = 'D'; f.buffer[4] = 'E'; f.buffer[5] = 'F';

	REQUIRE(AhabSim::convertRectToOrca(f, 0, 0, 2, 3) == "ABC\nDEF");
	REQUIRE(AhabSim::convertRectToOrca(f, 1, 1, 1, 2) == "EF");
	// Out-of-bounds / zero-size requests are safe and return empty
	REQUIRE(AhabSim::convertRectToOrca(f, 5, 5, 2, 2).empty());
	REQUIRE(AhabSim::convertRectToOrca(f, 0, 0, 0, 0).empty());
}

TEST_CASE("ORCA text parsing builds valid field", "[AhabSim]") {
	std::string orcaText = "D8...\n.....\n.3D8.";
	Field field;
	field_init(&field);
	DEFER({ field_deinit(&field); });
	
	bool success = AhabSim::buildFieldFromOrcaText(orcaText, field);
	REQUIRE(success == true);
	REQUIRE(field.height == 3);
	REQUIRE(field.width == 5);
	REQUIRE(field.buffer[0] == 'D');
	REQUIRE(field.buffer[1] == '8');
}

TEST_CASE("ORCA text parsing handles empty input", "[AhabSim]") {
	std::string orcaText = "";
	Field field;
	field_init(&field);
	DEFER({ field_deinit(&field); });
	
	bool success = AhabSim::buildFieldFromOrcaText(orcaText, field);
	REQUIRE(success == false);
}

TEST_CASE("Field size setting", "[AhabSim]") {
	AhabSim sim;
	
	sim.setFieldSizeRequest(10, 20, false);
	sim.process();
	
	REQUIRE(sim.getFieldHeight() == 10);
	REQUIRE(sim.getFieldWidth() == 20);
}

TEST_CASE("Glyph setting and retrieval", "[AhabSim]") {
	AhabSim sim;
	
	sim.setFieldSizeRequest(5, 5, false);
	sim.process();
	
	sim.setGlyphRequest(2, 3, 'X', Mark_flag_input, false);
	sim.process();
	
	Usz h, w;
	sim.getDisplayBuffer(h, w);
	Glyph const* buffer = sim.getFieldBuffer();
	
	REQUIRE(buffer[2 * w + 3] == 'X');
}

TEST_CASE("Fill rectangle operation", "[AhabSim]") {
	AhabSim sim;
	
	sim.setFieldSizeRequest(10, 10, false);
	sim.process();
	
	sim.fillRectRequest(2, 2, 3, 4, 'F');
	sim.process();
	
	Usz h, w;
	sim.getDisplayBuffer(h, w);
	Glyph const* buffer = sim.getFieldBuffer();
	
	// Check that region is filled
	for (Usz y = 2; y < 5; ++y) {
		for (Usz x = 2; x < 6; ++x) {
			REQUIRE(buffer[y * w + x] == 'F');
		}
	}
	
	// Check that outside region is unchanged
	REQUIRE(buffer[0] == '.');
	REQUIRE(buffer[1 * w + 1] == '.');
}

TEST_CASE("Cut rectangle operation", "[AhabSim]") {
	AhabSim sim;
	
	sim.setFieldSizeRequest(5, 5, false);
	sim.process();
	
	// Fill a region first
	sim.fillRectRequest(1, 1, 2, 2, 'X');
	sim.process();
	
	// Cut it
	sim.cutRectRequest(1, 1, 2, 2);
	sim.process();
	
	Usz h, w;
	sim.getDisplayBuffer(h, w);
	Glyph const* buffer = sim.getFieldBuffer();
	
	// Check that region is cleared
	for (Usz y = 1; y < 3; ++y) {
		for (Usz x = 1; x < 3; ++x) {
			REQUIRE(buffer[y * w + x] == '.');
		}
	}
}

TEST_CASE("Move rectangle operation", "[AhabSim]") {
	AhabSim sim;
	
	sim.setFieldSizeRequest(10, 10, false);
	sim.process();
	
	// Fill a region at (2,2)
	sim.fillRectRequest(2, 2, 2, 3, 'M');
	sim.process();
	
	// Move to (5,5)
	sim.moveRectRequest(2, 2, 2, 3, 5, 5);
	sim.process();
	
	Usz h, w;
	sim.getDisplayBuffer(h, w);
	Glyph const* buffer = sim.getFieldBuffer();
	
	// Original region should be cleared
	for (Usz y = 2; y < 4; ++y) {
		for (Usz x = 2; x < 5; ++x) {
			REQUIRE(buffer[y * w + x] == '.');
		}
	}
	
	// New region should have the content
	for (Usz y = 5; y < 7; ++y) {
		for (Usz x = 5; x < 8; ++x) {
			REQUIRE(buffer[y * w + x] == 'M');
		}
	}
}

TEST_CASE("Paste cells operation", "[AhabSim]") {
	AhabSim sim;
	
	sim.setFieldSizeRequest(10, 10, false);
	sim.process();
	
	// Create ORCA text to paste
	std::string orcaText = "ABC\nDEF";
	Usz out_h, out_w;
	bool success = sim.loadRectFromOrcaRequest(orcaText, 1, 1, out_h, out_w, false);
	REQUIRE(success == true);
	
	sim.process();
	
	Usz h, w;
	sim.getDisplayBuffer(h, w);
	Glyph const* buffer = sim.getFieldBuffer();
	
	// Check pasted content
	REQUIRE(buffer[1 * w + 1] == 'A');
	REQUIRE(buffer[1 * w + 2] == 'B');
	REQUIRE(buffer[1 * w + 3] == 'C');
	REQUIRE(buffer[2 * w + 1] == 'D');
	REQUIRE(buffer[2 * w + 2] == 'E');
	REQUIRE(buffer[2 * w + 3] == 'F');
}

TEST_CASE("Replace field operation", "[AhabSim]") {
	AhabSim sim;
	
	std::string orcaText = "12\n34";
	Usz out_h, out_w;
	bool success = sim.loadRectFromOrcaRequest(orcaText, 0, 0, out_h, out_w, true);
	REQUIRE(success == true);
	
	sim.process();
	
	REQUIRE(sim.getFieldHeight() == 2);
	REQUIRE(sim.getFieldWidth() == 2);
	
	Usz h, w;
	sim.getDisplayBuffer(h, w);
	Glyph const* buffer = sim.getFieldBuffer();
	
	REQUIRE(buffer[0] == '1');
	REQUIRE(buffer[1] == '2');
	REQUIRE(buffer[2] == '3');
	REQUIRE(buffer[3] == '4');
}

TEST_CASE("Undo and redo functionality", "[AhabSim]") {
	AhabSim sim;
	
	sim.setFieldSizeRequest(5, 5, false);
	sim.process();
	
	// Make a change with undo enabled
	sim.fillRectRequest(1, 1, 2, 2, 'X');
	sim.process();
	
	REQUIRE(sim.canUndo() == true);
	REQUIRE(sim.getUndoCount() == 1);
	
	// Undo
	sim.undoRequest();
	sim.process();
	
	Usz h, w;
	sim.getDisplayBuffer(h, w);
	Glyph const* buffer = sim.getFieldBuffer();
	
	// Should be back to '.'
	REQUIRE(buffer[1 * w + 1] == '.');
	
	// Redo
	REQUIRE(sim.canRedo() == true);
	sim.redoRequest();
	sim.process();
	
	sim.getDisplayBuffer(h, w);
	buffer = sim.getFieldBuffer();
	
	// Should have 'X' again
	REQUIRE(buffer[1 * w + 1] == 'X');
}

TEST_CASE("Undo limit enforcement", "[AhabSim]") {
	AhabSim sim;
	
	sim.setFieldSizeRequest(5, 5, false);
	sim.process();
	
	// Set a small undo limit
	sim.setUndoLimit(2);
	
	// Make 3 changes
	sim.fillRectRequest(0, 0, 1, 1, 'A');
	sim.process();
	sim.fillRectRequest(1, 1, 1, 1, 'B');
	sim.process();
	sim.fillRectRequest(2, 2, 1, 1, 'C');
	sim.process();
	
	// Should only have 2 undo entries
	REQUIRE(sim.getUndoCount() <= 2);
}

TEST_CASE("Undo after resize in same queue drain", "[AhabSim]") {
	AhabSim sim;
	
	sim.setFieldSizeRequest(3, 3, false);
	sim.process();
	
	// Make a change with undo
	sim.fillRectRequest(0, 0, 1, 1, 'X');
	sim.process();
	REQUIRE(sim.canUndo() == true);
	
	// Enqueue a resize AND an undo; both drain in the same process() call, so the
	// undo snapshot must be captured against the already-resized field (previously
	// this wrote past the too-small UI-allocated buffer, caught by ASan).
	sim.setFieldSizeRequest(5, 5, false);
	sim.undoRequest();
	sim.process();
	
	// Undo restores the pre-fill 3x3 state; the redo snapshot holds the 5x5 state
	Usz h, w;
	sim.getDisplayBuffer(h, w);
	Glyph const* buffer = sim.getFieldBuffer();
	REQUIRE(h == 3);
	REQUIRE(w == 3);
	REQUIRE(buffer[0] == '.');
	REQUIRE(sim.canRedo() == true);
}

TEST_CASE("Reset clears field and state", "[AhabSim]") {
	AhabSim sim;
	
	sim.setFieldSizeRequest(5, 5, false);
	sim.process();
	
	// Fill field
	sim.fillRectRequest(0, 0, 5, 5, 'X');
	sim.process();
	
	// Step a few times
	sim.stepRequest();
	sim.process();
	sim.stepRequest();
	sim.process();
	
	Usz tick_before = sim.getTickNumber();
	REQUIRE(tick_before > 0);
	
	// Reset
	sim.resetRequest();
	sim.process();
	
	REQUIRE(sim.getTickNumber() == 0);
	
	Usz h, w;
	sim.getDisplayBuffer(h, w);
	Glyph const* buffer = sim.getFieldBuffer();
	
	// Field should be cleared
	for (Usz i = 0; i < h * w; ++i) {
		REQUIRE(buffer[i] == '.');
	}
}

TEST_CASE("Step increments tick counter", "[AhabSim]") {
	AhabSim sim;
	
	Usz tick_before = sim.getTickNumber();
	
	sim.stepRequest();
	sim.process();
	
	REQUIRE(sim.getTickNumber() == tick_before + 1);
	
	sim.stepRequest();
	sim.process();
	
	REQUIRE(sim.getTickNumber() == tick_before + 2);
}

TEST_CASE("Random seed setting", "[AhabSim]") {
	AhabSim sim;
	
	sim.setRandomSeed(12345);
	REQUIRE(sim.getRandomSeed() == 12345);
	
	sim.setRandomSeed(67890);
	REQUIRE(sim.getRandomSeed() == 67890);
}

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

TEST_CASE("Callbacks are invoked", "[AhabSim]") {
	AhabSim sim;
	
	bool ui_tick_called = false;
	bool dsp_tick_called = false;
	bool reset_called = false;
	
	sim.setUiTickCallback([&](Field const* f) {
		ui_tick_called = true;
	});
	
	sim.setDspTickCallback([&](Oevent_list const* events) {
		dsp_tick_called = true;
	});
	
	sim.setUiResetCallback([&]() {
		reset_called = true;
	});
	
	// Step should trigger callbacks
	sim.stepRequest();
	sim.process();
	
	REQUIRE(ui_tick_called == true);
	REQUIRE(dsp_tick_called == true);
	
	// Reset should trigger reset callback
	sim.resetRequest();
	sim.process();
	
	REQUIRE(reset_called == true);
}

TEST_CASE("DSP reset callback fires on reset and field replace", "[AhabSim]") {
	AhabSim sim;

	int reset_calls = 0;
	sim.setDspResetCallback([&]() {
		reset_calls++;
	});

	// RESET command
	sim.resetRequest();
	sim.process();
	REQUIRE(reset_calls == 1);

	// REPLACE_FIELD command
	Usz h, w;
	REQUIRE(sim.loadRectFromOrcaRequest("AB\nCD", 0, 0, h, w, true) == true);
	sim.process();
	REQUIRE(reset_calls == 2);
}

TEST_CASE("Reset and field replace clear pending events", "[AhabSim]") {
	AhabSim sim;

	// Field with a banged MIDI operator ':' that emits a note event on step.
	Usz h, w;
	REQUIRE(sim.loadRectFromOrcaRequest(":04C21\n*.....", 0, 0, h, w, true) == true);
	sim.process();
	sim.stepRequest();
	sim.process();
	REQUIRE(sim.getEventCount() > 0);

	// RESET command clears pending events
	sim.resetRequest();
	sim.process();
	REQUIRE(sim.getEventCount() == 0);

	// Generate events again
	REQUIRE(sim.loadRectFromOrcaRequest(":04C21\n*.....", 0, 0, h, w, true) == true);
	sim.process();
	sim.stepRequest();
	sim.process();
	REQUIRE(sim.getEventCount() > 0);

	// Loading a new field (REPLACE_FIELD) also clears pending events
	REQUIRE(sim.loadRectFromOrcaRequest("..\n..", 0, 0, h, w, true) == true);
	sim.process();
	REQUIRE(sim.getEventCount() == 0);
}

TEST_CASE("Display buffer access is thread-safe", "[AhabSim]") {
	AhabSim sim;
	
	sim.setFieldSizeRequest(10, 10, false);
	sim.process();
	
	// Get display buffer multiple times - should be consistent
	Usz h1, w1;
	sim.getDisplayBuffer(h1, w1);
	
	Usz h2, w2;
	sim.getDisplayBuffer(h2, w2);
	REQUIRE(h1 == h2);
	REQUIRE(w1 == w2);
	REQUIRE(h1 == 10);
	REQUIRE(w1 == 10);
}

TEST_CASE("Clipping behavior for paste outside bounds", "[AhabSim]") {
	AhabSim sim;
	
	sim.setFieldSizeRequest(5, 5, false);
	sim.process();
	
	// Try to paste at edge - should clip
	std::string orcaText = "ABCDE\nFGHIJ\nKLMNO";
	Usz out_h, out_w;
	bool success = sim.loadRectFromOrcaRequest(orcaText, 3, 3, out_h, out_w, false);
	REQUIRE(success == true);
	
	sim.process();
	
	// Should have clipped to 2x2 region
	REQUIRE(out_h == 2);
	REQUIRE(out_w == 2);
	
	Usz h, w;
	sim.getDisplayBuffer(h, w);
	Glyph const* buffer = sim.getFieldBuffer();
	
	// Check that only the clipped portion was pasted
	REQUIRE(buffer[3 * w + 3] == 'A');
	REQUIRE(buffer[3 * w + 4] == 'B');
	REQUIRE(buffer[4 * w + 3] == 'F');
	REQUIRE(buffer[4 * w + 4] == 'G');
}


TEST_CASE("Successive E bang separation #426", "[AhabSim]") {
	AhabSim sim;
	
	// Use a wide row to allow E operators to propagate.
	sim.setFieldSizeRequest(1, 10, false);
	sim.process();
	Usz h, w;
	sim.getDisplayBuffer(h, w);
	REQUIRE(h == 1);
	REQUIRE(w == 10);
	Glyph const* buffer = sim.getFieldBuffer();

	// First E injection at the left edge
	sim.setGlyphRequest(0, 9, '#', Mark_flag_input, false);
	sim.setGlyphRequest(0, 0, 'E', Mark_flag_input, false);
	sim.process();
	REQUIRE(buffer[0] == 'E');
	REQUIRE(buffer[9] == '#');

	// Advance two ticks so this E moves to x=2
	for (int i = 0; i < 2; ++i) {
		sim.stepRequest();
		sim.process();
	}
	REQUIRE(buffer[2] == 'E');
	REQUIRE(buffer[0] == '.'); // Original position should be cleared
	REQUIRE(buffer[9] == '#'); // Should still be there

	// Insert a second E at the origin and watch both move with separation.
	sim.setGlyphRequest(0, 0, 'E', Mark_flag_input, false);
	sim.process();
	REQUIRE(buffer[0] == 'E');

	// Step enough ticks to get the two E's at positions 6 and 8.
	for (int i = 0; i < 6; ++i) {
		sim.stepRequest();
		sim.process();
	}
	REQUIRE(buffer[6] == 'E');
	REQUIRE(buffer[8] == 'E');

	// Next step should move them forward.
	sim.stepRequest();
	sim.process();
	REQUIRE(buffer[6] == '.'); // Previous positions should be cleared
	REQUIRE(buffer[7] == 'E'); // First E should have moved to 7
	REQUIRE(buffer[8] == '*'); // Verify bang appears at expected location

	sim.stepRequest();
	sim.process();
	// The bang of the first E triggers the bang of the second E. This behavior
	// seems not to be consistent across different implementations of ORCA. For 
	// now, we are testing how it behaves in ORCA-C and ORCA (JS).
	REQUIRE(buffer[7] == '*');
	REQUIRE(buffer[8] == '.');
}