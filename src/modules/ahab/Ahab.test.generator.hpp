#pragma once
// Headless unit tests for the AhabGenerator surface: ScratchPad plus the
// generate()/randomize() of var/Ahab_randomizer_review.md.
// Pure data — no live sim for the core; AhabSim appears only as the adapter's
// commit target. Included by Ahab.test.cpp.

#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "AhabScratchPad.hpp"
#include "Ahab.generator.hpp"
#include "AhabPatternScore.hpp"
#include "AhabSim.hpp"
#include "Ahab.test.hpp"

#include <set>

using StoermelderPackOne::Ahab::ScratchPad;
using StoermelderPackOne::Ahab::AhabGenerator;
using StoermelderPackOne::Ahab::AhabSim;


TEST_CASE("ScratchPad round-trips through toOrca", "[AhabGenerator]") {
	ScratchPad b(2, 3);
	REQUIRE(b.height() == 2);
	REQUIRE(b.width() == 3);

	// Fresh buffer is all '.' and not dirty.
	REQUIRE(b.toOrca() == "...\n...");
	REQUIRE(b.dirty() == false);

	b.set(0, 0, 'A');
	REQUIRE(b.toOrca() == "A..\n...");
	REQUIRE(b.dirty() == true);
}

TEST_CASE("ScratchPad drops out-of-bounds writes", "[AhabGenerator]") {
	ScratchPad b(2, 3);

	// Selection-local bounds: writes past the rect are dropped and reported.
	CHECK_FALSE(b.set(2, 0, 'X')); // row past bottom
	CHECK_FALSE(b.set(0, 3, 'X')); // column past right edge
	CHECK_FALSE(b.set(5, 5, 'X'));

	// Nothing was written, so the buffer stays clean AND undirty.
	CHECK(b.toOrca() == "...\n...");
	CHECK(b.dirty() == false);
}

TEST_CASE("ScratchPad get reads back writes and defaults to '.'", "[AhabGenerator]") {
	ScratchPad b(3, 4);
	b.set(1, 2, ':');
	REQUIRE(b.get(1, 2) == ':');
	REQUIRE(b.get(0, 0) == '.');

	// Out-of-bounds reads are safe and yield '.'
	REQUIRE(b.get(3, 0) == '.');
	REQUIRE(b.get(0, 4) == '.');
}

TEST_CASE("Randomizer is deterministic for a fixed seed", "[AhabGenerator]") {
	AhabGenerator a(12345), b(12345);
	AhabGenerator::Config cfg;
	REQUIRE(a.generate(12, 24, cfg).toOrca() == b.generate(12, 24, cfg).toOrca());

	// A non-zero cfg.seed overrides the instance seed per call, so even
	// differently-seeded instances agree.
	AhabGenerator::Config seeded;
	seeded.seed = 777;
	REQUIRE(a.generate(12, 24, seeded).toOrca() == b.generate(12, 24, seeded).toOrca());
	// getSeed() describes the ADOPTED attempt — the original seed plus
	// the retry offset when a re-roll won (bounded by kMaxAttempts - 1).
	{
		CATCH_INFO("adopted seed = " << a.getSeed());
		REQUIRE(a.getSeed() >= 777);
		REQUIRE(a.getSeed() <= 777 + 7);
		REQUIRE(b.getSeed() == a.getSeed());
	}

	// Two different seeds usually differ (sanity that the seed is actually
	// used). Gate OFF: under the retry loop adjacent seeds legitimately
	// converge (777's chain may adopt 778), so seed sensitivity is a property
	// of the raw generator.
	AhabGenerator::Config other;
	other.seed = 778;
	other.qualityGate = false;
	AhabGenerator::Config seededRaw = seeded;
	seededRaw.qualityGate = false;
	CHECK(a.generate(12, 24, other).toOrca() != a.generate(12, 24, seededRaw).toOrca());
}

TEST_CASE("generate honours size and dirty flag", "[AhabGenerator]") {
	AhabGenerator r(42);

	// Density 0 places nothing: buffer stays clean AND undirty.
	AhabGenerator::Config empty;
	empty.density = 0.0f;
	ScratchPad e = r.generate(8, 12, empty);
	REQUIRE(e.toOrca().size() == 8 * 12 + 7); // 8 rows, 7 newlines
	REQUIRE_FALSE(e.dirty());

	// Density 1 fills the arrangement with something.
	AhabGenerator::Config full;
	full.density = 1.0f;
	ScratchPad f = r.generate(8, 12, full);
	REQUIRE(f.toOrca().size() == 8 * 12 + 7);
	REQUIRE(f.dirty());
}

TEST_CASE("generate matches the pre-conversion generators byte for byte", "[AhabGenerator]") {
	// converting trySet(AhabSim*) -> buf.set() changed no
	// glyph placement. Verified exhaustively against the git-HEAD generators
	// with a differential harness (3 seeds x 7 sizes x 4 densities = 84/84
	// identical). This golden snapshot pins one such case permanently, so the
	// geometry fixes show up as deliberate updates of this string.
	// qualityGate is OFF: this measures the RAW generator.
	AhabGenerator r(12345u);
	AhabGenerator::Config cfg;
	cfg.density = 1.0f;
	cfg.qualityGate = false;
	ScratchPad buf = r.generate(6, 9, cfg);

	// Simple-pattern path (w < 10): three 4-wide patterns on alternating rows.
	// (Regenerated for the cleanup pass: varPool_/chanPool_ lost their
	// documented shuffles in the graph-plan rework; restoring them shifts
	// the RNG stream.)
	REQUIRE(buf.toOrca() ==
		".0R7..77O\n"
		".........\n"
		".tVv...Vt\n"
		".........\n"
		".4D2..8C3\n"
		".........");
}

TEST_CASE("Randomizer commits the whole region in one command", "[AhabGenerator]") {
	// the old per-glyph path overflowed the 512-slot
	// ring buffer and silently truncated on large fields. One paste command
	// cannot truncate, so after ONE process() the field must match the pure
	// core exactly.
	AhabSim sim;
	sim.setFieldSizeRequest(100, 100, false);
	sim.process();

	AhabGenerator r(99);
	AhabGenerator::Config cfg;
	cfg.density = 1.0f;
	REQUIRE(r.randomize(&sim, 0, 0, 100, 100, cfg));
	sim.process(); // a single process() applies the whole thing

	AhabGenerator ref(99);
	std::string expected = ref.generate(100, 100, cfg).toOrca();

	Usz h = sim.getFieldHeight(), w = sim.getFieldWidth();
	Glyph const* fieldBuf = sim.getFieldBuffer();
	std::string actual;
	actual.reserve(expected.size());
	for (Usz y = 0; y < h; ++y) {
		actual.append(fieldBuf + (size_t)y * w, w);
		if (y + 1 < h) actual.push_back('\n');
	}
	REQUIRE(actual == expected);
}

TEST_CASE("Randomizer pushes no undo entry when nothing is generated", "[AhabGenerator]") {
	AhabSim sim;
	sim.setFieldSizeRequest(4, 4, false);
	sim.process();
	Usz before = sim.getUndoCount();

	AhabGenerator r(1);
	AhabGenerator::Config cfg;
	cfg.density = 0.0f; // generates nothing
	REQUIRE_FALSE(r.randomize(&sim, 0, 0, 4, 4, cfg));
	sim.process();
	REQUIRE(sim.getUndoCount() == before);
}

TEST_CASE("randomize rejects degenerate rects", "[AhabGenerator]") {
	AhabSim sim;
	sim.setFieldSizeRequest(10, 10, false);
	sim.process();

	AhabGenerator r(5);
	AhabGenerator::Config cfg;
	CHECK_FALSE(r.randomize(nullptr, 0, 0, 4, 4, cfg)); // no sim
	CHECK_FALSE(r.randomize(&sim, 10, 0, 4, 4, cfg));   // start row past field
	CHECK_FALSE(r.randomize(&sim, 0, 10, 4, 4, cfg));   // start col past field
	CHECK_FALSE(r.randomize(&sim, 0, 0, 0, 4, cfg));    // zero height

	// Oversized selections are clamped to the field instead of rejected.
	cfg.density = 1.0f;
	CHECK(r.randomize(&sim, 5, 5, 500, 500, cfg));
}

TEST_CASE("Randomizer output produces MIDI events", "[AhabGenerator]") {
	// The load-bearing assertion: any generated pattern must actually make
	// sound. This catches bang-overwrites, missing note cells and misaligned
	// V-reads — it failed before the Phase 2 geometry fixes.
	for (uint32_t seed = 1; seed <= 32; ++seed) {
		AhabGenerator r(seed);
		AhabGenerator::Config cfg;
		cfg.density = 0.6f;
		ScratchPad buf = r.generate(16, 32, cfg);

		AhabSim sim;
		sim.setFieldSizeRequest(16, 32, false);
		sim.process();

		Usz outH = 0, outW = 0;
		REQUIRE(sim.loadRectFromOrcaRequest(buf.toOrca(), 0, 0, outH, outW, false));
		sim.process();

		// Run long enough for slow clocks/delays to come round.
		Usz events = 0;
		for (int i = 0; i < 64; ++i) {
			sim.stepRequest();
			sim.process();
			events += sim.getEventCount();
		}
		CATCH_INFO("seed = " << seed << "\n" << buf.toOrca());
		REQUIRE(events > 0);
	}
}

