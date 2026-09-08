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

// B1 (Flower_review.md): FlowerSeqModule wires leftExpander and rightExpander to the SAME
// pair of FlowerProcessArgs objects (argsProducer/argsConsumer) instead of giving each side
// its own pair. Rack's Expander contract (and Engine.cpp, which flips each side
// independently) assumes every module hands out one distinct producer/consumer pair per side.
// Aliasing them together means the two sides are never actually double-buffered from each
// other: swapping the master's messageFlipRequested for one side silently swaps the roles of
// the exact same two objects the other side is also holding pointers into.
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
		// With B1's aliasing, this fails: swapping "left" also swaps "right", because both
		// sides' pointers were referencing the very same two FlowerProcessArgs objects.
		CHECK(m->rightExpander.producerMessage == rightProducerBefore);
		CHECK(m->rightExpander.consumerMessage == rightConsumerBefore);
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
		// independent, non-aliased buffers they must always agree; B1's aliasing puts this
		// invariant at risk any tick the engine flips one side without the other.
		CHECK(seeds->seq.stepOutIndex == offspring->seq.stepOutIndex);
	}
}
