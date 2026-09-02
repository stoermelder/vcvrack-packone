#pragma once
// Test cases for AhabSim field manipulation: ORCA text parsing, field sizing,
// glyph access, and the fill/cut/move/paste/replace editing operations.
// Included by AhabSim.test.cpp.

#include "Ahab.test.hpp"
#include "../../test/test_mock.hpp"


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

TEST_CASE("Field size clamps to maximum", "[AhabSim]") {
	AhabSim sim;

	// Requests above MAX_FIELD_HEIGHT/WIDTH must be clamped so the fixed
	// DSP-thread scratch buffer (sized 100x100) is never overflowed.
	sim.setFieldSizeRequest(3, 3, false);
	sim.process();
	sim.fillRectRequest(0, 0, 3, 3, 'X');
	sim.process();

	sim.setFieldSizeRequest(500, 500, false);
	sim.process();

	REQUIRE(sim.getFieldHeight() == AhabSim::MAX_FIELD_HEIGHT);
	REQUIRE(sim.getFieldWidth() == AhabSim::MAX_FIELD_WIDTH);

	// Content in the overlapping region survives; the rest is '.'
	Usz h, w;
	sim.getDisplayBuffer(h, w);
	Glyph const* buf = sim.getFieldBuffer();
	for (Usz y = 0; y < 3; ++y) {
		for (Usz x = 0; x < 3; ++x) {
			REQUIRE(buf[y * w + x] == 'X');
		}
	}
	REQUIRE(buf[3 * w + 3] == '.');

	// The direct DSP-side entry point clamps too.
	sim.setFieldSize(1000, 250);
	REQUIRE(sim.getFieldHeight() == AhabSim::MAX_FIELD_HEIGHT);
	REQUIRE(sim.getFieldWidth() == AhabSim::MAX_FIELD_WIDTH);

	// Stepping a max-size field after a clamped resize is safe.
	for (int i = 0; i < 4; ++i) sim.step();
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

// AhabSim's saveToFile/loadFromFileRequest/convertFileToOrca use raw stdio on
// paths (no vcv-layer seam inside AhabSim), so these tests exercise REAL file
// I/O in the OS temp dir. Temp-dir lookup and fixture I/O still route through
// the swappable vcv fs layer via the pass-through mock from test_mock.hpp.
static std::string ahabTempPath(const char* name) {
	TEST_MOCK_FS(Test::mock::MockFileAccess);
	std::string dir = vcv::fs::getTempDirectory();
	REQUIRE(!dir.empty());
	return dir + "/" + name;
}

TEST_CASE("Save to file and load back round-trip", "[AhabSim][file]") {
	std::string path = ahabTempPath("ahab_test_roundtrip.orca");
	vcv::fs::remove(path); // clean any leftover
	DEFER({ vcv::fs::remove(path); });

	AhabSim sim;
	Usz h, w;
	REQUIRE(sim.loadRectFromOrcaRequest(".aBc\nD3e4\n.5F.", 0, 0, h, w, true) == true);
	sim.process();
	REQUIRE(sim.getFieldHeight() == 3);
	REQUIRE(sim.getFieldWidth() == 4);

	REQUIRE(sim.saveToFile(path) == true);

	// The saved file is ORCA plain text with one '\n'-terminated row per line
	// (i.e. it ends with a trailing newline).
	std::string contents;
	REQUIRE(vcv::fs::read(path, contents) == true);
	REQUIRE(contents == ".aBc\nD3e4\n.5F.\n");

	// Loading into a fresh sim restores the exact grid.
	AhabSim sim2;
	REQUIRE(sim2.loadFromFileRequest(path) == true);
	sim2.process(); // applies the queued ReplaceField command

	REQUIRE(sim2.getFieldHeight() == 3);
	REQUIRE(sim2.getFieldWidth() == 4);
	Glyph const* buf = sim.getFieldBuffer();
	Glyph const* buf2 = sim2.getFieldBuffer();
	for (Usz i = 0; i < 3 * 4; ++i) {
		REQUIRE(buf2[i] == buf[i]);
	}

	// Loading pushes an undo entry so the replace can be undone.
	REQUIRE(sim2.canUndo());
}

TEST_CASE("Load a file without trailing newline", "[AhabSim][file]") {
	std::string path = ahabTempPath("ahab_test_notrail.orca");
	vcv::fs::remove(path);
	DEFER({ vcv::fs::remove(path); });

	// Hand-written fixture whose last row has no trailing '\n'.
	REQUIRE(vcv::fs::write(path, "AB\nCD") == true);

	AhabSim sim;
	REQUIRE(sim.loadFromFileRequest(path) == true);
	sim.process();

	// The last row must not be dropped.
	REQUIRE(sim.getFieldHeight() == 2);
	REQUIRE(sim.getFieldWidth() == 2);
	Glyph const* buf = sim.getFieldBuffer();
	REQUIRE(buf[0] == 'A');
	REQUIRE(buf[1] == 'B');
	REQUIRE(buf[2] == 'C');
	REQUIRE(buf[3] == 'D');
}

TEST_CASE("loadFromFileRequest failure leaves state unchanged", "[AhabSim][file]") {
	AhabSim sim;
	Usz h, w;
	REQUIRE(sim.loadRectFromOrcaRequest("X.Y\nZ.W", 0, 0, h, w, true) == true);
	sim.process();

	SECTION("Nonexistent file") {
		CHECK(sim.loadFromFileRequest("/nonexistent/dir/ahab_missing.orca") == false);
	}

	SECTION("Not a rectangle") {
		std::string path = ahabTempPath("ahab_test_badrect.orca");
		vcv::fs::remove(path);
		DEFER({ vcv::fs::remove(path); });
		REQUIRE(vcv::fs::write(path, "AB\nC\n") == true);
		CHECK(sim.loadFromFileRequest(path) == false);
	}

	// Field is untouched in every failure case.
	REQUIRE(sim.getFieldHeight() == 2);
	REQUIRE(sim.getFieldWidth() == 3);
	Glyph const* buf = sim.getFieldBuffer();
	REQUIRE(buf[0] == 'X');
	REQUIRE(buf[1] == '.');
	REQUIRE(buf[2] == 'Y');
	REQUIRE(buf[3] == 'Z');
	REQUIRE(buf[5] == 'W');
}

TEST_CASE("Static convertFileToOrca serializes a file to ORCA text", "[AhabSim][file]") {
	SECTION("Success: rows joined by newline, no trailing newline") {
		std::string path = ahabTempPath("ahab_test_convert.orca");
		vcv::fs::remove(path);
		DEFER({ vcv::fs::remove(path); });

		AhabSim sim;
		Usz h, w;
		REQUIRE(sim.loadRectFromOrcaRequest(".aBc\nD3e4", 0, 0, h, w, true) == true);
		sim.process();
		REQUIRE(sim.saveToFile(path) == true);

		std::string orca;
		Usz oh = 0, ow = 0;
		REQUIRE(AhabSim::convertFileToOrca(path, orca, oh, ow) == true);
		REQUIRE(oh == 2);
		REQUIRE(ow == 4);
		REQUIRE(orca == ".aBc\nD3e4"); // no trailing newline
	}

	SECTION("Failure: nonexistent file") {
		std::string orca;
		Usz oh = 0, ow = 0;
		REQUIRE(AhabSim::convertFileToOrca("/nonexistent/dir/ahab_missing.orca", orca, oh, ow) == false);
	}
}