// Serialize the sim's live field to ORCA text (test helper).
static std::string fieldToOrca(AhabSim const& sim) {
	Usz h = sim.getFieldHeight(), w = sim.getFieldWidth();
	Glyph const* buf = sim.getFieldBuffer();
	std::string out;
	out.reserve((size_t)h * (w + 1));
	for (Usz y = 0; y < h; ++y) {
		out.append(buf + (size_t)y * w, w);
		if (y + 1 < h) out.push_back('\n');
	}
	return out;
}

TEST_CASE("Randomize (same seed) reproduces the generated pattern", "[AhabGenerator]") {
	// the widget remembers the seed that produced the last result
	// (persisted by the module) and re-running with it is deterministic.
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);
	AhabSimWidget* w = new AhabSimWidget();
	w->module = m;
	DEFER({
		delete w;
		Test::unregisterModule(m);
		Test::destroyModule(m);
	});

	m->sim->setFieldSizeRequest(16, 32, false);
	m->process({});
	w->editorState.setSelection(0, 0, 16, 32);

	w->simRandomize(0.5f);
	REQUIRE(m->lastGenerator.seed != 0);
	std::string first = fieldToOrca(*m->sim);

	// Same seed + same rect + same density => identical output.
	w->simRandomize(0.5f, m->lastGenerator.seed);
	std::string second = fieldToOrca(*m->sim);
	REQUIRE(second == first);
}

TEST_CASE("Euclidean voices vary their modulus", "[AhabGenerator]") {
	// before the fix every uclid voice passed max='8', so all of
	// them shared a period of 8 and phase-locked into the same rhythm.
	std::set<char> maxes;
	for (uint32_t seed = 1; seed <= 16; ++seed) {
		AhabGenerator r(seed);
		AhabGenerator::Config cfg;
		cfg.density = 1.0f;
		std::string orca = r.generate(24, 40, cfg).toOrca();
		for (size_t i = 0; i + 1 < orca.size(); ++i)
			if (orca[i] == 'U') maxes.insert(orca[i + 1]);
	}
	REQUIRE(maxes.size() >= 2);
}

TEST_CASE("Clock bus: reserved variables written once, read by voices", "[AhabGenerator]") {
	// the top-of-field bus publishes q/w/e(/r) — each exactly once
	// (its writer); voices READ them (".Vq" pattern) and never write them.
	// The legacy master-clock variable g is gone entirely.
	static std::string const kBus = "qwer";
	Usz totalReads = 0;
	for (uint32_t seed = 1; seed <= 16; ++seed) {
		AhabGenerator r(seed);
		AhabGenerator::Config cfg;
		cfg.density = 0.6f; // bus is placed at this density
		ScratchPad buf = r.generate(24, 40, cfg);
		std::string const& bus = r.getBusVars();

		CATCH_INFO("seed = " << seed << " bus = " << bus << "\n" << buf.toOrca());
		REQUIRE(bus.size() >= 1);
		REQUIRE(bus.size() <= 4);

		// Each placed bus var has exactly one XV write pair; no other bus var
		// is ever written; g is never written.
		std::string orca = buf.toOrca();
		for (char v : kBus) {
			size_t writes = 0;
			for (size_t i = 0; i + 1 < orca.size(); ++i)
				if (orca[i] == v && orca[i + 1] == 'V') ++writes;
			if (bus.find(v) == std::string::npos) REQUIRE(writes == 0);
			else REQUIRE(writes == 1);
		}
		{
			size_t gWrites = 0;
			for (size_t i = 0; i + 1 < orca.size(); ++i)
				if (orca[i] == 'g' && orca[i + 1] == 'V') ++gWrites;
			REQUIRE(gWrites == 0);
		}

		// Voices read the bus: a '.','V',busvar triple on one row.
		Usz reads = 0;
		for (Usz y = 0; y + 1 < buf.height(); ++y) {
			for (Usz x = 0; x + 3 < buf.width(); ++x) {
				if (buf.get(y, x + 1) == '.' && buf.get(y, x + 2) == 'V'
					&& bus.find(buf.get(y, x + 3)) != std::string::npos)
					++reads;
			}
		}
		totalReads += reads;
	}
	REQUIRE(totalReads > 0); // across seeds, voices really follow the bus
}

TEST_CASE("fillScaleWalk emits valid ORCA note glyphs", "[AhabGenerator]") {
	// scales are semitone tables converted with sim.c's note-glyph
	// mapping, so every glyph must be a note the ':' operator accepts.
	static std::string const kValid = "CcDdEFfGgAaB";
	for (uint32_t seed = 1; seed <= 16; ++seed) {
		std::mt19937 rng(seed);
		Usz const lens[] = {4, 8, 13};
		for (Usz len : lens) {
			std::string notes;
			AhabGenerator::fillScaleWalk(notes, len, rng);
			REQUIRE(notes.size() == len);
			for (char c : notes)
				REQUIRE(kValid.find(c) != std::string::npos);
		}
	}
}

TEST_CASE("scorePattern measures a known one-shot pattern", "[AhabGenerator]") {
	// ':04C21' banged once by a literal '*': exactly one Note-On (ch 0,
	// octave 4, note C = 48, velocity 15) on tick 0, then silence — the bang
	// is consumed after one fire.
	ScratchPad buf(2, 6);
	const char* rows[2] = {":04C21", "*....."};
	for (Usz y = 0; y < 2; ++y)
		for (Usz x = 0; x < 6; ++x)
			REQUIRE(buf.set(y, x, rows[y][x]));

	PatternScore s = scorePattern(buf, /*ticks=*/8);

	CHECK_FALSE(s.silent());
	CHECK(s.totalEvents == 1);
	CHECK(s.noteEvents == 1);
	CHECK(s.activeTicks == 1);
	CHECK(s.firstEventTick == 0);
	CHECK(s.distinctPitches == 1);   // 4 * 12 + C(0) = 48
	CHECK(s.distinctChannels == 1);
	CHECK(s.longestSilence == 7);    // ticks 1..7 mute
	CHECK(s.density == Catch::Approx(1.f / 8.f));
}

TEST_CASE("scorePattern reports silence for an empty field", "[AhabGenerator]") {
	ScratchPad buf(2, 6); // all '.'
	PatternScore s = scorePattern(buf, /*ticks=*/8);

	CHECK(s.silent());
	CHECK(s.totalEvents == 0);
	CHECK(s.noteEvents == 0);
	CHECK(s.activeTicks == 0);
	CHECK(s.firstEventTick == PatternScore::kNever);
	CHECK(s.longestSilence == 8);
	CHECK(s.density == 0.f);
}

TEST_CASE("scorePattern is deterministic", "[AhabGenerator]") {
	// Same buffer -> same score, including through R operators (seed pinned).
	AhabGenerator r(3);
	AhabGenerator::Config cfg;
	cfg.density = 0.5f;
	ScratchPad buf = r.generate(16, 32, cfg);

	PatternScore a = scorePattern(buf, /*ticks=*/32);
	PatternScore b = scorePattern(buf, /*ticks=*/32);

	CHECK(a.totalEvents == b.totalEvents);
	CHECK(a.noteEvents == b.noteEvents);
	CHECK(a.activeTicks == b.activeTicks);
	CHECK(a.distinctPitches == b.distinctPitches);
	CHECK(a.distinctChannels == b.distinctChannels);
	CHECK(a.firstEventTick == b.firstEventTick);
	CHECK(a.longestSilence == b.longestSilence);
}

