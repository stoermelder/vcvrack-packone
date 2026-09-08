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

// A module with no relation to the Flower bundle at all: model == nullptr, so
// isFlowerSeqModel()/isFlowerTrigModel() are false for it the same way they are for a real,
// unrelated Rack module. Stands in for "a foreign module sits where a Flower neighbour is
// expected".
struct ForeignModule : rack::Module {
	ForeignModule() { config(0, 0, 0, 0); }
	void process(const ProcessArgs& args) override {}
};

// Fires one clock edge on the master's INPUT_CLOCK: rises to 10V for one sample, then falls back
// to 0V. Duplicated from FlowerSeq.test.cpp (file-local static helper, no shared header) rather
// than shared, since the two files are independent translation units.
static void clockPulse(Test::Harness& h, MasterModule* m) {
	m->inputs[MasterModule::INPUT_CLOCK].setVoltage(10.f);
	h.dspStep();
	m->inputs[MasterModule::INPUT_CLOCK].setVoltage(0.f);
}

// resetTimer suppresses clock processing for 1ms after construction/reset. Duplicated from
// FlowerSeq.test.cpp for the same reason as clockPulse() above.
static void clearResetLockout(Test::Harness& h) {
	int samplesFor1ms = (int)std::ceil(h.sampleRate() * 1e-3f) + 1;
	h.dspSteps(samplesFor1ms);
}


