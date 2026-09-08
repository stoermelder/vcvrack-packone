#include "../../test/framework.hpp"

#include "FlowerSeq.cpp"
#include "FlowerSeqEx.cpp"
#include "FlowerSeqTrig.cpp"

using namespace StoermelderPackOne::Flower;

SYNC_MODEL(modelFlowerSeq, "FlowerSeq");
SYNC_MODEL(modelFlowerSeqEx, "FlowerSeqEx");
SYNC_MODEL(modelFlowerSeqTrig, "FlowerSeqTrig");
Test::TestContext<> testContext;

typedef FlowerSeqModule<16, 8, 8> MasterModule;
typedef FlowerSeqExModule<16, 8, 8> OffspringModule;
typedef FlowerTrigModule<16, 8, 8> SeedsModule;

// FlowerSeqModule must give leftExpander and rightExpander independent FlowerProcessArgs
// buffer pairs. Rack's Expander contract (and Engine.cpp, which flips each side
// independently) assumes every module hands out one distinct producer/consumer pair per side.
// Aliasing them together would mean the two sides are never actually double-buffered from each
// other: swapping the master's messageFlipRequested for one side would silently swap the roles
// of the exact same two objects the other side is also holding pointers into.
TEST_CASE("FlowerSeqModule expander buffers", "[Flower][B1]") {
	Test::ModuleScaffold<MasterModule> mods;
	MasterModule* m = mods.create("FlowerSeq");

	SECTION("left and right side use independent buffer objects") {
		// Rack's Expander docs: "allocate both message buffers with identical blocks of
		// memory" — one distinct pair per side. A shared pair on FlowerSeqModule means the
		// left and right Expander structs are never truly independent: flipping one is
		// indistinguishable, at the pointer level, from flipping the other, since both
		// structs are perpetually aliased onto the same two underlying objects.
		CHECK(m->leftExpander.producerMessage != m->rightExpander.producerMessage);
		CHECK(m->leftExpander.consumerMessage != m->rightExpander.consumerMessage);
	}

	SECTION("flipping only the left side must not change what the right side's pointers reference") {
		void* rightProducerBefore = m->rightExpander.producerMessage;
		void* rightConsumerBefore = m->rightExpander.consumerMessage;

		// Simulate the engine flipping only the left side for this tick (Engine.cpp flips
		// leftExpander and rightExpander independently based on each side's own
		// messageFlipRequested flag — a module is free to request one without the other,
		// e.g. because only one side has a connected neighbour that round).
		std::swap(m->leftExpander.producerMessage, m->leftExpander.consumerMessage);

		// With independent buffer pairs, the right side is untouched by a left-only flip.
		// If the two sides shared a buffer pair, this would fail: swapping "left" would also
		// swap "right", because both sides' pointers would reference the same two objects.
		CHECK(m->rightExpander.producerMessage == rightProducerBefore);
		CHECK(m->rightExpander.consumerMessage == rightConsumerBefore);
	}
}

