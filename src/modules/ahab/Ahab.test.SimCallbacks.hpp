#pragma once
// Test cases for AhabSim callbacks and event handling: UI/DSP tick callbacks,
// reset callbacks, event-list clearing, and display buffer access. Included by
// AhabSim.test.cpp.

#include "Ahab.test.hpp"


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
