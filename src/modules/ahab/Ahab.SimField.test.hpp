#pragma once
// Test cases for AhabSim field manipulation: ORCA text parsing, field sizing,
// glyph access, and the fill/cut/move/paste/replace editing operations.
// Included by AhabSim.test.cpp.

#include "Ahab.test.hpp"


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