// PatternList::next()/prev() must both wrap modulo `last` (the number of currently-active
// patterns), not modulo the list's fixed capacity SIZE. next() has always done this correctly;
// prev() is covered here in matching depth since it is the one that historically got this wrong
// (using `+ SIZE` before the modulus instead of `+ last`), which only gave the right answer when
// last == SIZE and otherwise jumped to an arbitrary slot or failed to move at all for any
// smaller, partially-enabled pattern set.
TEST_CASE("PatternList::next() and prev()", "[Flower][B3]") {
	PatternList list;

	SECTION("prev() and next() are inverses at every list size") {
		// Sweep every achievable `last` (disable() refuses to go below 1) and every starting pos.
		for (int size = 1; size <= PatternList::SIZE; size++) {
			list.reset(size);
			for (int start = 0; start < size; start++) {
				list.setPos(start);
				list.prev();
				int afterPrev = list.pos;
				list.next();
				CHECK(list.pos == start);
				int expectedPrev = (start - 1 + size) % size;
				CHECK(afterPrev == expectedPrev);

				list.setPos(start);
				list.next();
				int afterNext = list.pos;
				list.prev();
				CHECK(list.pos == start);
				int expectedNext = (start + 1) % size;
				CHECK(afterNext == expectedNext);
			}
		}
	}

	SECTION("regression: reduced pattern lists") {
		// | last | pos | prev() gives | correct | next() gives | correct |
		// |    2 |   0 |            0 |       1 |            1 |       1 |
		// |    3 |   0 |            0 |       2 |            1 |       1 |
		// |    5 |   0 |            2 |       4 |            1 |       1 |
		// |    8 |   0 |            4 |       7 |            1 |       1 |
		// next() was already correct for all of these; kept alongside prev() so a future
		// regression in either direction shows up in the same table.
		struct Case { int last, pos, expectedPrev, expectedNext; };
		const Case cases[] = {
			{2, 0, 1, 1},
			{3, 0, 2, 1},
			{5, 0, 4, 1},
			{8, 0, 7, 1},
		};
		for (const Case& c : cases) {
			list.reset(c.last);
			list.setPos(c.pos);
			list.prev();
			CHECK(list.pos == c.expectedPrev);

			list.reset(c.last);
			list.setPos(c.pos);
			list.next();
			CHECK(list.pos == c.expectedNext);
		}
	}

	SECTION("prev() wraps to the last slot from position 0") {
		list.reset(4);
		list.setPos(0);
		list.prev();
		CHECK(list.pos == 3);
	}

	SECTION("next() wraps to position 0 from the last slot") {
		list.reset(4);
		list.setPos(3);
		list.next();
		CHECK(list.pos == 0);
	}

	SECTION("prev() steps back by one when not at the boundary") {
		list.reset(4);
		list.setPos(2);
		list.prev();
		CHECK(list.pos == 1);
	}

	SECTION("next() steps forward by one when not at the boundary") {
		list.reset(4);
		list.setPos(1);
		list.next();
		CHECK(list.pos == 2);
	}

	SECTION("prev() is a no-op on a single-entry list") {
		list.reset(1);
		list.setPos(0);
		list.prev();
		CHECK(list.pos == 0);
	}

	SECTION("next() is a no-op on a single-entry list") {
		list.reset(1);
		list.setPos(0);
		list.next();
		CHECK(list.pos == 0);
	}

	SECTION("prev() after disabling entries respects the reduced last, not SIZE") {
		list.reset(PatternList::SIZE);
		// Disable down to 5 active entries (disable() refuses to go below 1).
		while (list.last > 5) {
			list.disable(list.at(list.last - 1));
		}
		REQUIRE(list.last == 5);
		list.setPos(0);
		list.prev();
		// A `+ SIZE` (13) modulus bug would land pos on (0 - 1 + 13) % 5 == 2 instead of 4.
		CHECK(list.pos == 4);
	}

	SECTION("next() after disabling entries respects the reduced last, not SIZE") {
		list.reset(PatternList::SIZE);
		while (list.last > 5) {
			list.disable(list.at(list.last - 1));
		}
		REQUIRE(list.last == 5);
		list.setPos(4);
		list.next();
		CHECK(list.pos == 0);
	}
}