TEST_CASE("Quality gate: generated patterns meet minimum standards", "[AhabGenerator][gate]") {
	// sweeps seeds and asserts on AGGREGATE statistics, never on any
	// single seed — individual seeds are allowed to be boring; the
	// distribution is what must be healthy. Exactly one hard assertion family
	// (a silent pattern is a bug, not a taste question); everything else is a
	// fraction-of-seeds bound to be ratcheted upward over time.
	//
	// (64 seeds, 16x32, density
	// 0.5, gate OFF - measuring the raw generator; 16x32 plans 1 dependent
	// round): silent=0, thin=0, fewChannels=0, badDensity=0, lateFirst=0,
	// density range [8%, 88%], worstSilence=13, worstFirst=0.
	// Sweep cost: ~0.02 s. Thresholds sit deliberately above these so
	// ordinary seed-to-seed variation does not flake. Re-measure after
	// planner changes that shift the distribution.
	constexpr Usz kSeeds = 64;
	constexpr Usz kTicks = 128;
	Usz silent = 0, thin = 0, fewChannels = 0, badDensity = 0, lateFirst = 0;
	Usz worstSilence = 0, worstFirst = 0;
	std::string worstField;

	for (uint32_t seed = 1; seed <= kSeeds; ++seed) {
		AhabGenerator r(seed);
		AhabGenerator::Config cfg;
		cfg.density = 0.5f;
		// Gate OFF per plan 6.3: measure the raw generator, not the retry
		// loop papering over defects.
		cfg.qualityGate = false;
		ScratchPad buf = r.generate(16, 32, cfg);
		PatternScore s = scorePattern(buf, kTicks);

		if (s.silent()) {
			++silent;
			if (worstField.empty()) worstField = buf.toOrca();
		}
		// a variable read with no preceding write always
		// yields '.' (vars_slots are wiped every tick). Static, deterministic,
		// and a bug by definition — assert per seed, not as a fraction.
		if (s.danglingReads > 0) {
			CATCH_INFO("danglingReads = " << s.danglingReads << " seed = " << seed << "\n" << buf.toOrca());
			REQUIRE(s.danglingReads == 0);
		}
		if (s.distinctPitches < 2) ++thin;
		if (s.distinctChannels < 2) ++fewChannels;
		if (s.density < 0.05f || s.density >= 0.999f) ++badDensity;
		if (s.firstEventTick >= 64) ++lateFirst;
		if (s.longestSilence > worstSilence) worstSilence = s.longestSilence;
		if (s.firstEventTick != PatternScore::kNever && s.firstEventTick > worstFirst) worstFirst = s.firstEventTick;
	}

	CATCH_INFO("silent=" << silent << "/" << kSeeds << " thin=" << thin
		<< " fewChannels=" << fewChannels << " badDensity=" << badDensity
		<< " lateFirst=" << lateFirst << " worstSilence=" << worstSilence
		<< " worstFirst=" << worstFirst
		<< (worstField.empty() ? "" : "\nfirst silent field:\n" + worstField));

	// HARD floors: the code works, full stop.
	REQUIRE(silent == 0);
	REQUIRE(lateFirst == 0); // something sounds within the first 64 ticks

	// SOFT floors: tuned to current behaviour, ratcheted upward over time.
	REQUIRE(thin <= kSeeds / 10);            // <=10% single-pitch drones
	REQUIRE(fewChannels <= kSeeds / 2);      // >=50% multi-channel arrangements
	REQUIRE(badDensity <= kSeeds / 10);      // <=10% degenerate sparsity/walls
	REQUIRE(worstSilence <= 64);             // no seed goes >64 ticks mute
}

TEST_CASE("Quality gate retry chain is deterministic", "[AhabGenerator][gate]") {
	// retries derive their seeds from the original seed, so two
	// instances with the same seed produce identical output AND report the
	// same final seed, whichever attempt was adopted.
	AhabGenerator a(9);
	AhabGenerator b(9);
	AhabGenerator::Config cfg;
	cfg.density = 0.5f; // qualityGate defaults ON
	ScratchPad fa = a.generate(16, 32, cfg);
	ScratchPad fb = b.generate(16, 32, cfg);

	REQUIRE(fa.toOrca() == fb.toOrca());
	REQUIRE(a.getSeed() == b.getSeed());
	REQUIRE(a.getSeed() != 0);

	// The same instance repeats deterministically when re-seeded via cfg.
	AhabGenerator::Config seeded = cfg;
	seeded.seed = 9;
	REQUIRE(a.generate(16, 32, seeded).toOrca() == fb.toOrca());
}

TEST_CASE("Quality gate always returns an attempt", "[AhabGenerator][gate]") {
	// A rect too small to hold any sounding pattern can never pass
	// acceptable(); the loop must still terminate after its bounded attempts
	// and return the best (possibly silent) attempt — never nothing.
	AhabGenerator r(2);
	AhabGenerator::Config cfg;
	cfg.density = 0.5f;
	ScratchPad buf = r.generate(2, 3, cfg);

	REQUIRE(buf.height() == 2);
	REQUIRE(buf.width() == 3);
	REQUIRE(buf.toOrca().size() == 2 * 3 + 1);
}

TEST_CASE("Quality gate keeps the seed when no attempt improves", "[AhabGenerator][gate]") {
	// Density 0 makes EVERY attempt an empty field, so nothing can be adopted
	// (a candidate never beats the best). Pins two contracts: the loop
	// terminates bounded, and getSeed() still reports the original seed when
	// no retry was adopted.
	AhabGenerator r(7);
	AhabGenerator::Config cfg;
	cfg.density = 0.0f;
	cfg.seed = 424242;
	ScratchPad buf = r.generate(16, 32, cfg);

	CHECK_FALSE(buf.dirty());
	REQUIRE(r.getSeed() == 424242);
}

TEST_CASE("Shared identity: every modulus divides the patch bar", "[AhabGenerator]") {
	// one bar length per patch; clock/delay/uclid moduli are its
	// divisors, so all voices lock to the same groove. 'U' is safe to scan
	// for (no note glyph or lowercase base36 data is an uppercase U).
	for (uint32_t seed = 1; seed <= 16; ++seed) {
		AhabGenerator r(seed);
		AhabGenerator::Config cfg;
		cfg.density = 1.0f;
		cfg.qualityGate = false;
		ScratchPad buf = r.generate(24, 40, cfg);
		Usz bar = r.getBarMod();
		CATCH_INFO("seed = " << seed << " bar = " << bar << "\n" << buf.toOrca());
		REQUIRE((bar == 8 || bar == 12));

		std::string orca = buf.toOrca();
		int checked = 0;
		for (size_t i = 0; i + 1 < orca.size(); ++i) {
			if (orca[i] == 'U') {
				char c = orca[i + 1];
				Usz v = (c >= '0' && c <= '9') ? Usz(c - '0') : (c >= 'a' && c <= 'z' ? Usz(c - 'a' + 10) : 0);
				REQUIRE(v > 0);
				REQUIRE(bar % v == 0);
				++checked;
			}
		}
		REQUIRE(checked > 0); // dense arrangements always contain uclid voices
	}
}

TEST_CASE("danglingReads: variable-flow scan semantics", "[AhabGenerator][gate]") {
	// the scan mirrors orca_run's order (top-to-bottom,
	// left-to-right) and V's write/read modes. vars_slots are wiped every
	// tick, so a read only sees writes above or to its left.

	// Write above read: healthy.
	{
		ScratchPad buf(2, 4);
		buf.set(0, 0, 'a'); buf.set(0, 1, 'V'); buf.set(0, 2, '3');
		buf.set(1, 0, '.'); buf.set(1, 1, 'V'); buf.set(1, 2, 'a');
		PatternScore s = scorePattern(buf, 2);
		CHECK(s.danglingReads == 0);
		CHECK(s.varsWritten == 1);
		CHECK(s.varsRead == 1);
	}

	// Read ABOVE write: dangling — the read can never see it.
	{
		ScratchPad buf(2, 4);
		buf.set(0, 0, '.'); buf.set(0, 1, 'V'); buf.set(0, 2, 'a');
		buf.set(1, 0, 'a'); buf.set(1, 1, 'V'); buf.set(1, 2, '3');
		PatternScore s = scorePattern(buf, 2);
		CHECK(s.danglingReads == 1);
	}

	// Same row: write LEFT of read is visible (left-to-right order).
	{
		ScratchPad buf(1, 7);
		buf.set(0, 0, 'a'); buf.set(0, 1, 'V'); buf.set(0, 2, '3');
		buf.set(0, 4, 'V'); buf.set(0, 5, 'a');
		PatternScore s = scorePattern(buf, 2);
		CHECK(s.danglingReads == 0);
	}

	// Same row: read LEFT of write is dangling.
	{
		ScratchPad buf(1, 6);
		buf.set(0, 0, '.'); buf.set(0, 1, 'V'); buf.set(0, 2, 'a');
		buf.set(0, 3, 'b'); buf.set(0, 4, 'V'); buf.set(0, 5, '3');
		PatternScore s = scorePattern(buf, 2);
		CHECK(s.danglingReads == 1);
		CHECK(s.varsWritten == 1); // b
		CHECK(s.varsRead == 1);    // a
	}

	// A write with '.' value still counts as a write (stores '.', but the
	// var IS written).
	{
		ScratchPad buf(2, 4);
		buf.set(0, 0, 'a'); buf.set(0, 1, 'V'); buf.set(0, 2, '.');
		buf.set(1, 0, '.'); buf.set(1, 1, 'V'); buf.set(1, 2, 'a');
		PatternScore s = scorePattern(buf, 2);
		CHECK(s.danglingReads == 0);
		CHECK(s.varsWritten == 1);
	}

	// Non-variable glyphs around a V are ignored (no crash, no counts).
	{
		ScratchPad buf(1, 3);
		buf.set(0, 0, '*'); buf.set(0, 1, 'V'); buf.set(0, 2, '*');
		PatternScore s = scorePattern(buf, 2);
		CHECK(s.varsWritten == 0);
		CHECK(s.varsRead == 0);
		CHECK(s.danglingReads == 0);
	}
}

