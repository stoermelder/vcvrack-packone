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