// FlowerSeqModule::patternCheck() reassigns any phrase pattern whose type has become inactive
// (disabled in patternList) to the next active type. The search must wrap at PATTERN_TYPE::NUM
// and be bounded by the number of pattern types, since patternList always has at least one
// active entry (disable() refuses to go below that) — an unbounded search that just increments
// past NUM walks off the end of PatternList::map into undefined memory instead of wrapping back
// to the low-numbered types.
TEST_CASE("FlowerSeqModule::patternCheck()", "[Flower][B4]") {
	Test::ModuleScaffold<MasterModule> mods;
	MasterModule* m = mods.create("FlowerSeq");

	SECTION("reassigns a disabled type to the next active one") {
		m->patternList.reset();
		m->patternList.disable(PATTERN_TYPE::SEQ_INV);
		m->phrases[0].patterns[0].type = PATTERN_TYPE::SEQ_INV;

		m->patternCheck();

		CHECK(m->patternList.active(m->phrases[0].patterns[0].type));
		CHECK(m->phrases[0].patterns[0].type != PATTERN_TYPE::SEQ_INV);
	}

	SECTION("leaves an already-active type untouched") {
		m->patternList.reset();
		m->phrases[0].patterns[0].type = PATTERN_TYPE::SEQ_FWD;

		m->patternCheck();

		CHECK(m->phrases[0].patterns[0].type == PATTERN_TYPE::SEQ_FWD);
	}

	SECTION("wraps past the highest type instead of walking off the end of the map") {
		// AUX_RAND (12) is the highest PATTERN_TYPE. Disabling it and leaving a phrase pointed
		// at it is exactly the case that used to read PatternList::map[13], map[14], ... — this
		// asserts the search wraps back to a low-numbered active type instead.
		m->patternList.reset();
		m->patternList.disable(PATTERN_TYPE::AUX_RAND);
		m->phrases[0].patterns[0].type = PATTERN_TYPE::AUX_RAND;

		m->patternCheck();

		CHECK(m->patternList.active(m->phrases[0].patterns[0].type));
		CHECK((int)m->phrases[0].patterns[0].type < (int)PATTERN_TYPE::NUM);
	}

	SECTION("checks every phrase and every pattern slot") {
		// MasterModule == FlowerSeqModule<16, 8, 8>: 8 phrases, 8 pattern slots per phrase.
		const int kPhrases = 8;
		const int kPatterns = 8;
		m->patternList.reset();
		m->patternList.disable(PATTERN_TYPE::AUX_RAND);
		for (int i = 0; i < kPhrases; i++) {
			for (int j = 0; j < kPatterns; j++) {
				m->phrases[i].patterns[j].type = PATTERN_TYPE::AUX_RAND;
			}
		}

		m->patternCheck();

		for (int i = 0; i < kPhrases; i++) {
			for (int j = 0; j < kPatterns; j++) {
				CHECK(m->patternList.active(m->phrases[i].patterns[j].type));
			}
		}
	}

	SECTION("reducing to a single active type still terminates and lands on it") {
		m->patternList.reset();
		while (m->patternList.last > 1) {
			m->patternList.disable(m->patternList.at(m->patternList.last - 1));
		}
		REQUIRE(m->patternList.last == 1);
		PATTERN_TYPE onlyActive = m->patternList.at(0);
		m->phrases[0].patterns[0].type = (PATTERN_TYPE)(((int)onlyActive + 1) % (int)PATTERN_TYPE::NUM);

		m->patternCheck();

		CHECK(m->phrases[0].patterns[0].type == onlyActive);
	}
}