TEST_CASE("Scorer sees CC events and separates modulation from a stuck value", "[AhabGenerator][gate]") {
	// Vocabulary plan Step 2a: '!' rows were invisible to the scorer, so a
	// modulation voice that silently failed could never be re-rolled. Both
	// grids bang a midicc every tick; they differ only in the VALUE cell.
	//
	//   r1: .D1...C8..     D(mod 1) re-bangs every tick; C feeds
	//                      glyph_of(tick%8) into the value cell
	//   r2: *.!17....      channel 1, control 7, value from above
	// (A hand-placed '*' cannot be used: the bang operator self-clears when
	// scanned, so it must be rewritten - and lock-marked - every tick.)
	{
		ScratchPad buf(4, 9);
		buf.set(1, 1, 'D'); buf.set(1, 2, '1');
		buf.set(1, 5, 'C'); buf.set(1, 6, '8');
		buf.set(2, 2, '!'); buf.set(2, 3, '1'); buf.set(2, 4, '7');
		PatternScore s = scorePattern(buf, 16);
		CATCH_INFO("cc = " << s.ccEvents << " distinct = " << s.distinctCcValues);
		REQUIRE(s.ccEvents == 16);        // banged every tick
		REQUIRE(s.distinctCcValues == 8); // values 0..7 cycle twice
		REQUIRE(s.noteEvents == 0);
		REQUIRE(s.silent());              // pure CC is still silent (plan §2a)
	}

	// Stuck value: the value cell is a V-read of an UNWRITTEN variable, so
	// it resolves to '.' forever. midicc does NOT early-return on '.' — it
	// emits index_of('.')*127/35 == 0 every tick. ccEvents > 0 with
	// distinctCcValues <= 1 is exactly the failure signature that Step 2b's
	// tests must reject.
	{
		ScratchPad buf(4, 9);
		buf.set(1, 1, 'D'); buf.set(1, 2, '1');
		buf.set(1, 5, 'V'); buf.set(1, 6, 'z');
		buf.set(2, 2, '!'); buf.set(2, 3, '1'); buf.set(2, 4, '7');
		PatternScore s = scorePattern(buf, 16);
		CATCH_INFO("cc = " << s.ccEvents << " distinct = " << s.distinctCcValues
			<< " dangling = " << s.danglingReads);
		REQUIRE(s.ccEvents == 16);     // emitted -- but constant
		REQUIRE(s.distinctCcValues <= 1);
		REQUIRE(s.danglingReads == 1); // the static scan does catch this one
	}

	// Notes-only patterns stay invisible to the CC counters. '+' is the
	// fork's pending-bang: scanned before the ':' to its right, it turns
	// into '*' so the ':' fires exactly once. (A hand-placed '*' cannot be
	// used — the bang operator self-clears when scanned — and nothing may
	// sit above the bang cell, or its writes clobber it.)
	{
		ScratchPad buf(1, 8);
		buf.set(0, 0, '+');
		buf.set(0, 1, ':'); buf.set(0, 2, '2'); buf.set(0, 3, '4'); buf.set(0, 4, 'E');
		buf.set(0, 5, 'f'); buf.set(0, 6, '4');
		PatternScore s = scorePattern(buf, 8);
		REQUIRE(s.noteEvents == 1);
		REQUIRE(s.ccEvents == 0);
		REQUIRE(s.distinctCcValues == 0);
	}
}

TEST_CASE("Modulation tail maps a published pitch onto a CC", "[AhabGenerator][gate]") {
	// The tail's value cell is fed by a V-read of
	// the producer's variable, so the CC must TRACK it. The producer here is
	// a bus-style writer: a C clock pokes its counter into the V-write's
	// value cell, so var p cycles through 8 glyphs.
	//
	// Sampling is deterministic: the tail's D bangs every 2nd tick while the
	// counter advances every tick through 8 glyphs, so exactly the even
	// phases are sampled -> 4 distinct CC values
	// (idx*127/35 for idx 0/2/4/6 = 0/7/14/21).
	ScratchPad buf(5, 10);
	buf.set(0, 1, '.'); buf.set(0, 2, '1'); buf.set(0, 3, 'C'); buf.set(0, 4, '8');
	buf.set(1, 1, 'p'); buf.set(1, 2, 'V'); buf.set(1, 3, '.');

	AhabGenerator rr(5);
	AhabGenerator::Extent e = rr.placeModulationTail(buf, 2, 0, 3, 10, '3', '4', 'p');
	REQUIRE(e.h == 2);
	REQUIRE(e.w == 7);

	AhabSim sim;
	sim.setFieldSizeRequest(buf.height(), buf.width(), false);
	sim.process();
	sim.setRandomSeed(1);
	Usz oh = 0, ow = 0;
	REQUIRE(sim.loadRectFromOrcaRequest(buf.toOrca(), 0, 0, oh, ow, false));
	sim.process();

	Usz cc = 0;
	std::set<int> values;
	for (int t = 0; t < 32; ++t) {
		sim.stepRequest();
		sim.process();
		Oevent_list const* ev = sim.getEvents();
		for (Usz i = 0; i < ev->count; ++i) {
			Oevent const& o = ev->buffer[i];
			if (o.any.oevent_type != Oevent_type_midi_cc) continue;
			++cc;
			REQUIRE(o.midi_cc.channel == 3);
			REQUIRE(o.midi_cc.control == 4);
			values.insert(o.midi_cc.value);
		}
	}
	CATCH_INFO("cc = " << cc << " distinct = " << values.size());
	REQUIRE(cc > 0);
	REQUIRE(values.size() == 4); // tracks the clock: not a stuck value

	PatternScore s = scorePattern(buf, 16);
	REQUIRE(s.danglingReads == 0);
	REQUIRE(s.ccEvents > 0);
	REQUIRE(s.distinctCcValues >= 2);
}

TEST_CASE("Plans attach Modulation edges to Pitch consumers", "[AhabGenerator]") {
	// Occasionally a dependent carries a Modulation
	// edge — an extra edge on an EXISTING node, never a new one. It must ride
	// the node's own Pitch producer and draw unique low control numbers.
	using StoermelderPackOne::Ahab::Edge;
	using StoermelderPackOne::Ahab::VoiceNode;

	Usz seedsWithMod = 0;
	for (uint32_t seed = 1; seed <= 32; ++seed) {
		AhabGenerator r(seed);
		AhabGenerator::Config warm;
		warm.density = 0.6f;
		warm.qualityGate = false;
		r.generate(24, 40, warm); // primes varPool_ / chanPool_

		std::vector<VoiceNode> const plan = r.planArrangement(24, 40, 0.6f, 3);
		bool modHere = false;
		std::set<char> controls;
		for (size_t i = 0; i < plan.size(); ++i) {
			VoiceNode const& vn = plan[i];
			for (Edge const& e : vn.inputs) {
				if (e.kind != Edge::Modulation) continue;
				modHere = true;
				bool hasPitchFromSame = false;
				for (Edge const& p : vn.inputs)
					if (p.kind == Edge::Pitch && p.from == e.from) hasPitchFromSame = true;
				REQUIRE(hasPitchFromSame);
				REQUIRE(e.param >= '0');
				REQUIRE(e.param <= '3');
				REQUIRE(controls.insert(e.param).second); // no two tails share a CC
			}
		}
		if (modHere) ++seedsWithMod;
	}
	CATCH_INFO("seeds with modulation edges: " << seedsWithMod << "/32");
	REQUIRE(seedsWithMod >= 4); // occasional but not rare
}

TEST_CASE("Modulation edges emit varying CC in generated fields", "[AhabGenerator][gate]") {
	// Patches whose plan carries a
	// Modulation edge score ccEvents > 0 AND distinctCcValues >= 2 — a
	// placed-but-stuck tail is exactly the failure Step 2a exists to catch.
	// '!' is emitted nowhere else in AhabGenerator, so its presence
	// fingerprints a placed tail. Notes must survive: modulation decorates
	// voices, it never replaces them.
	Usz fieldsWithTail = 0;
	for (uint32_t seed = 1; seed <= 32; ++seed) {
		AhabGenerator r(seed);
		AhabGenerator::Config cfg;
		cfg.density = 0.6f;
		ScratchPad buf = r.generate(24, 40, cfg);
		std::string const orca = buf.toOrca();

		PatternScore s = scorePattern(buf, 64);
		CATCH_INFO("seed = " << seed << " cc = " << s.ccEvents
			<< " distinctCc = " << s.distinctCcValues
			<< " notes = " << s.noteEvents << "\n" << orca);
		REQUIRE(s.danglingReads == 0);
		REQUIRE(s.noteEvents > 0); // modulation must not cost the notes

		if (orca.find('!') == std::string::npos) continue;
		++fieldsWithTail;
		REQUIRE(s.ccEvents > 0);
		REQUIRE(s.distinctCcValues >= 2);
	}
	CATCH_INFO("fields with placed tails: " << fieldsWithTail << "/32");
	REQUIRE(fieldsWithTail >= 1); // the capability is reachable, not dead code
}