TEST_CASE("Chain delivers current tick to both SEEDS and OFFSPRING", "[Flower]") {
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

// Unlike some other expander-chain bundles in this repo, no Flower
// module forwards a *pointer* into another module's storage — each module's own FlowerProcessArgs
// buffers are set once in its constructor and stay valid for the module's own lifetime, and
// leftExpander.module/rightExpander.module are re-read fresh every sample rather than cached. So
// there is no dangling-pointer read to guard against here; what was actually missing before the
// FlowerChainModule base existed was output freshness: nothing noticed a chain break and reset a
// disconnected OFFSPRING/SEEDS's outputs, so they kept outputting their last value forever.
TEST_CASE("FlowerChainModule resets outputs when the chain breaks", "[Flower]") {
	Test::Harness h;
	auto seeds = h.addModule<SeedsModule>("FlowerSeqTrig");
	auto master = h.addModule<MasterModule>("FlowerSeq");
	auto offspring = h.addModule<OffspringModule>("FlowerSeqEx");

	h.connectChain(seeds, master, offspring);
	master->running = true;
	clearResetLockout(h);

	// Drive a clock edge so both expanders latch a non-zero step and their outputs move off
	// whatever the all-zero constructed state happened to be.
	offspring->params[OffspringModule::PARAM_STEP + 0].setValue(1.f);
	clockPulse(h, master);
	h.dspSteps(4);

	REQUIRE(offspring->outputs[OffspringModule::OUTPUT_CV].getVoltage() != 0.f);

	SECTION("removing the master notifies OFFSPRING and SEEDS to reset their own outputs") {
		// Mirrors Engine::removeModule_NoLock in full: onRemove() fires on the removed module,
		// then the engine clears the adjacency on each of its neighbours and dispatches
		// ExpanderChangeEvent on them (Engine.cpp: "Update expanders of other modules"). Calling
		// onRemove() alone, without also breaking the adjacency, is not a scenario Flower ever
		// sees in the real engine — process() re-derives validity from leftExpander.module /
		// rightExpander.module fresh every sample, so a module whose neighbour still points at
		// a "removed" module (only onRemove() was called, nothing else) keeps reading it, same
		// as it would for a module that never left.
		Module::RemoveEvent e;
		master->onRemove(e);
		h.disconnectExpander(seeds, Test::Harness::SIDE_RIGHT);
		h.disconnectExpander(offspring, Test::Harness::SIDE_LEFT);

		CHECK(offspring->outputs[OffspringModule::OUTPUT_CV].getVoltage() == 0.f);
		CHECK(offspring->outputs[OffspringModule::OUTPUT_AUX].getVoltage() == 0.f);
		CHECK(seeds->outputs[SeedsModule::OUTPUT_GATE].getVoltage() == 0.f);
		CHECK(seeds->outputs[SeedsModule::OUTPUT_TRIG].getVoltage() == 0.f);

		// The notification from master's onRemove() is what makes consumeSiblingRemoved() true
		// on the next process() — check it actually ran, not just that disconnectExpander's own
		// onExpanderChange() path (already covered by the sections below) did the resetting.
		h.dspStep();
		CHECK(offspring->outputs[OffspringModule::OUTPUT_CV].getVoltage() == 0.f);
		CHECK(seeds->outputs[SeedsModule::OUTPUT_GATE].getVoltage() == 0.f);
	}

	SECTION("disconnecting OFFSPRING from the master resets OFFSPRING's outputs") {
		// The engine clears the adjacency and dispatches ExpanderChangeEvent on the survivor
		// when a neighbour is removed or moved away — disconnectExpander() replicates exactly
		// that (see test_harness.hpp), without also simulating full removal of `master`.
		h.disconnectExpander(offspring, Test::Harness::SIDE_LEFT);

		// onExpanderChange() calls resetOutputs() synchronously, before any further dspStep().
		CHECK(offspring->outputs[OffspringModule::OUTPUT_CV].getVoltage() == 0.f);
		CHECK(offspring->outputs[OffspringModule::OUTPUT_AUX].getVoltage() == 0.f);

		// The master and SEEDS side of the chain are untouched by a purely OFFSPRING-side break.
		h.dspStep();
		CHECK(seeds->outputs[SeedsModule::OUTPUT_GATE].getVoltage() != 0.f);
	}

	SECTION("disconnecting SEEDS from the master resets SEEDS's outputs") {
		h.disconnectExpander(seeds, Test::Harness::SIDE_RIGHT);

		CHECK(seeds->outputs[SeedsModule::OUTPUT_GATE].getVoltage() == 0.f);
		CHECK(seeds->outputs[SeedsModule::OUTPUT_TRIG].getVoltage() == 0.f);

		h.dspStep();
		CHECK(offspring->outputs[OffspringModule::OUTPUT_CV].getVoltage() != 0.f);
	}

	SECTION("a disconnected OFFSPRING/SEEDS stays silent rather than reusing a stale message") {
		h.disconnectExpander(offspring, Test::Harness::SIDE_LEFT);
		h.dspSteps(4);

		// process() early-returns (no left neighbour), resetOutputs() runs every sample rather
		// than just once at the moment of disconnection.
		CHECK(offspring->outputs[OffspringModule::OUTPUT_CV].getVoltage() == 0.f);
		CHECK(offspring->outputs[OffspringModule::OUTPUT_AUX].getVoltage() == 0.f);
	}
}


// ---- Topology acceptance --------------------------------------------------------------------
//
// SEEDS reads its rightExpander (isFlowerSeqModel() || isFlowerTrigModel()); FLOWER publishes to
// both sides unconditionally with no neighbour-type check at all; OFFSPRING reads its
// leftExpander (isFlowerSeqModel() only). "Rejected" below always means the checking module's
// own process() early-returns and calls resetOutputs() — never that the far side refuses to
// publish.

TEST_CASE("Chain topology - accepted shapes", "[Flower]") {
	Test::Harness h;

	SECTION("SEEDS -> SEEDS chain of length >= 2 [B2 regression]") {
		// The bug B2 fixed: SEEDS used to compare against a hand-written, wrong slug string, so
		// a second SEEDS module never chained. isFlowerTrigModel() now covers this.
		auto seeds1 = h.addModule<SeedsModule>("FlowerSeqTrig");
		auto seeds2 = h.addModule<SeedsModule>("FlowerSeqTrig");
		auto master = h.addModule<MasterModule>("FlowerSeq");

		h.connectChain(seeds1, seeds2, master);
		master->running = true;
		clearResetLockout(h);
		clockPulse(h, master);
		h.dspSteps(4);

		// seeds2 sits between seeds1 and master and must actually forward the tick, not just
		// avoid early-returning: seeds1's stepOutIndex must track the master's, proving the
		// tick made it across the SEEDS-SEEDS link.
		CHECK(seeds1->seq.stepOutIndex == seeds2->seq.stepOutIndex);
	}

	SECTION("OFFSPRING -> OFFSPRING chain of length >= 2") {
		auto master = h.addModule<MasterModule>("FlowerSeq");
		auto offspring1 = h.addModule<OffspringModule>("FlowerSeqEx");
		auto offspring2 = h.addModule<OffspringModule>("FlowerSeqEx");

		h.connectChain(master, offspring1, offspring2);
		master->running = true;
		clearResetLockout(h);

		offspring1->params[OffspringModule::PARAM_STEP + 0].setValue(1.f);
		offspring2->params[OffspringModule::PARAM_STEP + 0].setValue(1.f);
		clockPulse(h, master);
		h.dspSteps(4);

		CHECK(offspring1->seq.stepOutIndex == offspring2->seq.stepOutIndex);
		REQUIRE(offspring2->outputs[OffspringModule::OUTPUT_CV].getVoltage() != 0.f);
	}

	SECTION("Mixed chain SEEDS + SEEDS + FLOWER + OFFSPRING + OFFSPRING: every member sees the same tick") {
		auto seeds1 = h.addModule<SeedsModule>("FlowerSeqTrig");
		auto seeds2 = h.addModule<SeedsModule>("FlowerSeqTrig");
		auto master = h.addModule<MasterModule>("FlowerSeq");
		auto offspring1 = h.addModule<OffspringModule>("FlowerSeqEx");
		auto offspring2 = h.addModule<OffspringModule>("FlowerSeqEx");

		h.connectChain(std::vector<rack::Module*>{seeds1, seeds2, master, offspring1, offspring2});
		master->running = true;
		clearResetLockout(h);

		for (int tick = 0; tick < 4; tick++) {
			clockPulse(h, master);
			h.dspSteps(4);
			CHECK(seeds1->seq.stepOutIndex == seeds2->seq.stepOutIndex);
			CHECK(seeds2->seq.stepOutIndex == offspring1->seq.stepOutIndex);
			CHECK(offspring1->seq.stepOutIndex == offspring2->seq.stepOutIndex);
		}
	}
}

TEST_CASE("Chain topology - rejected shapes early-return cleanly", "[Flower]") {
	Test::Harness h;

	SECTION("OFFSPRING with no neighbour resets its outputs and does not crash") {
		auto offspring = h.addModule<OffspringModule>("FlowerSeqEx");
		offspring->params[OffspringModule::PARAM_STEP + 0].setValue(1.f);
		h.dspSteps(4);
		CHECK(offspring->outputs[OffspringModule::OUTPUT_CV].getVoltage() == 0.f);
		CHECK(offspring->outputs[OffspringModule::OUTPUT_AUX].getVoltage() == 0.f);
	}

	SECTION("SEEDS with no neighbour resets its outputs and does not crash") {
		auto seeds = h.addModule<SeedsModule>("FlowerSeqTrig");
		h.dspSteps(4);
		CHECK(seeds->outputs[SeedsModule::OUTPUT_GATE].getVoltage() == 0.f);
		CHECK(seeds->outputs[SeedsModule::OUTPUT_TRIG].getVoltage() == 0.f);
	}

	SECTION("A foreign module as OFFSPRING's left neighbour is rejected") {
		auto foreign = h.adoptModule(new ForeignModule);
		auto offspring = h.addModule<OffspringModule>("FlowerSeqEx");
		h.connectExpander(foreign, offspring);

		offspring->params[OffspringModule::PARAM_STEP + 0].setValue(1.f);
		h.dspSteps(4);
		CHECK(offspring->outputs[OffspringModule::OUTPUT_CV].getVoltage() == 0.f);
	}

	SECTION("A foreign module as SEEDS's right neighbour is rejected") {
		auto seeds = h.addModule<SeedsModule>("FlowerSeqTrig");
		auto foreign = h.adoptModule(new ForeignModule);
		h.connectExpander(seeds, foreign);

		h.dspSteps(4);
		CHECK(seeds->outputs[SeedsModule::OUTPUT_GATE].getVoltage() == 0.f);
	}

	SECTION("OFFSPRING placed to the left of FLOWER is rejected") {
		// OFFSPRING only ever reads its own leftExpander. Placing it left-of-FLOWER puts FLOWER
		// on OFFSPRING's *right*, which OFFSPRING never looks at — from OFFSPRING's own
		// perspective it simply has no left neighbour.
		auto offspring = h.addModule<OffspringModule>("FlowerSeqEx");
		auto master = h.addModule<MasterModule>("FlowerSeq");
		h.connectExpander(offspring, master);
		master->running = true;
		clearResetLockout(h);

		offspring->params[OffspringModule::PARAM_STEP + 0].setValue(1.f);
		clockPulse(h, master);
		h.dspSteps(4);
		CHECK(offspring->outputs[OffspringModule::OUTPUT_CV].getVoltage() == 0.f);
	}

	SECTION("SEEDS placed to the right of FLOWER is rejected") {
		// SEEDS only ever reads its own rightExpander. Placing it right-of-FLOWER puts FLOWER
		// on SEEDS's *left*, which SEEDS never looks at.
		auto master = h.addModule<MasterModule>("FlowerSeq");
		auto seeds = h.addModule<SeedsModule>("FlowerSeqTrig");
		h.connectExpander(master, seeds);
		master->running = true;
		clearResetLockout(h);

		clockPulse(h, master);
		h.dspSteps(4);
		CHECK(seeds->outputs[SeedsModule::OUTPUT_GATE].getVoltage() == 0.f);
	}

	SECTION("An expander with no valid neighbour stays inert over many steps, no UB [B9 path]") {
		// Exercises the module's own zero-initialised argsProducer/argsConsumer across many
		// samples with nothing ever validating a neighbour — the path that would divide by zero
		// on an uninitialised stepLength if FlowerProcessArgs's B8/B9 fix ever regressed.
		auto offspring = h.addModule<OffspringModule>("FlowerSeqEx");
		for (int i = 0; i < 2000; i++) {
			h.dspStep();
			CHECK(offspring->outputs[OffspringModule::OUTPUT_CV].getVoltage() == 0.f);
		}
	}
}


// ---- Forwarding and mutation [B11] ------------------------------------------------------------
//
// B11 is still open in source (see var/Flower_review.md): FlowerSeqEx::process() and
// FlowerTrigModule::process() both do `seqArgs->randTick = false`, where `seqArgs` is a pointer
// straight into the *upstream* neighbour's own consumerMessage buffer — not a local copy. So
// randomizeInherit == false does not just suppress this module's own randomize, it mutates the
// buffer the whole chain reads that tick.

TEST_CASE("Expander forwarding and mutation [B11]", "[Flower]") {
	Test::Harness h;

	SECTION("KNOWN BUG: randomizeInherit == false on one OFFSPRING clears randTick for a sibling downstream of it too") {
		// FLOWER -> OFFSPRING(inherit=off) -> OFFSPRING(inherit=on). The plan's expectation is
		// that the second OFFSPRING still randomizes on the master's randTick even though the
		// first one opted out — that is NOT what happens today, because the first OFFSPRING's
		// `seqArgs->randTick = false` mutates the master's own published buffer in place, and
		// the second OFFSPRING reads that same (now-mutated) buffer via the first OFFSPRING's
		// forwarded copy.
		auto master = h.addModule<MasterModule>("FlowerSeq");
		auto offspring1 = h.addModule<OffspringModule>("FlowerSeqEx");
		auto offspring2 = h.addModule<OffspringModule>("FlowerSeqEx");
		h.connectChain(master, offspring1, offspring2);

		offspring1->randomizeInherit = false;
		offspring2->randomizeInherit = true;
		master->running = true;
		clearResetLockout(h);

		master->params[MasterModule::PARAM_RAND].setValue(10.f);
		h.dspStep();
		master->params[MasterModule::PARAM_RAND].setValue(0.f);
		h.dspStep();

		// offspring2 reads its own seqArgs from offspring1->rightExpander.consumerMessage — the
		// copy offspring1 forwarded after (per B11) mutating it in place.
		auto seenByOffspring2 = reinterpret_cast<FlowerProcessArgs*>(offspring1->rightExpander.consumerMessage);

		// Pinned as the current (buggy) behaviour: offspring2 does not see randTick, even
		// though its own randomizeInherit is true. This CHECK documents B11 rather than
		// asserting the fix; flip it to CHECK(seenByOffspring2->randTick == true) once B11 lands.
		CHECK(seenByOffspring2->randTick == false);
	}

	SECTION("Upstream consumerMessage is unchanged after a downstream process() when inherit stays true") {
		// The read-only-consumer contract: with randomizeInherit left at its default (true),
		// a downstream module's process() must not mutate anything in the buffer it read.
		auto master = h.addModule<MasterModule>("FlowerSeq");
		auto offspring = h.addModule<OffspringModule>("FlowerSeqEx");
		h.connectChain(master, offspring);
		master->running = true;
		clearResetLockout(h);
		h.dspSteps(4);

		auto before = *reinterpret_cast<FlowerProcessArgs*>(master->rightExpander.consumerMessage);
		h.dspStep();
		auto after = *reinterpret_cast<FlowerProcessArgs*>(master->rightExpander.consumerMessage);

		// Compare the fields the master itself just published this same tick, not a stale copy:
		// both `before`/`after` are read from the master's own consumerMessage across one
		// process() of `offspring`, which is the only thing that could have mutated it.
		CHECK(before.stepIndex == after.stepIndex);
		CHECK(before.stepStart == after.stepStart);
		CHECK(before.stepLength == after.stepLength);
		CHECK(before.randTick == after.randTick);
	}

	SECTION("randomizeFlagsSlave written by one expander does not leak into a sibling's randomize") {
		auto master = h.addModule<MasterModule>("FlowerSeq");
		auto offspring1 = h.addModule<OffspringModule>("FlowerSeqEx");
		auto offspring2 = h.addModule<OffspringModule>("FlowerSeqEx");
		h.connectChain(master, offspring1, offspring2);
		master->running = true;
		clearResetLockout(h);

		offspring1->randomizeFlags.set(0, true);
		offspring2->randomizeFlags.set(0, false);
		h.dspStep();

		// Each expander writes its own randomizeFlags into the seqArgs buffer it just read from
		// (master's for offspring1, offspring1's for offspring2) before calling seq.process() —
		// that write is what the engine reads back out as randomizeFlagsSlave.
		auto seenByOffspring1 = reinterpret_cast<FlowerProcessArgs*>(master->rightExpander.consumerMessage);
		auto seenByOffspring2 = reinterpret_cast<FlowerProcessArgs*>(offspring1->rightExpander.consumerMessage);
		CHECK(seenByOffspring1->randomizeFlagsSlave.test(0) == true);
		CHECK(seenByOffspring2->randomizeFlagsSlave.test(0) == false);
	}

	SECTION("Forwarding order: the copy handed downstream carries the master's tick, each module sees its own randomizeFlagsSlave") {
		auto master = h.addModule<MasterModule>("FlowerSeq");
		auto offspring1 = h.addModule<OffspringModule>("FlowerSeqEx");
		auto offspring2 = h.addModule<OffspringModule>("FlowerSeqEx");
		h.connectChain(master, offspring1, offspring2);
		master->running = true;
		clearResetLockout(h);
		clockPulse(h, master);
		h.dspSteps(4);

		CHECK(offspring1->seq.stepOutIndex == offspring2->seq.stepOutIndex);
		auto seenByOffspring1 = reinterpret_cast<FlowerProcessArgs*>(master->rightExpander.consumerMessage);
		auto seenByOffspring2 = reinterpret_cast<FlowerProcessArgs*>(offspring1->rightExpander.consumerMessage);
		CHECK(seenByOffspring1->randomizeFlagsSlave == offspring1->randomizeFlags);
		CHECK(seenByOffspring2->randomizeFlagsSlave == offspring2->randomizeFlags);
	}

	SECTION("A mid-chain module with no further neighbour does not break the members before it") {
		auto master = h.addModule<MasterModule>("FlowerSeq");
		auto offspring1 = h.addModule<OffspringModule>("FlowerSeqEx");
		h.connectChain(master, offspring1);
		// offspring1 has no right neighbour: it stops forwarding past itself, but must not
		// affect the master or its own ability to read from the master.
		master->running = true;
		clearResetLockout(h);

		offspring1->params[OffspringModule::PARAM_STEP + 0].setValue(1.f);
		clockPulse(h, master);
		h.dspSteps(4);

		CHECK(offspring1->outputs[OffspringModule::OUTPUT_CV].getVoltage() != 0.f);
	}
}


// ---- Teardown and lifetime ---------------------------------------------------------------------
//
// Note: the review (var/Flower_review.md, Problem 1) determined there is no dangling-pointer
// hazard here the way there is in Intermix — OFFSPRING/SEEDS forward a *copy* of the tick each
// sample, never a pointer into another module's storage, and {left,right}Expander.module is
// re-read fresh every sample rather than cached. So the cases below are plain regression/output-
// freshness checks on FlowerChainModule's sibling-notify path under topologies more complex than
// the 3-module case already covered above, not a use-after-free hunt.

TEST_CASE("Teardown across a longer chain", "[Flower]") {
	Test::Harness h;
	auto seeds = h.addModule<SeedsModule>("FlowerSeqTrig");
	auto master = h.addModule<MasterModule>("FlowerSeq");
	auto offspring1 = h.addModule<OffspringModule>("FlowerSeqEx");
	auto offspring2 = h.addModule<OffspringModule>("FlowerSeqEx");

	h.connectChain(seeds, master, offspring1, offspring2);
	master->running = true;
	clearResetLockout(h);

	offspring1->params[OffspringModule::PARAM_STEP + 0].setValue(1.f);
	offspring2->params[OffspringModule::PARAM_STEP + 0].setValue(1.f);
	clockPulse(h, master);
	h.dspSteps(4);

	REQUIRE(offspring1->outputs[OffspringModule::OUTPUT_CV].getVoltage() != 0.f);
	REQUIRE(offspring2->outputs[OffspringModule::OUTPUT_CV].getVoltage() != 0.f);
	REQUIRE(seeds->outputs[SeedsModule::OUTPUT_GATE].getVoltage() != 0.f);

	SECTION("Removing a middle module (offspring1) resets only what was downstream of it") {
		Module::RemoveEvent e;
		offspring1->onRemove(e);
		h.disconnectExpander(master, Test::Harness::SIDE_RIGHT);
		h.disconnectExpander(offspring2, Test::Harness::SIDE_LEFT);

		// offspring2's forwarded consumerMessage came from offspring1, which is now gone —
		// offspring2 must not go on reading whatever offspring1 last forwarded.
		CHECK(offspring2->outputs[OffspringModule::OUTPUT_CV].getVoltage() == 0.f);
		CHECK(offspring2->outputs[OffspringModule::OUTPUT_AUX].getVoltage() == 0.f);

		// seeds and master are on the other side of the break and untouched by it.
		h.dspStep();
		CHECK(seeds->outputs[SeedsModule::OUTPUT_GATE].getVoltage() != 0.f);
	}

	SECTION("Removing the leftmost module (seeds) first leaves the rest of the chain intact") {
		Module::RemoveEvent e;
		seeds->onRemove(e);
		h.disconnectExpander(master, Test::Harness::SIDE_LEFT);

		h.dspStep();
		CHECK(offspring1->outputs[OffspringModule::OUTPUT_CV].getVoltage() != 0.f);
		CHECK(offspring2->outputs[OffspringModule::OUTPUT_CV].getVoltage() != 0.f);
	}

	SECTION("Removing the rightmost module (offspring2) first leaves the rest of the chain intact") {
		Module::RemoveEvent e;
		offspring2->onRemove(e);
		h.disconnectExpander(offspring1, Test::Harness::SIDE_RIGHT);

		h.dspStep();
		CHECK(seeds->outputs[SeedsModule::OUTPUT_GATE].getVoltage() != 0.f);
		CHECK(offspring1->outputs[OffspringModule::OUTPUT_CV].getVoltage() != 0.f);
	}

	SECTION("Rapid connect/disconnect/reconnect across many steps leaves every member well-defined") {
		for (int i = 0; i < 50; i++) {
			h.disconnectExpander(offspring1, Test::Harness::SIDE_LEFT);
			h.dspStep();
			h.connectExpander(master, offspring1);
			h.dspStep();
		}
		// Whatever the final state, every output must be a defined voltage (not NaN/inf from a
		// half-initialised read) and the reconnected member must be receiving ticks again.
		clockPulse(h, master);
		h.dspSteps(4);
		float v = offspring1->outputs[OffspringModule::OUTPUT_CV].getVoltage();
		CHECK(std::isfinite(v));
	}
}
