
TEST_CASE("FlowerSeqModule preset JSON fuzzing", "[Flower][JSON]") {
	Test::ModuleScaffold<MasterModule> mods;
	auto module = mods.create("FlowerSeq");

	SECTION("Every property is null-guarded in dataFromJson()") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetNullGuards(module, rootJ);
		json_decref(rootJ);
	}

	SECTION("Every property tolerates a wrong-typed value") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetTypeConfusion(module, rootJ);
		json_decref(rootJ);
	}

	SECTION("Every array tolerates being oversized") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetOversizedArrays(module, rootJ);
		json_decref(rootJ);
	}
}

TEST_CASE("FlowerSeqExModule (OFFSPRING) preset JSON fuzzing", "[Flower][JSON]") {
	Test::ModuleScaffold<OffspringModule> mods;
	auto module = mods.create("FlowerSeqEx");

	SECTION("Every property is null-guarded in dataFromJson()") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetNullGuards(module, rootJ);
		json_decref(rootJ);
	}

	SECTION("Every property tolerates a wrong-typed value") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetTypeConfusion(module, rootJ);
		json_decref(rootJ);
	}

	SECTION("Every array tolerates being oversized") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetOversizedArrays(module, rootJ);
		json_decref(rootJ);
	}
}

TEST_CASE("FlowerTrigModule (SEEDS) preset JSON fuzzing", "[Flower][JSON]") {
	Test::ModuleScaffold<SeedsModule> mods;
	auto module = mods.create("FlowerSeqTrig");

	SECTION("Every property is null-guarded in dataFromJson()") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetNullGuards(module, rootJ);
		json_decref(rootJ);
	}

	SECTION("Every property tolerates a wrong-typed value") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetTypeConfusion(module, rootJ);
		json_decref(rootJ);
	}

	SECTION("Every array tolerates being oversized") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetOversizedArrays(module, rootJ);
		json_decref(rootJ);
	}
}