TEST_CASE("Chord voices take live velocity through K", "[AhabGenerator][gate]") {
	// With an Operand(kOpVelocity) edge, a voice's
	// velocity cell is fed by a K row reading the producer's published pitch
	// — same footprint, accents instead of a fixed literal. Driven through
	// the public placeChordVoice; the lead's note+velocity K variant is
	// covered by the generated-field test below ('K' fingerprint).
	//
	// The producer is a bus-style writer whose counter cycles mod 7 and the
	// chord bangs every tick (rate 1, mod 1), so velocities follow the
	// counter directly: six of seven ticks emit — velocity index 0 drops
	// the note (midi treats it as a note-off) — mapping to 7..47.
	ScratchPad buf(7, 10);
	buf.set(0, 1, '.'); buf.set(0, 2, '1'); buf.set(0, 3, 'C'); buf.set(0, 4, '7');
	buf.set(1, 1, 'p'); buf.set(1, 2, 'V'); buf.set(1, 3, '.');

	AhabGenerator rr(3);
	AhabGenerator::Extent e = rr.placeChordVoice(buf, 2, 0, 4, 10, '1', '1', '4', '4', {'E', 'G'}, 'p');
	REQUIRE(e.h == 4);
	REQUIRE(e.w == 9);

	AhabSim sim;
	sim.setFieldSizeRequest(buf.height(), buf.width(), false);
	sim.process();
	sim.setRandomSeed(1);
	Usz oh = 0, ow = 0;
	REQUIRE(sim.loadRectFromOrcaRequest(buf.toOrca(), 0, 0, oh, ow, false));
	sim.process();

	Usz notes = 0;
	std::set<int> vels;
	for (int t = 0; t < 28; ++t) {
		sim.stepRequest();
		sim.process();
		Oevent_list const* ev = sim.getEvents();
		for (Usz i = 0; i < ev->count; ++i) {
			Oevent const& o = ev->buffer[i];
			if (o.any.oevent_type != Oevent_type_midi_note) continue;
			if (o.midi_note.channel != 4) continue;
			++notes;
			REQUIRE(o.midi_note.velocity >= 7);  // non-zero floor held
			REQUIRE(o.midi_note.velocity <= 47); // idx 1..6 -> 7..47
			vels.insert(o.midi_note.velocity);
		}
	}
	CATCH_INFO("notes = " << notes << " distinct vels = " << vels.size() << "\n" << buf.toOrca());
	REQUIRE(notes >= 36); // 48 bangs minus the eight velocity-0 ticks
	REQUIRE(vels.size() == 6);

	PatternScore s = scorePattern(buf, 16);
	REQUIRE(s.danglingReads == 0);
}

TEST_CASE("Plans attach Operand edges to publishers", "[AhabGenerator]") {
	// §4.1: occasionally a lead or texture carries ONE Operand edge naming
	// the velocity cell; the source must be a pitch publisher (their glyphs
	// never index to velocity 0 — the midi operator's note-off).
	using StoermelderPackOne::Ahab::Edge;
	using StoermelderPackOne::Ahab::VoiceNode;

	Usz seedsWithOperand = 0;
	for (uint32_t seed = 1; seed <= 32; ++seed) {
		AhabGenerator r(seed);
		AhabGenerator::Config warm;
		warm.density = 0.6f;
		warm.qualityGate = false;
		r.generate(24, 40, warm); // primes varPool_ / chanPool_

		std::vector<VoiceNode> const plan = r.planArrangement(24, 40, 0.6f, 3);
		bool hasOperand = false;
		for (size_t i = 0; i < plan.size(); ++i) {
			VoiceNode const& vn = plan[i];
			size_t nOp = 0;
			for (Edge const& e : vn.inputs) {
				if (e.kind != Edge::Operand) continue;
				++nOp;
				hasOperand = true;
				REQUIRE(e.param == Edge::kOpVelocity);
				REQUIRE((plan[e.from].role == VoiceNode::Lead
					|| plan[e.from].role == VoiceNode::Harmony));
			}
			REQUIRE(nOp <= 1); // one velocity cell per voice
		}
		if (hasOperand) ++seedsWithOperand;
	}
	CATCH_INFO("seeds with operand edges: " << seedsWithOperand << "/32");
	REQUIRE(seedsWithOperand >= 4); // occasional but not rare
}

TEST_CASE("K rows land in generated fields and vary velocity", "[AhabGenerator][gate]") {
	// §4.1 done-when: fields whose plan carried an Operand edge contain a
	// 'K' (emitted nowhere else in AhabGenerator) and stay healthy —
	// dangling-read free, still sounding, with more than one velocity in
	// the note stream.
	Usz fieldsWithK = 0;
	for (uint32_t seed = 1; seed <= 32; ++seed) {
		AhabGenerator r(seed);
		AhabGenerator::Config cfg;
		cfg.density = 0.6f;
		ScratchPad buf = r.generate(24, 40, cfg);
		std::string const orca = buf.toOrca();

		PatternScore s = scorePattern(buf, 64);
		REQUIRE(s.danglingReads == 0);
		REQUIRE(s.noteEvents > 0);
		if (orca.find('K') == std::string::npos) continue;

		++fieldsWithK;
		AhabSim sim;
		sim.setFieldSizeRequest(buf.height(), buf.width(), false);
		sim.process();
		sim.setRandomSeed(1);
		Usz oh = 0, ow = 0;
		REQUIRE(sim.loadRectFromOrcaRequest(orca, 0, 0, oh, ow, false));
		sim.process();

		std::set<int> vels;
		for (int t = 0; t < 64; ++t) {
			sim.stepRequest();
			sim.process();
			Oevent_list const* ev = sim.getEvents();
			for (Usz i = 0; i < ev->count; ++i)
				if (ev->buffer[i].any.oevent_type == Oevent_type_midi_note)
					vels.insert(ev->buffer[i].midi_note.velocity);
		}
		CATCH_INFO("seed = " << seed << " distinct vels = " << vels.size() << "\n" << orca);
		REQUIRE(vels.size() >= 2);
	}
	CATCH_INFO("fields with K rows: " << fieldsWithK << "/32");
	REQUIRE(fieldsWithK >= 1); // the capability is reachable, not dead code
}

TEST_CASE("Generated patterns have zero dangling reads", "[AhabGenerator][gate]") {
	// By construction every voice writes its variable above its read, and
	// bus writers sit above bus readers. This pins that invariant across
	// seeds for BOTH paths and both gate modes.
	for (uint32_t seed = 1; seed <= 16; ++seed) {
		AhabGenerator r(seed);
		AhabGenerator::Config cfg;
		cfg.density = 0.6f;
		for (int gate = 0; gate < 2; ++gate) {
			cfg.qualityGate = gate == 1;
			ScratchPad buf = r.generate(24, 40, cfg);
			PatternScore s = scorePattern(buf, 8);
			CATCH_INFO("seed = " << seed << " gate = " << gate
				<< " written = " << s.varsWritten
				<< " read = " << s.varsRead << "\n" << buf.toOrca());
			REQUIRE(s.danglingReads == 0);
		}
	}
}

TEST_CASE("Derived voice transposes the lead pitch", "[AhabGenerator]") {
	// a writer publishes 'C' into var p; the derived voice reads it
	// and adds 2 letter-steps (C -> E in base36 note space), on its own
	// channel, banged by its own D.
	ScratchPad buf(6, 12);
	buf.set(0, 0, 'p'); buf.set(0, 1, 'V'); buf.set(0, 2, 'C'); // lead publish

	AhabGenerator rr(31);
	AhabGenerator::Extent e = rr.placeDerivedVoice(buf, 1, 0, 5, 12, '3', 'p', false, '2');
	REQUIRE(e.h == 3);
	REQUIRE(e.w == 8);

	AhabSim sim;
	sim.setFieldSizeRequest(buf.height(), buf.width(), false);
	sim.process();
	sim.setRandomSeed(1);
	Usz oh = 0, ow = 0;
	REQUIRE(sim.loadRectFromOrcaRequest(buf.toOrca(), 0, 0, oh, ow, false));
	sim.process();

	// index_of('C') = 12; +2 -> glyph_table[14] = 'E' -> pitch class 4.
	std::set<int> harmPcs;
	for (int t = 0; t < 32; ++t) {
		sim.stepRequest();
		sim.process();
		Oevent_list const* ev = sim.getEvents();
		for (Usz i = 0; i < ev->count; ++i) {
			Oevent const& o = ev->buffer[i];
			if (o.any.oevent_type == Oevent_type_midi_note && o.midi_note.channel == 3) {
				harmPcs.insert((o.midi_note.octave * 12 + o.midi_note.note) % 12);
			}
		}
	}
	CATCH_INFO("harmony pitch classes: " << harmPcs.size());
	REQUIRE(harmPcs.size() == 1); // constant input -> constant harmony
	// glyph_table[14] is LOWERCASE 'e' (sharp) -> F -> pc 5;
	// the relationship is deterministic either way.
	REQUIRE(*harmPcs.begin() == 5);
}

TEST_CASE("Derived voice gates on the lead pitch", "[AhabGenerator][gate]") {
	// 'F' pokes '*' only when lead pitch == param: a matching param fires,
	// a non-matching one stays silent on the gated channel.
	auto runGate = [](char gateParam) {
		ScratchPad buf(6, 14);
		buf.set(0, 0, 'p'); buf.set(0, 1, 'V'); buf.set(0, 2, 'C');
		AhabGenerator rr(9);
		AhabGenerator::Extent e = rr.placeDerivedVoice(buf, 1, 0, 5, 14, '2', 'p', true, gateParam);
		REQUIRE(e.h == 3);

		AhabSim sim;
		sim.setFieldSizeRequest(buf.height(), buf.width(), false);
		sim.process();
		sim.setRandomSeed(1);
		Usz oh = 0, ow = 0;
		REQUIRE(sim.loadRectFromOrcaRequest(buf.toOrca(), 0, 0, oh, ow, false));
		sim.process();

		Usz notes = 0;
		for (int t = 0; t < 32; ++t) {
			sim.stepRequest();
			sim.process();
			Oevent_list const* ev = sim.getEvents();
			for (Usz i = 0; i < ev->count; ++i) {
				if (ev->buffer[i].any.oevent_type == Oevent_type_midi_note
					&& ev->buffer[i].midi_note.channel == 2) {
					++notes;
				}
			}
		}
		return notes;
	};

	CHECK(runGate('C') > 0); // lead is a constant C: matches -> fires
	CHECK(runGate('D') == 0); // never matches -> silent
}

