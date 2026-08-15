#pragma once
// Test cases for the ORCA operators implemented by AhabSim: the custom vcvin /
// vcvout operators and the E bang propagation. Included by AhabSim.test.cpp.

#include "Ahab.test.hpp"


// custom_vcvin / custom_vcvout are C callbacks registered with the ORCA VM in
// AhabSim.cpp; they live in the plugin dylib the test binary links against.
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