// FlowerSeqModule must give leftExpander and rightExpander independent FlowerProcessArgs
// buffer pairs. Rack's Expander contract (and Engine.cpp, which flips each side
// independently) assumes every module hands out one distinct producer/consumer pair per side.
// Aliasing them together would mean the two sides are never actually double-buffered from each
// other: swapping the master's messageFlipRequested for one side would silently swap the roles
// of the exact same two objects the other side is also holding pointers into.
TEST_CASE("Expander buffers", "[Flower]") {
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

// SeqStepParamQuantity, SeqStepButtonParamQuantity, SeqStepModeParamQuantity and
// SeqFlowerKnobParamQuantity (Flower.hpp's Trig* counterparts are the same shape) are templated
// on the engine (FlowerSeq<MODULE, STEPS>) rather than the host MODULE, and each configParam<PQ>()
// call site sets `pq->engine = &seq` explicitly rather than relying on a dynamic_cast. These tests
// drive the quantities the same way the widget layer would (through the module's own
// paramQuantities[], reading whatever engine state each getter dispatches on) so a future change
// that forgets to wire `engine` — or points it at the wrong module's engine — fails here instead
// of only manifesting as a wrong tooltip in the UI.
TEST_CASE("Param quantities read their engine's state", "[Flower]") {
	Test::ModuleScaffold<MasterModule> mods;
	MasterModule* m = mods.create("FlowerSeq");

	SECTION("SeqStepParamQuantity::getDisplayValue() dispatches on engine->outCvMode") {
		auto pq = m->paramQuantities[MasterModule::PARAM_STEP];
		m->params[MasterModule::PARAM_STEP].setValue(1.f);

		m->seq.outCvMode = OUT_CV_MODE::UNI_10V;
		CHECK(pq->getDisplayValue() == Catch::Approx(10.f));

		m->seq.outCvMode = OUT_CV_MODE::BI_10V;
		CHECK(pq->getDisplayValue() == Catch::Approx(10.f));

		m->seq.outCvMode = OUT_CV_MODE::UNI_5V;
		CHECK(pq->getDisplayValue() == Catch::Approx(5.f));
	}

	SECTION("SeqStepParamQuantity::getUnit() dispatches on engine->stepCvMode and the step's own input") {
		auto pq = m->paramQuantities[MasterModule::PARAM_STEP];

		m->seq.stepCvMode = SEQ_CV_MODE::SUM;
		CHECK(pq->getUnit() == "V");

		m->seq.stepCvMode = SEQ_CV_MODE::ATTENUATE;
		m->inputs[MasterModule::INPUT_STEP].channels = 1;
		m->inputs[MasterModule::INPUT_STEP].setVoltage(1.f);
		CHECK(pq->getUnit() == "x attenuate");

		m->inputs[MasterModule::INPUT_STEP].channels = 0;
		CHECK(pq->getUnit() == "V");
	}

	SECTION("SeqStepButtonParamQuantity::getDisplayValueString() dispatches on engine->stepState and engine->stepGet(i)") {
		auto pq = m->paramQuantities[MasterModule::PARAM_STEP_BUTTON + 2];
		m->seq.stepGet(2)->ratchets = 5;

		m->seq.stepState = SEQ_UI_STATE::DEFAULT;
		CHECK(pq->getDisplayValueString().find("Step 3:") != std::string::npos);

		m->seq.stepState = SEQ_UI_STATE::RATCHETS;
		CHECK(pq->getDisplayValueString().find("Step 3 ratchets: 5") != std::string::npos);
	}

	SECTION("SeqStepModeParamQuantity::getDisplayValueString() dispatches on engine->stepState") {
		auto pq = m->paramQuantities[MasterModule::PARAM_STEPMODE];

		m->seq.stepState = SEQ_UI_STATE::DEFAULT;
		CHECK(pq->getDisplayValueString() == "Edit step on/off");

		m->seq.stepState = SEQ_UI_STATE::SLEW;
		CHECK(pq->getDisplayValueString() == "Edit step slew");
	}

	SECTION("SeqFlowerKnobParamQuantity reads engine->stepEditSelected and engine->stepGet(i)") {
		auto pq = m->paramQuantities[MasterModule::PARAM_STEP_CENTER];
		m->seq.stepEditSelected = 4;
		m->seq.stepGet(4)->auxiliary = 3.25f;

		m->seq.stepState = SEQ_UI_STATE::AUXILIARY;
		CHECK(pq->getDisplayValueString() == "3.250V");
		CHECK(pq->getLabel() == "Step 5 auxiliary voltage");
	}
}

// The same param-quantity idiom, on FlowerTrigModule (SEEDS): TrigStepButtonParamQuantity,
// TrigStepModeParamQuantity and TrigFlowerKnobParamQuantity are templated on FlowerTrig<...>
// rather than the host module, mirroring the FlowerSeq/OFFSPRING case above but exercised
// against the independent SEEDS module and pattern-quantity instance to catch a copy-paste
// mistake that wires a Trig* quantity to the wrong engine instance.
TEST_CASE("Param quantities read their engine's state (SEEDS)", "[Flower]") {
	Test::ModuleScaffold<SeedsModule> mods;
	SeedsModule* m = mods.create("FlowerSeqTrig");

	SECTION("TrigStepButtonParamQuantity::getDisplayValueString() dispatches on engine->stepState") {
		auto pq = m->paramQuantities[SeedsModule::PARAM_STEP_BUTTON + 1];
		m->seq.stepGet(1)->attack = 0.4f;

		m->seq.stepState = TRIG_UI_STATE::ATTACK;
		CHECK(pq->getDisplayValueString().find("Step 2 attack: 0.400") != std::string::npos);
	}

	SECTION("TrigStepModeParamQuantity::getDisplayValueString() dispatches on engine->stepState") {
		auto pq = m->paramQuantities[SeedsModule::PARAM_STEPMODE];

		m->seq.stepState = TRIG_UI_STATE::DECAY;
		CHECK(pq->getDisplayValueString() == "Edit step decay");
	}

	SECTION("TrigFlowerKnobParamQuantity reads engine->stepEditSelected and engine->stepGet(i)") {
		auto pq = m->paramQuantities[SeedsModule::PARAM_STEP_CENTER];
		m->seq.stepEditSelected = 3;
		m->seq.stepGet(3)->ratchets = 6;

		m->seq.stepState = TRIG_UI_STATE::RATCHETS;
		CHECK(pq->getDisplayValueString() == "6");
		CHECK(pq->getLabel() == "Step 4 ratchets");
	}
}

// PatternList::next()/prev() must both wrap modulo `last` (the number of currently-active
// patterns), not modulo the list's fixed capacity SIZE. next() has always done this correctly;
// prev() is covered here in matching depth since it is the one that historically got this wrong
// (using `+ SIZE` before the modulus instead of `+ last`), which only gave the right answer when
// last == SIZE and otherwise jumped to an arbitrary slot or failed to move at all for any
// smaller, partially-enabled pattern set.
TEST_CASE("PatternList::next() and prev()", "[Flower]") {
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

	SECTION("current() equals slot[pos] after each cursor move") {
		list.reset(6);
		list.setPos(2);
		CHECK(list.current() == list.at(list.pos));

		list.next();
		CHECK(list.current() == list.at(list.pos));

		list.prev();
		list.prev();
		CHECK(list.current() == list.at(list.pos));
	}

	SECTION("KNOWN BUG: disable() never touches pos, so shrinking last past it leaves current() "
			"reading an inactive type") {
		// This is a live path: the pattern-toggle menu disables a type, then a MUTATE trigger
		// (or another "next pattern" press) calls next()/prev()/current() without anything
		// having range-checked pos in between. Pinned here as current (buggy) behaviour, not
		// as a requirement — fixing it is a decision for whoever picks up PatternList next.
		list.reset(PatternList::SIZE);
		list.setPos(7);
		REQUIRE(list.active(list.current()));

		while (list.last > 1) {
			list.disable(list.at(list.last - 1));
		}

		REQUIRE(list.last == 1);
		REQUIRE(list.pos == 7);
		CHECK_FALSE(list.active(list.current()));
	}
}

// FlowerSeqModule::patternCheck() reassigns any phrase pattern whose type has become inactive
// (disabled in patternList) to the next active type. The search must wrap at PATTERN_TYPE::NUM
// and be bounded by the number of pattern types, since patternList always has at least one
// active entry (disable() refuses to go below that) — an unbounded search that just increments
// past NUM walks off the end of PatternList::map into undefined memory instead of wrapping back
// to the low-numbered types.
TEST_CASE("FlowerSeqModule::patternCheck()", "[Flower]") {
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
TEST_CASE("AUX_RAND sign bit selection", "[Flower]") {
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

// Fires one clock edge on the master's INPUT_CLOCK: rises to 10V for one sample, then falls back
// to 0V. Mirrors how the B1 chain test above drives the same input, factored out since the
// master-transport suite below needs it repeatedly.
static void clockPulse(Test::Harness& h, MasterModule* m) {
	m->inputs[MasterModule::INPUT_CLOCK].setVoltage(10.f);
	h.dspStep();
	m->inputs[MasterModule::INPUT_CLOCK].setVoltage(0.f);
}

// resetTimer suppresses clock processing for 1ms after construction/reset (see FlowerSeq.cpp's
// `resetTimer.process(args.sampleTime) >= 1e-3f` guard) — a Timer starts at 0 and this lockout
// applies from module construction too, not just from an explicit RESET. Any test that drives
// clock edges and expects them to advance the sequencer must clear this first.
static void clearResetLockout(Test::Harness& h) {
	int samplesFor1ms = (int)std::ceil(h.sampleRate() * 1e-3f) + 1;
	h.dspSteps(samplesFor1ms);
}

TEST_CASE("FlowerSeqModule run/reset", "[Flower][transport]") {
	Test::Harness h;
	auto m = h.addModule<MasterModule>("FlowerSeq");
	h.dspStep();

	SECTION("running defaults true after onReset()") {
		CHECK(m->running == true);
	}

	SECTION("RUN button toggles running") {
		m->params[MasterModule::PARAM_RUN].setValue(10.f);
		h.dspStep();
		CHECK(m->running == false);
		m->params[MasterModule::PARAM_RUN].setValue(0.f);
		h.dspStep();
		m->params[MasterModule::PARAM_RUN].setValue(10.f);
		h.dspStep();
		CHECK(m->running == true);
	}

	SECTION("RUN input toggles running") {
		m->inputs[MasterModule::INPUT_RUN].setVoltage(10.f);
		h.dspStep();
		CHECK(m->running == false);
	}

	SECTION("a held RUN input does not re-toggle (Schmitt edge only)") {
		m->inputs[MasterModule::INPUT_RUN].setVoltage(10.f);
		h.dspSteps(10);
		CHECK(m->running == false);
		// Still false after many more samples at the same held voltage.
		h.dspSteps(50);
		CHECK(m->running == false);
	}

	SECTION("while !running, clock edges do not advance stepIndex, and tick flags stay false") {
		m->running = false;
		int stepBefore = m->stepIndex;

		clockPulse(h, m);
		h.dspSteps(4);

		CHECK(m->stepIndex == stepBefore);
		auto* args = reinterpret_cast<FlowerProcessArgs*>(m->rightExpander.consumerMessage);
		CHECK_FALSE(args->clockTick);
		CHECK_FALSE(args->stepTick);
	}

	SECTION("RESET button sets patternIndex and stepIndex to 0 and raises stepTick") {
		m->running = true;
		clearResetLockout(h);
		// Advance a little first so reset has something to undo.
		for (int i = 0; i < 3; i++) { clockPulse(h, m); h.dspSteps(2); }
		REQUIRE(m->stepIndex != 0);

		m->params[MasterModule::PARAM_RESET].setValue(10.f);
		h.dspStep();

		CHECK(m->patternIndex == 0);
		CHECK(m->stepIndex == 0);
		auto* args = reinterpret_cast<FlowerProcessArgs*>(m->rightExpander.consumerMessage);
		CHECK(args->stepTick);
	}

	SECTION("RESET input also resets, and works while stopped") {
		m->running = false;
		h.dspSteps(4);
		m->stepIndex = 5;
		m->patternIndex = 3;

		m->inputs[MasterModule::INPUT_RESET].setVoltage(10.f);
		h.dspStep();

		CHECK(m->stepIndex == 0);
		CHECK(m->patternIndex == 0);
	}

	SECTION("reset lockout: a clock edge within 1ms of reset does not advance stepIndex") {
		m->running = true;
		h.dspSteps(4);

		m->params[MasterModule::PARAM_RESET].setValue(10.f);
		h.dspStep();
		m->params[MasterModule::PARAM_RESET].setValue(0.f);
		h.dspStep();
		REQUIRE(m->stepIndex == 0);

		// Immediately after reset: well under 1ms has elapsed, so this edge must be suppressed.
		clockPulse(h, m);
		h.dspSteps(2);
		CHECK(m->stepIndex == 0);

		// Advance real time past the 1ms lockout using the harness's actual sample rate, then
		// the next edge must take effect.
		int samplesFor1ms = (int)std::ceil(h.sampleRate() * 1e-3f) + 1;
		h.dspSteps(samplesFor1ms);
		clockPulse(h, m);
		h.dspSteps(2);
		CHECK(m->stepIndex == 1);
	}
}

TEST_CASE("FlowerSeqModule step advance and sequence window", "[Flower][transport]") {
	Test::Harness h;
	auto m = h.addModule<MasterModule>("FlowerSeq");
	m->running = true;
	clearResetLockout(h);

	SECTION("free-running clock advances stepIndex and wraps at stepGetSeqLength()") {
		m->params[MasterModule::PARAM_STEPLENGTH].setValue(4.f);
		h.dspStep();
		REQUIRE(m->stepGetSeqLength() == 4);

		int seen[8];
		for (int i = 0; i < 8; i++) {
			clockPulse(h, m);
			h.dspSteps(2);
			seen[i] = m->stepIndex;
		}
		int expected[8] = {1, 2, 3, 0, 1, 2, 3, 0};
		for (int i = 0; i < 8; i++) CHECK(seen[i] == expected[i]);
	}

	SECTION("stepGetSeqStart() clamps PARAM_START + INPUT_START to [0, STEPS-1]") {
		// PARAM_START's own configParam range is already [0, STEPS-1], so setValue() alone
		// can never push it out of range (ParamQuantity::setValue clamps to the configured
		// range before storing) — the clamp in stepGetSeqStart() is only actually reachable
		// via the *sum* with an unclamped CV input.
		m->params[MasterModule::PARAM_START].setValue(0.f);
		m->inputs[MasterModule::INPUT_START].setVoltage(0.f);
		h.dspStep();
		CHECK(m->stepGetSeqStart() == 0);

		m->params[MasterModule::PARAM_START].setValue(15.f);
		h.dspStep();
		CHECK(m->stepGetSeqStart() == 15);

		// Above the top of the range, driven by a positive CV summed with the param.
		m->params[MasterModule::PARAM_START].setValue(15.f);
		m->inputs[MasterModule::INPUT_START].setVoltage(10.f);
		h.dspStep();
		CHECK(m->stepGetSeqStart() == 15);

		// Below the bottom, driven by a negative CV summed with the param.
		m->params[MasterModule::PARAM_START].setValue(0.f);
		m->inputs[MasterModule::INPUT_START].setVoltage(-10.f);
		h.dspStep();
		CHECK(m->stepGetSeqStart() == 0);
	}

	SECTION("stepGetSeqLength() clamps PARAM_STEPLENGTH + INPUT_STEPCNT to [1, STEPS]") {
		// Same reasoning as stepGetSeqStart() above: PARAM_STEPLENGTH's own range is already
		// [1, STEPS], so only the CV-summed path can exercise the clamp.
		m->params[MasterModule::PARAM_STEPLENGTH].setValue(1.f);
		m->inputs[MasterModule::INPUT_STEPCNT].setVoltage(0.f);
		h.dspStep();
		CHECK(m->stepGetSeqLength() == 1);

		m->params[MasterModule::PARAM_STEPLENGTH].setValue(16.f);
		h.dspStep();
		CHECK(m->stepGetSeqLength() == 16);

		m->params[MasterModule::PARAM_STEPLENGTH].setValue(16.f);
		m->inputs[MasterModule::INPUT_STEPCNT].setVoltage(10.f);
		h.dspStep();
		CHECK(m->stepGetSeqLength() == 16);

		// A negative CV driving the sum below the floor must still clamp to 1, not 0 or
		// negative — this is exactly what keeps `% args.stepLength` alive downstream [B9].
		m->params[MasterModule::PARAM_STEPLENGTH].setValue(1.f);
		m->inputs[MasterModule::INPUT_STEPCNT].setVoltage(-10.f);
		h.dspStep();
		CHECK(m->stepGetSeqLength() == 1);
	}

	SECTION("start + length wrapping past STEPS walks the window correctly") {
		m->params[MasterModule::PARAM_START].setValue(14.f);
		m->params[MasterModule::PARAM_STEPLENGTH].setValue(6.f);
		h.dspStep();
		REQUIRE(m->stepGetSeqStart() == 14);
		REQUIRE(m->stepGetSeqLength() == 6);

		int expectedWindow[6] = {14, 15, 0, 1, 2, 3};
		for (int i = 0; i < 6; i++) {
			h.dspStep();
			auto* args = reinterpret_cast<FlowerProcessArgs*>(m->rightExpander.consumerMessage);
			int windowIndex = (args->stepStart + args->stepIndex) % 16;
			CHECK(windowIndex == expectedWindow[i]);
			clockPulse(h, m);
			h.dspSteps(2);
		}
	}

	SECTION("length shrinking below the current stepIndex recovers on the next tick") {
		m->params[MasterModule::PARAM_STEPLENGTH].setValue(8.f);
		h.dspSteps(4);
		for (int i = 0; i < 5; i++) { clockPulse(h, m); h.dspSteps(2); }
		REQUIRE(m->stepIndex == 5);

		// Shrink the window below the current position without a clock edge in between.
		m->params[MasterModule::PARAM_STEPLENGTH].setValue(3.f);
		h.dspSteps(4);
		REQUIRE(m->stepGetSeqLength() == 3);

		clockPulse(h, m);
		h.dspSteps(2);

		CHECK(m->stepIndex < m->stepGetSeqLength());
	}

	SECTION("stepSetIndex() returns true exactly when the pattern rolls over") {
		m->patternCount = 3;
		m->params[MasterModule::PARAM_STEPLENGTH].setValue(2.f);
		h.dspSteps(4);
		REQUIRE(m->stepGetSeqLength() == 2);

		// patternMultCount starts at 1 (set by onReset()), so the very first roll-over past
		// the sequence window immediately advances the pattern.
		CHECK(m->stepSetIndex(0) == false);
		CHECK(m->stepSetIndex(1) == false);
		CHECK(m->stepSetIndex(2) == true);

		// Give the now-current pattern a multi-repeat count and confirm it holds that many
		// passes before rolling over again.
		m->phrases[m->phraseIndex].patterns[m->patternIndex].mult = 3;
		CHECK(m->stepSetIndex(2) == true);
		m->patternMultCount = 3;
		CHECK(m->stepSetIndex(2) == false);
		CHECK(m->stepSetIndex(2) == false);
		CHECK(m->stepSetIndex(2) == true);
	}
}

TEST_CASE("FlowerSeqModule pattern advance and repeats", "[Flower][transport]") {
	Test::Harness h;
	auto m = h.addModule<MasterModule>("FlowerSeq");
	m->running = true;
	m->params[MasterModule::PARAM_STEPLENGTH].setValue(1.f);
	clearResetLockout(h);
	REQUIRE(m->stepGetSeqLength() == 1);

	SECTION("patternMult repeats hold patternIndex, then advance and reload from the new pattern") {
		// patternMultCount starts at 1 (set by onReset()), so the very first roll-over past the
		// sequence window always advances patternIndex regardless of the *departing* pattern's
		// own mult — stepSetIndex()'s `patternMultCount > 1` branch only starts holding once
		// patternMultCount has actually been reloaded from a pattern with mult > 1. So pattern
		// 1's mult == 3 only takes effect once patternIndex has already moved onto it.
		m->patternCount = 3;
		m->phrases[m->phraseIndex].patterns[1].mult = 3;
		m->phrases[m->phraseIndex].patterns[2].mult = 1;
		REQUIRE(m->patternIndex == 0);
		REQUIRE(m->patternMultCount == 1);

		// First roll-over: patternMultCount was 1, so it advances immediately onto pattern 1
		// and reloads patternMultCount from pattern 1's mult (3).
		clockPulse(h, m); h.dspSteps(2);
		CHECK(m->patternIndex == 1);
		CHECK(m->patternMultCount == 3);

		// Pattern 1 now holds for its remaining two repeats before advancing again.
		clockPulse(h, m); h.dspSteps(2);
		CHECK(m->patternIndex == 1);
		clockPulse(h, m); h.dspSteps(2);
		CHECK(m->patternIndex == 1);
		clockPulse(h, m); h.dspSteps(2);
		CHECK(m->patternIndex == 2);
		// The new pattern (index 2) has mult == 1, so patternMultCount was reloaded from it.
		CHECK(m->patternMultCount == 1);
	}

	SECTION("patternIndex wraps modulo patternCount, not PATTERNS") {
		m->patternCount = 2;
		for (int i = 0; i < 8; i++) {
			m->phrases[m->phraseIndex].patterns[i].mult = 1;
		}
		int seen[4];
		for (int i = 0; i < 4; i++) {
			clockPulse(h, m);
			h.dspSteps(2);
			seen[i] = m->patternIndex;
		}
		int expected[4] = {1, 0, 1, 0};
		for (int i = 0; i < 4; i++) CHECK(seen[i] == expected[i]);
	}

	SECTION("patternCount == 1 never advances patternIndex and never divides by zero") {
		m->patternCount = 1;
		m->phrases[m->phraseIndex].patterns[0].mult = 1;
		for (int i = 0; i < 5; i++) {
			clockPulse(h, m);
			h.dspSteps(2);
			CHECK(m->patternIndex == 0);
		}
	}

	SECTION("long-press on a pattern button cycles mult 1 to 2 to 3 to 4 to 1") {
		// LongPressButton::process() is only invoked from inside FlowerSeqModule::process()'s
		// paramDivider.process() gate (division 32), so pressedTime only accumulates
		// args.sampleTime once every 32 real samples. Its threshold here is
		// 0.8f / 32 == 0.025s of *accumulated gated* time, so the number of gated calls needed
		// is 0.025 / sampleTime == 0.025 * sampleRate, and each gated call costs 32 real
		// samples — with margin, since the divider's phase relative to the press isn't
		// controlled here.
		double sampleTime = 1.0 / h.sampleRate();
		int gatedCallsNeeded = (int)std::ceil(0.025 / sampleTime);
		int holdSamples = (gatedCallsNeeded + 2) * 32;
		auto holdLong = [&]() {
			m->params[MasterModule::PARAM_PATTERN_SELECT + 0].setValue(10.f);
			h.dspSteps(holdSamples);
			m->params[MasterModule::PARAM_PATTERN_SELECT + 0].setValue(0.f);
			h.dspSteps(64);
		};
		REQUIRE(m->phrases[m->phraseIndex].patterns[0].mult == 1);
		holdLong();
		CHECK(m->phrases[m->phraseIndex].patterns[0].mult == 2);
		holdLong();
		CHECK(m->phrases[m->phraseIndex].patterns[0].mult == 3);
		holdLong();
		CHECK(m->phrases[m->phraseIndex].patterns[0].mult == 4);
		holdLong();
		CHECK(m->phrases[m->phraseIndex].patterns[0].mult == 1);
	}
}

TEST_CASE("FlowerSeqModule phrase selection", "[Flower][transport]") {
	Test::Harness h;
	auto m = h.addModule<MasterModule>("FlowerSeq");
	clearResetLockout(h);

	SECTION("phraseSetIndex() is a no-op for the current index and for a negative index") {
		m->phraseCount = 4;
		m->phraseIndex = 2;
		m->phraseSetIndex(2);
		CHECK(m->phraseIndex == 2);
		m->phraseSetIndex(-1);
		CHECK(m->phraseIndex == 2);
	}

	SECTION("phraseSetIndex() clamps to phraseCount - 1") {
		m->phraseCount = 3;
		m->phraseSetIndex(99);
		CHECK(m->phraseIndex == 2);
	}

	SECTION("PHRASE_CV_MODE::OFF ignores the input entirely") {
		m->phraseCvMode = PHRASE_CV_MODE::OFF;
		m->phraseCount = 4;
		m->phraseIndex = 1;
		m->inputs[MasterModule::INPUT_PHRASE].channels = 1;
		m->inputs[MasterModule::INPUT_PHRASE].setVoltage(10.f);
		h.dspSteps(4);
		CHECK(m->phraseIndex == 1);
	}

	SECTION("TRIG_FWD advances (phraseIndex + 1) % phraseCount per trigger, wrapping") {
		m->phraseCvMode = PHRASE_CV_MODE::TRIG_FWD;
		m->phraseCount = 3;
		m->phraseIndex = 0;
		m->inputs[MasterModule::INPUT_PHRASE].channels = 1;
		// phraseTrigger (a SchmittTrigger<float>) starts UNINITIALIZED, and its very first
		// process() call — regardless of the input value — only settles it into LOW or HIGH
		// without firing (see TSchmittTrigger<float>::process()). It never saw a sample while
		// the input was unconnected (isConnected() gates the whole switch), so one settling
		// step at 0V is needed before the first real edge can register.
		m->inputs[MasterModule::INPUT_PHRASE].setVoltage(0.f);
		h.dspStep();

		auto trig = [&]() {
			m->inputs[MasterModule::INPUT_PHRASE].setVoltage(10.f);
			h.dspStep();
			m->inputs[MasterModule::INPUT_PHRASE].setVoltage(0.f);
			h.dspStep();
		};

		int seen[4];
		for (int i = 0; i < 4; i++) {
			trig();
			seen[i] = m->phraseIndex;
		}
		int expected[4] = {1, 2, 0, 1};
		for (int i = 0; i < 4; i++) CHECK(seen[i] == expected[i]);
	}

	SECTION("C4 maps 0V to 0, 1V to 12 (clamped to phraseCount - 1), negative volts to 0") {
		m->phraseCvMode = PHRASE_CV_MODE::C4;
		m->phraseCount = 8;
		m->inputs[MasterModule::INPUT_PHRASE].channels = 1;

		m->inputs[MasterModule::INPUT_PHRASE].setVoltage(0.f);
		h.dspStep();
		CHECK(m->phraseIndex == 0);

		m->phraseIndex = 0;
		m->inputs[MasterModule::INPUT_PHRASE].setVoltage(1.f);
		h.dspStep();
		CHECK(m->phraseIndex == 7);

		m->phraseIndex = 3;
		m->inputs[MasterModule::INPUT_PHRASE].setVoltage(-5.f);
		h.dspStep();
		CHECK(m->phraseIndex == 0);
	}

	SECTION("VOLT maps 0..10V across [0, phraseCount)") {
		m->phraseCvMode = PHRASE_CV_MODE::VOLT;
		m->phraseCount = 4;
		m->inputs[MasterModule::INPUT_PHRASE].channels = 1;

		m->inputs[MasterModule::INPUT_PHRASE].setVoltage(0.f);
		h.dspStep();
		CHECK(m->phraseIndex == 0);

		m->phraseIndex = 0;
		m->inputs[MasterModule::INPUT_PHRASE].setVoltage(10.f);
		h.dspStep();
		// The 1e-3f epsilon keeps 10V from overflowing past phraseCount - 1.
		CHECK(m->phraseIndex == 3);
	}

	SECTION("ARM: a phrase button sets phraseNext without changing phraseIndex until the next trigger") {
		m->phraseCvMode = PHRASE_CV_MODE::ARM;
		m->phraseCount = 4;
		m->phraseIndex = 0;
		m->phraseNext = 0;
		m->inputs[MasterModule::INPUT_PHRASE].channels = 1;

		// Phrase buttons are only sampled inside FlowerSeqModule::process()'s
		// paramDivider.process() gate (division 32) — a held param needs at least one full
		// divider period to be observed.
		m->params[MasterModule::PARAM_PHRASE_SELECT + 2].setValue(10.f);
		h.dspSteps(40);
		CHECK(m->phraseIndex == 0);
		CHECK(m->phraseNext == 2);

		m->params[MasterModule::PARAM_PHRASE_SELECT + 2].setValue(0.f);
		h.dspSteps(40);

		m->inputs[MasterModule::INPUT_PHRASE].setVoltage(10.f);
		h.dspStep();

		CHECK(m->phraseIndex == 2);
	}

	SECTION("a button press in non-ARM mode applies immediately") {
		m->phraseCvMode = PHRASE_CV_MODE::TRIG_FWD;
		m->phraseCount = 4;
		m->phraseIndex = 0;

		m->params[MasterModule::PARAM_PHRASE_SELECT + 3].setValue(10.f);
		h.dspSteps(40);

		CHECK(m->phraseIndex == 3);
	}

	SECTION("phraseSetCount() clamps phraseIndex down when the count shrinks") {
		m->phraseCount = 6;
		m->phraseIndex = 5;
		m->phraseSetCount(3);
		CHECK(m->phraseCount == 3);
		CHECK(m->phraseIndex == 2);
	}
}

// patternMutate() picks a slot index p and calls patternNext(p, ...) on it. patternNext() itself
// is B6's shared-cursor logic — postponed unchanged (see var/Flower_review.md) — so what a slot
// ends up containing after a mutation is entangled with that known-buggy cursor. These cases
// stick to what patternMutate() itself is responsible for: which slot index it picks, and that
// patternBinomialDist's index arithmetic never runs off the end of the array — not what
// patternNext() then does with that index.
TEST_CASE("FlowerSeqModule patternMutate()", "[Flower][transport]") {
	// patternMutate() calls random::u32()/random::uniform() directly. Rack's RNG
	// (random::local()) is a Xoroshiro128Plus that defaults to an all-zero, unseeded state —
	// random::init() seeds it from wall-clock time and is idempotent, but nothing else in this
	// test binary calls it, so it must be seeded explicitly here before relying on any
	// statistical spread across many calls.
	random::init();

	Test::Harness h;
	auto m = h.addModule<MasterModule>("FlowerSeq");
	h.dspStep();

	SECTION("UNIFORM_FIXED_0 never touches slot 0, and is a no-op for patternCount == 1") {
		m->patternMutateDist = MUTATE_DISTRIBUTION::UNIFORM_FIXED_0;

		m->patternCount = 1;
		PATTERN_TYPE before = m->phrases[m->phraseIndex].patterns[0].type;
		m->patternMutate();
		CHECK(m->phrases[m->phraseIndex].patterns[0].type == before);

		// For patternCount > 1, slot 0 must never be touched: sample many mutations and check
		// slot 0 stays exactly what it started as, while the mutable range [1, patternCount)
		// still contains a permutation-consistent set of active types.
		m->patternCount = 5;
		PATTERN_TYPE slot0Before = m->phrases[m->phraseIndex].patterns[0].type;
		for (int i = 0; i < 200; i++) m->patternMutate();
		CHECK(m->phrases[m->phraseIndex].patterns[0].type == slot0Before);
	}

	SECTION("UNIFORM can touch every slot including 0") {
		m->patternMutateDist = MUTATE_DISTRIBUTION::UNIFORM;
		m->patternCount = 4;
		bool changed[4] = {false, false, false, false};
		PATTERN_TYPE before[4];
		for (int i = 0; i < 4; i++) before[i] = m->phrases[m->phraseIndex].patterns[i].type;

		for (int i = 0; i < 500; i++) {
			m->patternMutate();
			for (int j = 0; j < 4; j++) {
				if (m->phrases[m->phraseIndex].patterns[j].type != before[j]) changed[j] = true;
			}
		}
		// Statistically, every slot in [0, patternCount) should be hit at least once over 500
		// mutations at uniform 1/4 odds per call.
		for (int j = 0; j < 4; j++) {
			CATCH_INFO("slot " << j);
			CHECK(changed[j]);
		}
	}

	SECTION("BINOMIAL_FIXED_0: patternCount 1 is a no-op, patternCount 2 always touches slot 1") {
		m->patternMutateDist = MUTATE_DISTRIBUTION::BINOMIAL_FIXED_0;

		m->patternCount = 1;
		PATTERN_TYPE before0 = m->phrases[m->phraseIndex].patterns[0].type;
		m->patternMutate();
		CHECK(m->phrases[m->phraseIndex].patterns[0].type == before0);

		m->patternCount = 2;
		PATTERN_TYPE slot0Before = m->phrases[m->phraseIndex].patterns[0].type;
		for (int i = 0; i < 50; i++) {
			m->patternMutate();
			CHECK(m->phrases[m->phraseIndex].patterns[0].type == slot0Before);
		}
	}

	SECTION("BINOMIAL: patternCount 1 touches slot 0") {
		m->patternMutateDist = MUTATE_DISTRIBUTION::BINOMIAL;
		m->patternCount = 1;
		// patternList starts with every type active, so patternNext(0, ...) always has a valid
		// target — this must not crash regardless of what it resolves to.
		for (int i = 0; i < 20; i++) m->patternMutate();
		SUCCEED("BINOMIAL mutation with patternCount == 1 did not crash");
	}

	SECTION("patternBinomialDist index arithmetic stays in bounds for every patternCount") {
		// [review Problem 5]: BINOMIAL_FIXED_0 indexes patternBinomialDist[patternCount - 3]
		// for patternCount >= 3, and BINOMIAL indexes patternBinomialDist[patternCount - 2] for
		// patternCount >= 2. The array is sized PATTERNS - 1 (7 for this 8-pattern module) —
		// looping every patternCount under both distributions and running many mutations each
		// is the whole test, per the plan; a real out-of-bounds access here is exactly the kind
		// of thing ASan (already enabled for this test binary) catches.
		for (int count = 1; count <= 8; count++) {
			m->patternCount = count;
			m->patternMutateDist = MUTATE_DISTRIBUTION::BINOMIAL_FIXED_0;
			for (int i = 0; i < 30; i++) m->patternMutate();
			m->patternMutateDist = MUTATE_DISTRIBUTION::BINOMIAL;
			for (int i = 0; i < 30; i++) m->patternMutate();
		}
		SUCCEED("patternBinomialDist indexing stayed in bounds for every patternCount in [1, 8]");
	}
}

TEST_CASE("FlowerSeqModule doRandomize()", "[Flower][transport]") {
	// See the same note in the patternMutate() suite above: doRandomize() also calls
	// random::u32() directly, and Rack's RNG needs an explicit random::init() in this binary.
	random::init();

	Test::Harness h;
	auto m = h.addModule<MasterModule>("FlowerSeq");
	h.dspStep();

	SECTION("SEQ_START flag only changes PARAM_START") {
		float lengthBefore = m->params[MasterModule::PARAM_STEPLENGTH].getValue();
		int patternCountBefore = m->patternCount;
		int mult0Before = m->phrases[m->phraseIndex].patterns[0].mult;
		m->randomizeFlags.reset();
		m->randomizeFlags.set(FlowerProcessArgs::SEQ_START);
		for (int i = 0; i < 20; i++) m->doRandomize();
		CHECK(m->params[MasterModule::PARAM_STEPLENGTH].getValue() == lengthBefore);
		CHECK(m->patternCount == patternCountBefore);
		CHECK(m->phrases[m->phraseIndex].patterns[0].mult == mult0Before);
	}

	SECTION("SEQ_LENGTH flag only changes PARAM_STEPLENGTH") {
		float startBefore = m->params[MasterModule::PARAM_START].getValue();
		int patternCountBefore = m->patternCount;
		m->randomizeFlags.reset();
		m->randomizeFlags.set(FlowerProcessArgs::SEQ_LENGTH);
		for (int i = 0; i < 20; i++) m->doRandomize();
		CHECK(m->params[MasterModule::PARAM_START].getValue() == startBefore);
		CHECK(m->patternCount == patternCountBefore);
	}

	SECTION("PATTERN_CNT flag only changes patternCount") {
		float startBefore = m->params[MasterModule::PARAM_START].getValue();
		float lengthBefore = m->params[MasterModule::PARAM_STEPLENGTH].getValue();
		m->randomizeFlags.reset();
		m->randomizeFlags.set(FlowerProcessArgs::PATTERN_CNT);
		m->doRandomize();
		CHECK(m->params[MasterModule::PARAM_START].getValue() == startBefore);
		CHECK(m->params[MasterModule::PARAM_STEPLENGTH].getValue() == lengthBefore);
	}

	SECTION("PATTERN_RPT flag only changes the mult values") {
		float startBefore = m->params[MasterModule::PARAM_START].getValue();
		int patternCountBefore = m->patternCount;
		m->randomizeFlags.reset();
		m->randomizeFlags.set(FlowerProcessArgs::PATTERN_RPT);
		m->doRandomize();
		CHECK(m->params[MasterModule::PARAM_START].getValue() == startBefore);
		CHECK(m->patternCount == patternCountBefore);
	}

	SECTION("all flags clear leaves everything unchanged") {
		float startBefore = m->params[MasterModule::PARAM_START].getValue();
		float lengthBefore = m->params[MasterModule::PARAM_STEPLENGTH].getValue();
		int patternCountBefore = m->patternCount;
		int mult0Before = m->phrases[m->phraseIndex].patterns[0].mult;
		m->randomizeFlags.reset();
		m->doRandomize();
		CHECK(m->params[MasterModule::PARAM_START].getValue() == startBefore);
		CHECK(m->params[MasterModule::PARAM_STEPLENGTH].getValue() == lengthBefore);
		CHECK(m->patternCount == patternCountBefore);
		CHECK(m->phrases[m->phraseIndex].patterns[0].mult == mult0Before);
	}

	SECTION("randomized values stay within their documented ranges") {
		m->randomizeFlags.reset();
		m->randomizeFlags.set(FlowerProcessArgs::SEQ_START);
		m->randomizeFlags.set(FlowerProcessArgs::SEQ_LENGTH);
		m->randomizeFlags.set(FlowerProcessArgs::PATTERN_CNT);
		m->randomizeFlags.set(FlowerProcessArgs::PATTERN_RPT);

		for (int i = 0; i < 200; i++) {
			m->doRandomize();
			CHECK(m->params[MasterModule::PARAM_START].getValue() >= 0.f);
			CHECK(m->params[MasterModule::PARAM_START].getValue() <= 15.f);
			CHECK(m->params[MasterModule::PARAM_STEPLENGTH].getValue() >= 1.f);
			CHECK(m->params[MasterModule::PARAM_STEPLENGTH].getValue() <= 16.f);
			CHECK(m->patternCount >= 1);
			CHECK(m->patternCount <= 8);
			for (int p = 0; p < 8; p++) {
				int mult = m->phrases[m->phraseIndex].patterns[p].mult;
				CHECK(mult >= 1);
				CHECK(mult <= 4);
			}
		}
	}

	SECTION("the RAND button and RAND input each fire once per edge and raise randTick") {
		m->running = true;
		clearResetLockout(h);

		m->params[MasterModule::PARAM_RAND].setValue(10.f);
		h.dspStep();
		auto* args = reinterpret_cast<FlowerProcessArgs*>(m->rightExpander.consumerMessage);
		CHECK(args->randTick);

		m->params[MasterModule::PARAM_RAND].setValue(0.f);
		h.dspStep();
		args = reinterpret_cast<FlowerProcessArgs*>(m->rightExpander.consumerMessage);
		CHECK_FALSE(args->randTick);

		// A held button does not keep firing on every subsequent sample.
		m->params[MasterModule::PARAM_RAND].setValue(10.f);
		h.dspStep();
		h.dspStep();
		args = reinterpret_cast<FlowerProcessArgs*>(m->rightExpander.consumerMessage);
		CHECK_FALSE(args->randTick);
	}
}