TEST_CASE("Fan-in gate sounds only when both publishers match", "[AhabGenerator][gate]") {
	// the second Trigger edge must actually
	// reach the emitted field. The lead publishes 'C', the second publisher
	// 'E'; the gated channel may only sound when param == 'C' AND
	// paramB == 'E'. A dropped edge (the old bug) would fire on any param.
	auto runFanGate = [](char param, char paramB) {
		ScratchPad buf(10, 16);
		buf.set(0, 0, 'p'); buf.set(0, 1, 'V'); buf.set(0, 2, 'C');
		buf.set(0, 4, 'q'); buf.set(0, 5, 'V'); buf.set(0, 6, 'E');
		AhabGenerator rr(9);
		AhabGenerator::Extent e = rr.placeDerivedVoice(buf, 1, 0, 9, 16,
			'2', 'p', true, param, nullptr, 'q', paramB);
		REQUIRE(e.h == 7);
		REQUIRE(e.w == 12);

		AhabSim sim;
		sim.setFieldSizeRequest(buf.height(), buf.width(), false);
		sim.process();
		sim.setRandomSeed(1);
		Usz oh = 0, ow = 0;
		REQUIRE(sim.loadRectFromOrcaRequest(buf.toOrca(), 0, 0, oh, ow, false));
		sim.process();

		Usz notes = 0;
		for (int t = 0; t < 32; ++t) {
			sim.stepRequest();
			sim.process();
			Oevent_list const* ev = sim.getEvents();
			for (Usz i = 0; i < ev->count; ++i) {
				if (ev->buffer[i].any.oevent_type == Oevent_type_midi_note
					&& ev->buffer[i].midi_note.channel == 2) {
					++notes;
				}
			}
		}
		return notes;
	};

	CHECK(runFanGate('C', 'E') > 0); // both match -> fires
	CHECK(runFanGate('D', 'E') == 0); // lead misses -> silent
	CHECK(runFanGate('C', 'D') == 0); // second publisher misses -> silent
	CHECK(runFanGate('D', 'D') == 0); // neither matches -> silent
}

TEST_CASE("Arrangements derive from leads without dangling reads", "[AhabGenerator][gate]") {
	// some arrangements place derived voices, and the
	// dangling-read invariant holds for them everywhere.
	Usz withDerived = 0;
	for (uint32_t seed = 1; seed <= 16; ++seed) {
		AhabGenerator r(seed);
		AhabGenerator::Config cfg;
		cfg.density = 0.6f;
		cfg.qualityGate = false;
		ScratchPad buf = r.generate(24, 40, cfg);
		PatternScore s = scorePattern(buf, 8);
		CATCH_INFO("seed = " << seed << " derived = " << r.getDerivedPlaced()
			<< " dangling = " << s.danglingReads);
		REQUIRE(s.danglingReads == 0);
		withDerived += r.getDerivedPlaced() > 0 ? 1 : 0;
	}
	REQUIRE(withDerived >= 4); // most seeds couple at least one voice to a lead
}

TEST_CASE("Chord voices sound all notes simultaneously", "[AhabGenerator]") {
	// the old single-delay fan-out connected only
	// the FIRST chord note to the bang - notes 2..N never fired. The fixed
	// layout stacks N delay+MIDI pairs with identical rate/mod, so all three
	// pitches arrive on the same tick.
	ScratchPad buf(8, 12);
	AhabGenerator rr(13);
	AhabGenerator::Extent e = rr.placeChordVoice(buf, 0, 0, 8, 12, '1', '2', '5', '4',
												  std::vector<char>({'C', 'E', 'G'}));
	REQUIRE(e.h == 6); // 3 notes x 2 rows
	REQUIRE(e.w == 9);

	AhabSim sim;
	sim.setFieldSizeRequest(buf.height(), buf.width(), false);
	sim.process();
	sim.setRandomSeed(1);
	Usz oh = 0, ow = 0;
	REQUIRE(sim.loadRectFromOrcaRequest(buf.toOrca(), 0, 0, oh, ow, false));
	sim.process();

	// rate 1 * mod 2: the delays fire on every second tick.
	std::set<int> distinct;
	Usz bestTogether = 0;
	for (Usz t = 0; t < 8; ++t) {
		sim.stepRequest();
		sim.process();
		Oevent_list const* ev = sim.getEvents();
		std::set<int> thisTick;
		for (Usz i = 0; i < ev->count; ++i) {
			Oevent const& o = ev->buffer[i];
			if (o.any.oevent_type == Oevent_type_midi_note && o.midi_note.channel == 5) {
				distinct.insert((o.midi_note.octave * 12 + o.midi_note.note) % 12);
				thisTick.insert((o.midi_note.octave * 12 + o.midi_note.note) % 12);
			}
		}
		bestTogether = std::max(bestTogether, thisTick.size());
	}

	CATCH_INFO("distinct = " << distinct.size() << " bestTogether = " << bestTogether);
	REQUIRE(distinct.size() == 3);      // C=0, E=4, G=7: ALL notes sound...
	REQUIRE(bestTogether == 3);         // ...and at least once SIMULTANEOUSLY
}

TEST_CASE("Sparse settings spread voices across the selection", "[AhabGenerator]") {
	// Density buys breathing room as well as count (user report: a Sparse
	// take clustered its few voices in one corner). Same seed, two densities:
	// the Sparse take must cover a comparable span with much less content
	// inside that span — the blocks keep their distance instead of clumping,
	// while Packed fills its box edge to edge.
	auto boxFill = [](float density, uint32_t seed) {
		AhabGenerator r(seed);
		AhabGenerator::Config cfg;
		cfg.density = density;
		cfg.qualityGate = false;
		ScratchPad buf = r.generate(24, 40, cfg);

		Usz minRow = 99, maxRow = 0, minCol = 99, maxCol = 0, glyphs = 0;
		for (Usz y = 0; y < buf.height(); ++y) {
			for (Usz x = 0; x < buf.width(); ++x) {
				if (buf.get(y, x) == '.') continue;
				++glyphs;
				minRow = std::min(minRow, y);
				maxRow = std::max(maxRow, y);
				minCol = std::min(minCol, x);
				maxCol = std::max(maxCol, x);
			}
		}
		if (glyphs == 0) return 1.f; // degenerate: maximally "sparse"
		float const spanRows = (float)(maxRow - minRow + 1);
		float const spanCols = (float)(maxCol - minCol + 1);
		CATCH_INFO("seed = " << seed << " density = " << density
			<< " span = " << spanRows << "x" << spanCols
			<< " glyphs = " << glyphs);
		return (float)glyphs / (spanRows * spanCols);
	};

	for (uint32_t seed = 1; seed <= 8; ++seed) {
		float const sparseFill = boxFill(0.2f, seed);
		float const packedFill = boxFill(1.0f, seed);
		CATCH_INFO("seed = " << seed << " sparseFill = " << sparseFill
			<< " packedFill = " << packedFill);
		REQUIRE(sparseFill < packedFill); // sparse keeps distance between blocks
	}
}

TEST_CASE("Large scratchpads are filled proportionally", "[AhabGenerator]") {
	// User-reported regression: capacity was hard-capped at 12 voices, so big
	// selections used only a small corner. Area-scaled capacity must fill a
	// proportional share of the rect at density 1.
	for (uint32_t seed = 1; seed <= 3; ++seed) {
		AhabGenerator r(seed);
		AhabGenerator::Config cfg;
		cfg.density = 1.0f;
		cfg.qualityGate = false;
		ScratchPad buf = r.generate(48, 96, cfg);

		Usz lastRow = 0, lastCol = 0, glyphs = 0;
		for (Usz y = 0; y < buf.height(); ++y) {
			for (Usz x = 0; x < buf.width(); ++x) {
				if (buf.get(y, x) != '.') {
					lastRow = std::max(lastRow, y);
					lastCol = std::max(lastCol, x);
					++glyphs;
				}
			}
		}
		CATCH_INFO("seed = " << seed << " lastRow = " << lastRow
			<< " lastCol = " << lastCol << " glyphs = " << glyphs);
		REQUIRE(lastRow >= 24);   // vertical span >= half the 48-row height
		REQUIRE(lastCol >= 48);   // horizontal span >= half the 96-col width
		REQUIRE(glyphs >= 200);   // a real amount of content, not a corner
	}
}