// AUX_RAND's sign term picks a second random bit from stepRandomSeqAuxiliary using a shift
// amount derived from stepRandomIndex, an unreduced random::u32(). The shift amount must be
// bounded to a valid bit position before shifting; an unbounded amount both exceeds the
// operand's bit width (undefined behaviour) and, since it's driven by the low bits of a full
// 32-bit random value rather than being confined to [16, 31], loses the intended "pick bit
// (stepRandomIndex % 16) + 16 of stepRandomSeqAuxiliary" semantics entirely.
TEST_CASE("FlowerSeqModule AUX_RAND sign bit selection", "[Flower][B5]") {
	Test::ModuleScaffold<MasterModule> mods;
	MasterModule* m = mods.create("FlowerSeq");

	// Isolate processOutput()'s AUX_RAND arithmetic from every other moving part: no CV input,
	// no step disable/probability/clamp, zero slew (so the output settles in one call), and a
	// known auxiliary voltage so the sign term is the only thing that can move the result.
	m->seq.reset();
	m->seq.outCvMode = OUT_CV_MODE::UNI_3V;
	m->seq.outCvClamp = false;
	m->seq.stepGet(0)->disabled = false;
	m->seq.stepGet(0)->probability = 1.f;
	m->seq.stepGet(0)->slew = 0.f;
	m->seq.stepGet(0)->auxiliary = 1.f;
	m->params[MasterModule::PARAM_STEP + 0].setValue(0.5f);

	FlowerProcessArgs args;
	args.reset();
	args.running = true;
	args.sampleTime = 1.f / 44100.f;
	args.sampleRate = 44100.f;
	args.stepStart = 0;
	args.stepIndex = 0;
	args.stepLength = 16;
	args.patternType = PATTERN_TYPE::AUX_RAND;

	auto settledVoltage = [&]() {
		// stepSlew's shape (0.975, set by seq.reset()) makes it approach the target
		// exponentially rather than linearly, so it needs many samples — not many calls — to
		// fully settle; 4096 comfortably reaches the target to float precision at 44.1kHz.
		float v = 0.f;
		for (int i = 0; i < 4096; i++) {
			m->seq.processOutput(args, false);
			v = m->outputs[MasterModule::OUTPUT_CV].getVoltage();
		}
		return v;
	};

	// stepOutIndex is 0 here, so the first term ((1u << 0) & stepRandomSeqAuxiliary) selects
	// bit 0. Setting stepRandomSeqAuxiliary's bit 0 alone (and no bit in [16, 31]) isolates the
	// "+1" branch: sign == 1, so the output should land on base + auxiliary == 1.5 + 1.0 == 2.5.
	m->seq.stepRandomSeqAuxiliary = 1u << 0;
	m->seq.stepRandomIndex = 0;
	CHECK(settledVoltage() == Catch::Approx(2.5f));

	// Now select a high bit only, with a stepRandomIndex whose low-16 residue points at it:
	// stepRandomIndex % 16 == 5 picks bit (5 + 16) == 21. With bit 0 clear and bit 21 set, only
	// the "-1" branch should fire: sign == -1, output == base - auxiliary == 1.5 - 1.0 == 0.5.
	m->seq.stepRandomSeqAuxiliary = 1u << 21;
	m->seq.stepRandomIndex = 5;
	CHECK(settledVoltage() == Catch::Approx(0.5f));

	// The bug this guards against: stepRandomIndex is an unreduced random::u32(), so a caller
	// can hand in a value whose low-16 residue (16 % 16 == 0, picking bit 16) does not match
	// what a naive "just add 16 and shift" would do on hardware that masks an oversized shift
	// count modulo the operand width — (16 + 16) % 32 == 0, i.e. bit 0, a different bit
	// entirely. Setting only bit 16 isolates that divergence: the fixed code must select bit
	// 16 (sign == -1, output == 0.5), not bit 0 (which would read as sign == +1, output == 2.5,
	// matching the currently-set stepRandomSeqAuxiliary from the previous check).
	m->seq.stepRandomSeqAuxiliary = 1u << 16;
	m->seq.stepRandomIndex = 16;
	CHECK(settledVoltage() == Catch::Approx(0.5f));

	// Neither bit set: both terms are zero, sign == 0, output stays at the unmodified base value.
	m->seq.stepRandomSeqAuxiliary = 0;
	m->seq.stepRandomIndex = 5;
	CHECK(settledVoltage() == Catch::Approx(1.5f));
}

TEST_CASE("FlowerSeqModule chain delivers current tick to both SEEDS and OFFSPRING", "[Flower][B1]") {
	// End-to-end regression: a full SEEDS - FLOWER - OFFSPRING chain, driven by clock pulses,
	// must have both expanders reading the master's current-tick step position off of
	// consistent, non-aliased state.
	Test::Harness h;
	auto seeds = h.addModule<SeedsModule>("FlowerSeqTrig");
	auto master = h.addModule<MasterModule>("FlowerSeq");
	auto offspring = h.addModule<OffspringModule>("FlowerSeqEx");

	h.connectChain(seeds, master, offspring);

	master->running = true;

	h.dspSteps(4);

	for (int tick = 0; tick < 8; tick++) {
		master->inputs[MasterModule::INPUT_CLOCK].setVoltage(10.f);
		h.dspStep();
		master->inputs[MasterModule::INPUT_CLOCK].setVoltage(0.f);
		h.dspSteps(4);

		// Both expanders derive stepOutIndex from the same FlowerProcessArgs tick. With
		// independent, non-aliased buffers they must always agree, on every tick.
		CHECK(seeds->seq.stepOutIndex == offspring->seq.stepOutIndex);
	}
}
