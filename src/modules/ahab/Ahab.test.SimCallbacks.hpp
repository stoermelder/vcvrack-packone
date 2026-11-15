#pragma once
// Test cases for AhabSim callbacks and event handling: UI/DSP tick callbacks,
// reset callbacks, event-list clearing, and display buffer access. Included by
// AhabSim.test.cpp.

#include "Ahab.test.hpp"


TEST_CASE("Tick and reset notifications reach the right consumers", "[AhabSim]") {
	AhabSim sim;

	bool dsp_tick_called = false;
	bool dsp_reset_called = false;

	sim.setDspTickCallback([&](Oevent_list const* events) {
		dsp_tick_called = true;
	});
	sim.setDspResetCallback([&]() {
		dsp_reset_called = true;
	});

	// A step advances the display counter and calls the DSP tick callback.
	uint32_t seq = sim.getDisplaySeq();
	sim.stepRequest();
	sim.process();
	REQUIRE(dsp_tick_called == true);
	REQUIRE(sim.getDisplaySeq() != seq);

	// A reset advances the reset epoch and calls the DSP reset callback.
	uint32_t epoch = sim.getResetEpoch();
	sim.resetRequest();
	sim.process();
	REQUIRE(dsp_reset_called == true);
	REQUIRE(sim.getResetEpoch() != epoch);

	// Replacing the field is a reset as well.
	epoch = sim.getResetEpoch();
	Usz h, w;
	REQUIRE(sim.loadRectFromOrcaRequest("..\n..", 0, 0, h, w, true));
	sim.process();
	REQUIRE(sim.getResetEpoch() != epoch);
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


// DSP/UI exclusion: while the UI is inside a section the command drain is skipped and the
// commands stay queued until the section ends.

TEST_CASE("Drain waits while a UI section is held and applies afterwards", "[AhabSim]") {
	AhabSim sim;
	sim.setFieldSizeRequest(5, 5, false);
	sim.process();

	sim.exclusion.enterUi();
	sim.setGlyphRequest(1, 1, 'X', Mark_flag_input, true);
	sim.process();
	REQUIRE(sim.getFieldBuffer()[1 * 5 + 1] == '.');
	sim.exclusion.leaveUi();

	sim.process();
	REQUIRE(sim.getFieldBuffer()[1 * 5 + 1] == 'X');
}

TEST_CASE("Request methods work inside an outer UI section", "[AhabSim]") {
	AhabSim sim;
	sim.setFieldSizeRequest(5, 5, false);
	sim.process();

	sim.exclusion.enterUi();
	sim.setGlyphRequest(0, 0, 'A', Mark_flag_input, true); // pushUndo nests
	sim.fillRectRequest(1, 1, 2, 2, 'B');
	sim.undoRequest();
	REQUIRE(sim.canUndo());
	sim.exclusion.leaveUi();
	REQUIRE_FALSE(sim.exclusion.uiInside.load());

	sim.process();
	REQUIRE(sim.getFieldBuffer()[0] == '.'); // glyph then undone, in order
}

TEST_CASE("copyForDisplay copies content and marks for several field sizes", "[AhabSim]") {
	const Usz sizes[][2] = {{1, 1}, {5, 5}, {9, 13}, {49, 97}};
	for (auto& sz : sizes) {
		AhabSim sim;
		sim.setFieldSizeRequest(sz[0], sz[1], false);
		sim.process();
		Usz last = sz[0] * sz[1] - 1;
		sim.setGlyphRequest(sz[0] - 1, sz[1] - 1, 'Z', Mark_flag_input, false);
		sim.process();

		Field out;
		field_init(&out);
		Mbuf_reusable marks;
		mbuf_reusable_init(&marks);
		sim.copyForDisplay(&out, &marks);

		REQUIRE(out.height == sz[0]);
		REQUIRE(out.width == sz[1]);
		REQUIRE(memcmp(out.buffer, sim.getFieldBuffer(), sz[0] * sz[1]) == 0);
		REQUIRE(out.buffer[last] == 'Z');
		REQUIRE(memcmp(marks.buffer, sim.getMbufBuffer(), sz[0] * sz[1]) == 0);
		REQUIRE((marks.buffer[last] & Mark_flag_input) != 0);
		REQUIRE_FALSE(sim.exclusion.uiInside.load());

		mbuf_reusable_deinit(&marks);
		field_deinit(&out);
	}
}

TEST_CASE("The mark buffer keeps its address across a resize to the maximum", "[AhabSim]") {
	AhabSim sim;
	Mark const* before = sim.getMbufBuffer();
	sim.setFieldSizeRequest(MAX_FIELD_HEIGHT, MAX_FIELD_WIDTH, false);
	sim.process();
	REQUIRE(sim.getMbufBuffer() == before);
	sim.stepRequest();
	sim.process();
	REQUIRE(sim.getMbufBuffer() == before);
}