TEST_CASE("Mid-size selections fill without large empty swaths", "[AhabGenerator]") {
	// User-reported regression, second half: even where capacity sufficed,
	// the layout abandoned row remainders when dependents needed deeper
	// bands and added random gaps on every wrap — leaving most of a Medium/
	// Very-dense selection blank. Band packing with deferral must keep the
	// whole vertical span in use: nearly every row carries content, and a
	// healthy share of the cells is written.
	for (uint32_t seed = 1; seed <= 8; ++seed) {
		for (float density : {0.3f, 0.5f}) {
			AhabGenerator r(seed);
			AhabGenerator::Config cfg;
			cfg.density = density;
			cfg.qualityGate = false;
			ScratchPad buf = r.generate(24, 40, cfg);

			Usz lastRow = 0, glyphs = 0, rowsWithContent = 0;
			for (Usz y = 0; y < buf.height(); ++y) {
				bool any = false;
				for (Usz x = 0; x < buf.width(); ++x) {
					if (buf.get(y, x) != '.') { any = true; ++glyphs; }
				}
				if (any) { ++rowsWithContent; lastRow = std::max(lastRow, y); }
			}
			CATCH_INFO("seed = " << seed << " density = " << density
				<< " lastRow = " << lastRow << " rows = " << rowsWithContent
				<< " glyphs = " << glyphs);
			// Medium fills more gently than Very-dense by design; both must
			// use the vertical span instead of stacking everything up top.
			// Floors sit well below the measured distribution (the old
			// layout stranded selections in the top half: lastRow ~10-12).
			Usz const minLastRow = density == 0.5f ? 16 : 13;
			Usz const minRows = density == 0.5f ? 13 : 11;
			Usz const minGlyphs = density == 0.5f ? 120 : 90;
			REQUIRE(lastRow >= minLastRow);       // span reaches the bottom third
			REQUIRE(rowsWithContent >= minRows);  // few fully empty rows of 24
			REQUIRE(glyphs >= minGlyphs);
		}
	}
}

TEST_CASE("getDerivedPlaced counts only the returned pattern", "[AhabGenerator][gate]") {
	// derivedPlaced_ used to
	// accumulate across quality-gate retry attempts, describing discarded
	// patterns as well as the returned one. After the fix it must describe
	// exactly the RETURNED pattern: a gate-on generation whose adopted seed
	// differs from the constructor seed (i.e. a retry was adopted) must
	// report the same count as reproducing that adopted attempt with the
	// gate off (same final seed => identical RNG stream => identical plan).
	Usz retryCases = 0;
	for (uint32_t seed = 1; seed <= 64 && retryCases < 4; ++seed) {
		AhabGenerator r(seed);
		AhabGenerator::Config cfg;
		cfg.density = 0.6f;
		cfg.qualityGate = true;
		r.generate(24, 40, cfg);
		if (r.getSeed() == seed) continue; // no retry was adopted
		++retryCases;

		uint32_t const adopted = r.getSeed();
		int const gateOnCount = r.getDerivedPlaced();

		// Reproduce exactly the adopted attempt: a fresh instance seeded with
		// the adopted seed, gate off, single pass.
		AhabGenerator repro(adopted);
		AhabGenerator::Config raw;
		raw.density = 0.6f;
		raw.qualityGate = false;
		ScratchPad buf = repro.generate(24, 40, raw);
		REQUIRE(repro.getSeed() == adopted);

		CATCH_INFO("seed = " << seed << " adopted = " << adopted
			<< " gateOn = " << gateOnCount
			<< " raw = " << repro.getDerivedPlaced()
			<< "\n" << buf.toOrca());
		REQUIRE(gateOnCount == repro.getDerivedPlaced());
	}
	REQUIRE(retryCases >= 1); // the sweep must actually exercise retries
}

TEST_CASE("Planned graph mirrors the legacy plan shape", "[AhabGenerator]") {
	// with a bus present, node 0 is the bus;
	// leads carry at most one Clock edge; Harmony/Gate carry exactly one
	// Pitch/Trigger edge pointing at a Lead; textures carry none. Pinning
	// this now gives topological layout a fixed starting shape.
	using StoermelderPackOne::Ahab::Edge;
	using StoermelderPackOne::Ahab::VoiceNode;

	for (uint32_t seed = 1; seed <= 16; ++seed) {
		AhabGenerator r(seed);
		AhabGenerator::Config warm;
		warm.density = 0.6f;
		warm.qualityGate = false;
		r.generate(24, 40, warm); // primes chanPool_/varPool_ (generateOnce prologue)
		std::vector<VoiceNode> const plan = r.planArrangement(24, 40, 0.6f, 3);

		CATCH_INFO("seed = " << seed << " nodes = " << plan.size());
		REQUIRE(plan.size() >= 4); // bus + lead(s) + drums + texture
		REQUIRE(plan[0].role == VoiceNode::Bus);

		size_t leads = 0, textures = 0;
		for (size_t i = 0; i < plan.size(); ++i) {
			VoiceNode const& vn = plan[i];
			switch (vn.role) {
				case VoiceNode::Bus:
					REQUIRE(i == 0);
					REQUIRE(vn.inputs.empty());
					break;
				case VoiceNode::Lead:
					// A lead may additionally carry ONE Operand edge
					// (its velocity follows another publisher's pitch via K).
					REQUIRE(vn.inputs.size() <= 2);
					for (Edge const& e : vn.inputs) {
						if (e.kind == Edge::Clock) {
							REQUIRE(e.from == 0);
						} else {
							REQUIRE(e.kind == Edge::Operand);
							REQUIRE(e.param == Edge::kOpVelocity);
							REQUIRE(plan[e.from].role != VoiceNode::Bus);
						}
					}
					++leads;
					break;
				case VoiceNode::Harmony:
				case VoiceNode::Gate:
					// Step 6 fan-in: a Gate may carry a SECOND Trigger edge
					// gating it on another publisher. Vocabulary Step 2b: any
					// Pitch-carrying voice may additionally carry ONE
					// Modulation edge (a CC tail on its own Pitch source).
					REQUIRE(vn.inputs.size() >= 1);
					REQUIRE(vn.inputs.size() <= 3);
					for (Edge const& e : vn.inputs) {
						REQUIRE(e.from < plan.size());
						REQUIRE(e.from != i);
						REQUIRE((plan[e.from].role == VoiceNode::Lead
							|| plan[e.from].role == VoiceNode::Harmony));
					}
					if (vn.role == VoiceNode::Harmony)
						REQUIRE(vn.inputs[0].kind == Edge::Pitch);
					else if (vn.inputs.size() >= 2)
						REQUIRE(vn.inputs[0].kind == Edge::Pitch); // fan-in Gate
					else
						REQUIRE(vn.inputs[0].kind == Edge::Trigger);
					// Modulation tails ride the SAME producer as the Pitch
					// edge and draw low control numbers from the per-call
					// pool ('0'..'3'), uniquely per plan.
					{
						size_t nMod = 0;
						std::set<char> controls;
						for (Edge const& e : vn.inputs) {
							if (e.kind != Edge::Modulation) continue;
							++nMod;
							REQUIRE(e.from == vn.inputs[0].from);
							REQUIRE(e.param >= '0');
							REQUIRE(e.param <= '3');
							REQUIRE(controls.insert(e.param).second);
						}
						REQUIRE(nMod <= 1);
					}
					break;
				default: // textures
					// A texture voice may carry one Operand edge.
					REQUIRE(vn.inputs.size() <= 1);
					for (Edge const& e : vn.inputs) {
						REQUIRE(e.kind == Edge::Operand);
						REQUIRE(e.param == Edge::kOpVelocity);
						REQUIRE((plan[e.from].role == VoiceNode::Lead
							|| plan[e.from].role == VoiceNode::Harmony));
					}
					++textures;
					break;
			}
		}

		// chanPool_ hands out unique channels; two melodic voices
		// sharing one cut each other off (note-off race). The pool
		// holds only 4 channels ('0'- '3'), so uniqueness is
		// guaranteed just for the first four melodic voices - after
		// that randomChannel() legitimately falls back to random.
		std::set<char> firstFour;
		Usz melodic = 0;
		for (VoiceNode const& vn : plan) {
			if (vn.role == VoiceNode::Lead || vn.role == VoiceNode::Harmony
					|| vn.role == VoiceNode::Gate) {
				if (melodic < 4)
					REQUIRE(firstFour.insert(vn.channel).second);
				++melodic;
			}
		}
		REQUIRE(leads >= 1);
		REQUIRE(textures >= 1);
	}

	// Without a bus there is no node 0; leads are roots except for the
	// occasional Operand edge from an earlier publisher.
	AhabGenerator r(5);
	std::vector<VoiceNode> const plan = r.planArrangement(24, 40, 0.6f, 0);
	REQUIRE(plan[0].role != VoiceNode::Bus);
	for (VoiceNode const& vn : plan) {
		if (vn.role != VoiceNode::Lead) continue;
		for (Edge const& e : vn.inputs)
			REQUIRE(e.kind == Edge::Operand);
	}
}

TEST_CASE("Topological layout: every reader sits below its sources", "[AhabGenerator]") {
	// the invariant the three zones used to enforce by
	// convention is now structural. For every variable READ in the field,
	// some WRITE of the same variable must exist on a strictly earlier row
	// (vars are within-tick and evaluated top-to-bottom, review 7.1).
	auto isVarName = [](char c) {
		return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
	};
	for (uint32_t seed = 1; seed <= 32; ++seed) {
		AhabGenerator r(seed);
		AhabGenerator::Config cfg;
		cfg.density = 0.6f;
		cfg.qualityGate = false;
		ScratchPad buf = r.generate(32, 48, cfg);

		Usz reads = 0;
		for (Usz y = 0; y < buf.height(); ++y) {
			for (Usz x = 0; x < buf.width(); ++x) {
				if (buf.get(y, x) != 'V') continue;
				Glyph const left = x > 0 ? buf.get(y, x - 1) : '.';
				Glyph const right = buf.get(y, x + 1);
				if (left != '.') continue;          // write mode, not a read
				if (!isVarName(right)) continue;
				++reads;
				// The write must sit ABOVE the reader, or on the SAME ROW to
				// its left: ORCA scans top-to-bottom, left-to-right, so both
				// are visible. The bus shares the top band with the voices
				// that read it, which is exactly the same-row case.
				bool foundVisible = false;
				for (Usz wy = 0; wy < y && !foundVisible; ++wy) {
					for (Usz wx = 0; wx + 1 < buf.width() && !foundVisible; ++wx) {
						if (buf.get(wy, wx) == right && buf.get(wy, wx + 1) == 'V') {
							foundVisible = true;
						}
					}
				}
				for (Usz wx = 0; wx + 1 < x && !foundVisible; ++wx) {
					if (buf.get(y, wx) == right && buf.get(y, wx + 1) == 'V') {
						foundVisible = true;
					}
				}
				CATCH_INFO("seed = " << seed << " read of '" << right << "' at row "
					<< y << " col " << x << "\n" << buf.toOrca());
				REQUIRE(foundVisible);
			}
		}
		REQUIRE(reads >= 1); // the arrangement must actually couple voices
	}
}

TEST_CASE("Derived voices publish a readable pitch variable", "[AhabGenerator]") {
	// a publishing harmony (channel 3) transposes the lead
	// C by +2 letter-steps and republishes the result into its own variable;
	// a second derived voice (channel 4) reads THAT variable and transposes
	// by another +2. Both must sound, and the whole field must be free of
	// dangling reads.
	ScratchPad buf(12, 16);
	buf.set(0, 0, 'p'); buf.set(0, 1, 'V'); buf.set(0, 2, 'C'); // lead publish
	char v1 = 0;

	AhabGenerator rr(77);
	AhabGenerator::Config warm;
	warm.density = 0.5f;
	warm.qualityGate = false;
	rr.generate(12, 20, warm); // primes varPool_ (filled by generateOnce prologue)

	AhabGenerator::Extent e1 = rr.placeDerivedVoice(buf, 1, 0, 6, 16, '3', 'p', false, '2', &v1);
	REQUIRE(e1.h == 5); // publishing harmony is 5 rows
	REQUIRE(v1 != 0);

	AhabGenerator::Extent e2 = rr.placeDerivedVoice(buf, 6, 0, 6, 16, '4', v1, false, '2');
	REQUIRE(e2.h == 3); // second voice does not request publication

	AhabSim sim;
	sim.setFieldSizeRequest(buf.height(), buf.width(), false);
	sim.process();
	sim.setRandomSeed(1);
	Usz oh = 0, ow = 0;
	REQUIRE(sim.loadRectFromOrcaRequest(buf.toOrca(), 0, 0, oh, ow, false));
	sim.process();

	std::set<int> harmPcs, chainPcs;
	for (int t = 0; t < 32; ++t) {
		sim.stepRequest();
		sim.process();
		Oevent_list const* ev = sim.getEvents();
		for (Usz i = 0; i < ev->count; ++i) {
			Oevent const& o = ev->buffer[i];
			if (o.any.oevent_type == Oevent_type_midi_note && o.midi_note.channel == 3) {
				harmPcs.insert((o.midi_note.octave * 12 + o.midi_note.note) % 12);
			}
			if (o.any.oevent_type == Oevent_type_midi_note && o.midi_note.channel == 4) {
				chainPcs.insert((o.midi_note.octave * 12 + o.midi_note.note) % 12);
			}
		}
	}
	CATCH_INFO("harm = " << harmPcs.size() << " chain = " << chainPcs.size()
		<< "\n" << buf.toOrca());
	REQUIRE(harmPcs.size() == 1);
	REQUIRE(*harmPcs.begin() == 5);  // C +2 letter-steps -> glyph_table 'e' -> F
	REQUIRE(chainPcs.size() == 1);   // E-letter +2 -> 'g' -> G#
	REQUIRE(*chainPcs.begin() == 8);

	PatternScore s = scorePattern(buf, 8);
	REQUIRE(s.danglingReads == 0);   // publish write sits above the chain read
}

TEST_CASE("Coupling chains reach depth > 2", "[AhabGenerator]") {
	// the thing two zones structurally could not do.
	// Chain depth counts Pitch/Trigger edges (Clock edges are timing, not
	// pitch inheritance): Lead=1, its harmony=2, a harmony on that harmony=3.
	// planArrangement consumes instance RNG and needs a primed variable
	// pool, so each seed warms up with one throwaway generation first.
	Usz deepestOverall = 0;
	Usz seedsWithChain = 0;
	for (uint32_t seed = 1; seed <= 32; ++seed) {
		AhabGenerator r(seed);
		AhabGenerator::Config warm;
		warm.density = 0.6f;
		warm.qualityGate = false;
		r.generate(24, 40, warm); // fills varPool_ / exercises the instance

		std::vector<VoiceNode> const plan = r.planArrangement(24, 40, 0.6f, 3);
		std::vector<Usz> depth(plan.size(), 0);
		Usz deepest = 0;
		for (size_t i = 0; i < plan.size(); ++i) {
			depth[i] = 1;
			for (Edge const& e : plan[i].inputs) {
				if (e.kind == Edge::Pitch || e.kind == Edge::Trigger) {
					depth[i] = std::max(depth[i], depth[e.from] + 1);
				}
			}
			deepest = std::max(deepest, depth[i]);
		}
		CATCH_INFO("seed = " << seed << " deepest = " << deepest
			<< " nodes = " << plan.size());
		if (deepest >= 3) ++seedsWithChain;
		deepestOverall = std::max(deepestOverall, deepest);
	}
	REQUIRE(deepestOverall >= 3);   // depth-3 chains exist at all...
	REQUIRE(seedsWithChain >= 4);   // ...across most seeds, not a fluke
}

TEST_CASE("Fan-in: some Gates gate on a second publisher", "[AhabGenerator]") {
	// occasionally a round-1 Gate carries TWO inbound
	// edges - Pitch from its note source, Trigger from a DIFFERENT
	// publisher - and the dangling-read invariant still holds for the
	// resulting field.
	Usz seedsWithFanIn = 0;
	for (uint32_t seed = 1; seed <= 32; ++seed) {
		AhabGenerator r(seed);
		AhabGenerator::Config warm;
		warm.density = 0.6f;
		warm.qualityGate = false;
		ScratchPad buf = r.generate(24, 40, warm); // primes varPool_

		std::vector<VoiceNode> const plan = r.planArrangement(24, 40, 0.6f, 3);
		bool fanInHere = false;
		for (size_t i = 0; i < plan.size(); ++i) {
			VoiceNode const& vn = plan[i];
			// Fan-in = a SECOND PITCH SOURCE: a Trigger edge alongside a
			// Pitch edge. A plain Gate carries a lone Trigger (no comparison),
			// and a Modulation tail shares the Pitch producer by
			// construction, so neither enters the distinct-source check.
			size_t trigIdx = vn.inputs.size();
			bool hasPitch = false;
			for (size_t k = 0; k < vn.inputs.size(); ++k) {
				if (vn.inputs[k].kind == Edge::Trigger) trigIdx = k;
				if (vn.inputs[k].kind == Edge::Pitch) hasPitch = true;
			}
			if (trigIdx == vn.inputs.size() || !hasPitch) continue;
			fanInHere = true;
			REQUIRE(vn.inputs[0].from != vn.inputs[trigIdx].from);
			for (Edge const& e : vn.inputs) {
				REQUIRE(e.from < plan.size());
				REQUIRE((plan[e.from].role == VoiceNode::Lead
					|| plan[e.from].role == VoiceNode::Harmony));
			}
		}
		if (fanInHere) ++seedsWithFanIn;

		PatternScore s = scorePattern(buf, 8);
		REQUIRE(s.danglingReads == 0);
	}
	CATCH_INFO("seeds with fan-in: " << seedsWithFanIn << "/32");
	REQUIRE(seedsWithFanIn >= 4); // fan-in is occasional but not rare
}

TEST_CASE("Fan-in gates are emitted into generated fields", "[AhabGenerator][gate]") {
	// the plan promising a second Trigger edge is not
	// enough - the 7x12 merge layout must actually fit and land in real
	// arrangements. 'J' and 'L' are emitted nowhere else, so their presence
	// fingerprints a placed fan-in gate.
	Usz fieldsWithFanIn = 0;
	for (uint32_t seed = 1; seed <= 32; ++seed) {
		AhabGenerator r(seed);
		AhabGenerator::Config cfg;
		cfg.density = 0.6f;
		cfg.qualityGate = false;
		ScratchPad buf = r.generate(24, 40, cfg);
		std::string const orca = buf.toOrca();
		if (orca.find('J') != std::string::npos && orca.find('L') != std::string::npos) {
			++fieldsWithFanIn;
		}
		PatternScore ps = scorePattern(buf, 8);
		REQUIRE(ps.danglingReads == 0);
	}
	CATCH_INFO("fields with placed fan-in: " << fieldsWithFanIn << "/32");
	REQUIRE(fieldsWithFanIn >= 1); // the capability is reachable, not dead
}