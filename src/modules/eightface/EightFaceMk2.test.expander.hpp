// mk2-specific: +8 expander chaining.
//
// Before this file, zero tests exercised expander chaining at all: presetTotal, ctrlOffset,
// expSlot()'s (module, slot) mapping, the MAX_EXPANDERS cap, expanderCleanUp(), or +8's
// faceSlotCmd forwarding. All of that lives inside process()'s chain-walk
// (EightFaceMk2.cpp:253-279), so every test here must run at least one h.dspStep() after wiring
// the chain before presetTotal/N[] reflect it.

// Wires `count` EightFaceMk2Ex expanders to the right of `m` via the harness's connectChain (so
// onExpanderChange fires per link, as Rack does), then steps once so process() walks the chain
// and establishes presetTotal/N[]/ctrlOffset. Returns the expander modules in chain order.
static std::vector<EightFaceMk2ExModule<8>*> chainExpanders(Test::Harness& h, EightFaceMk2Module<8>* m, int count) {
	std::vector<EightFaceMk2ExModule<8>*> exps;
	std::vector<rack::Module*> chain{m};
	for (int i = 0; i < count; i++) {
		auto* ex = h.addModule<EightFaceMk2ExModule<8>>("EightFaceMk2Ex");
		h.addWidget<EightFaceMk2ExWidget<8>>(ex);
		exps.push_back(ex);
		chain.push_back(ex);
	}
	h.connectChain(chain);
	h.dspStep();
	return exps;
}

TEST_CASE("One +8 gives presetTotal == 16; slots 8-15 route to the expander", "[EightFaceMk2][expander]") {
	Test::Harness h{Test::UiMode::UiPresent};
	EightFaceMk2Module<8>* m = h.addModule<EightFaceMk2Module<8>>(createEightFaceMk2Module);
	h.addWidget<EightFaceMk2Widget<8>>(m);
	h.dspStep();
	REQUIRE(m->presetTotal == 8);

	auto exps = chainExpanders(h, m, 1);
	REQUIRE(m->presetTotal == 16);

	// Raise presetCount so presetLoad() (gated on the member, EightFaceMk2.cpp:608) accepts
	// indices past 8.
	m->presetSetCount(16);

	// Slot 8 on the controller must resolve to slot 0 of the expander -- expSlot() maps by
	// (index / NUM_PRESETS, index % NUM_PRESETS).
	EightFaceMk2Slot* slot8 = m->expSlot(8);
	REQUIRE(slot8 == exps[0]->faceSlot(0));
	EightFaceMk2Slot* slot15 = m->expSlot(15);
	REQUIRE(slot15 == exps[0]->faceSlot(7));

	// Saving through the controller at index 8 lands in the expander's own storage, not the
	// controller's slot[0].
	m->presetSave(8);
	REQUIRE(exps[0]->presetSlotUsed[0] == true);
	REQUIRE(m->presetSlotUsed[0] == false);
}

TEST_CASE("ctrlOffset is assigned per expander; expSlot() maps index to (module, slot)", "[EightFaceMk2][expander]") {
	Test::Harness h{Test::UiMode::UiPresent};
	EightFaceMk2Module<8>* m = h.addModule<EightFaceMk2Module<8>>(createEightFaceMk2Module);
	h.addWidget<EightFaceMk2Widget<8>>(m);
	h.dspStep();

	auto exps = chainExpanders(h, m, 3);
	REQUIRE(m->presetTotal == 32);

	// process()'s chain-walk assigns ctrlOffset = c, incrementing per link starting at 1 (c=0 is
	// the controller itself, EightFaceMk2.cpp:261-274).
	REQUIRE(exps[0]->ctrlOffset == 1);
	REQUIRE(exps[1]->ctrlOffset == 2);
	REQUIRE(exps[2]->ctrlOffset == 3);

	// Every index in an expander's 8-slot range maps to that expander, at the right local index.
	for (int e = 0; e < 3; e++) {
		for (int local = 0; local < 8; local++) {
			int index = (e + 1) * 8 + local;
			REQUIRE(m->expSlot(index) == exps[e]->faceSlot(local));
		}
	}

	// ctrlModuleId lets +8 resolve the controller from the GUI thread (faceSlotCmd) without a
	// stored back-pointer.
	for (auto* ex : exps) REQUIRE(ex->ctrlModuleId == m->id);
}

TEST_CASE("Chain stops at a foreign module and at MAX_EXPANDERS", "[EightFaceMk2][expander]") {
	SECTION("A non-EightFaceMk2Ex module on the right stops the chain") {
		Test::Harness h{Test::UiMode::UiPresent};
		EightFaceMk2Module<8>* m = h.addModule<EightFaceMk2Module<8>>(createEightFaceMk2Module);
		h.addWidget<EightFaceMk2Widget<8>>(m);
		h.dspStep();

		// A second EightFaceMk2 (not EightFaceMk2Ex) is a foreign model on the right --
		// process()'s walk checks "exp->model != modelEightFaceMk2Ex" and breaks
		// (EightFaceMk2.cpp:267).
		EightFaceMk2Module<8>* foreign = h.addModule<EightFaceMk2Module<8>>(createEightFaceMk2Module);
		h.addWidget<EightFaceMk2Widget<8>>(foreign);
		h.connectExpander(m, foreign);
		h.dspStep();

		REQUIRE(m->presetTotal == 8);
	}

	SECTION("The cap yields the documented 128 slots (15 expanders x 8 + controller's 8)") {
		Test::Harness h{Test::UiMode::UiPresent};
		EightFaceMk2Module<8>* m = h.addModule<EightFaceMk2Module<8>>(createEightFaceMk2Module);
		h.addWidget<EightFaceMk2Widget<8>>(m);
		h.dspStep();

		// One past MAX_EXPANDERS (15): the 16th +8 must not be counted.
		auto exps = chainExpanders(h, m, 16);
		REQUIRE(m->presetTotal == 128);
		REQUIRE(exps.size() == 16);

		// The 15th expander (index 14) is the last one the chain-walk assigns an offset to.
		REQUIRE(exps[14]->ctrlOffset == 15);
		// The 16th (index 15, one past the cap) is never reached by the walk -- its offset stays
		// at the default it's constructed with.
		REQUIRE(exps[15]->ctrlOffset == 0);
	}
}

TEST_CASE("Disconnecting an expander shrinks presetTotal without a stale N[] access", "[EightFaceMk2][expander]") {
	// Review #3/#4: expSlot()'s only guard is "index >= presetTotal" -- N[n] itself is never
	// null-checked, so a stale N[n] left over from before a disconnect is a crash risk. The
	// production path re-scans the whole chain every process() call (EightFaceMk2.cpp:253:
	// "if (moduleChangedFlag || ctrlMode != BASE::ctrlMode)"), and onExpanderChange sets
	// moduleChangedFlag via notifyModuleListeners -- so a real disconnectExpander() followed by
	// one dspStep() must never crash and must leave presetTotal/expSlot() consistent with the
	// shorter chain.
	Test::Harness h{Test::UiMode::UiPresent};
	EightFaceMk2Module<8>* m = h.addModule<EightFaceMk2Module<8>>(createEightFaceMk2Module);
	h.addWidget<EightFaceMk2Widget<8>>(m);
	h.dspStep();

	auto exps = chainExpanders(h, m, 2);
	REQUIRE(m->presetTotal == 24);
	m->presetSetCount(24);

	// Save something in the far expander so a stale reference would be observable, not just a
	// silent no-op.
	m->presetSave(16);
	REQUIRE(exps[1]->presetSlotUsed[0] == true);

	// Disconnect the second expander from the first -- the "hot-swap" case: the chain shortens
	// mid-rack, not at the controller.
	h.disconnectExpander(exps[0], Test::Harness::SIDE_RIGHT);
	h.dspStep();

	REQUIRE(m->presetTotal == 16);
	// No crash reaching here is the primary assertion for review #3/#4. Every index within the
	// new, shorter total must still resolve to a live slot.
	for (int i = 0; i < 16; i++) {
		REQUIRE(m->expSlot(i) != nullptr);
	}
	// Past the new presetTotal, expSlot() must return NULL rather than touching a stale N[2].
	REQUIRE(m->expSlot(16) == nullptr);
	REQUIRE(m->expSlot(23) == nullptr);
}

TEST_CASE("presetLoad() past a shrunk presetTotal is a safe no-op", "[EightFaceMk2][expander]") {
	// FIXED (review #3/#4). presetLoad() gated on the raw *member* presetCount, which can be stale
	// above presetTotal right after an expander disconnects -- expSlot(p) then returned NULL and
	// was dereferenced unchecked. Fixed by null-checking expSlot()'s result in presetLoad().
	Test::Harness h{Test::UiMode::UiPresent};
	EightFaceMk2Module<8>* m = h.addModule<EightFaceMk2Module<8>>(createEightFaceMk2Module);
	h.addWidget<EightFaceMk2Widget<8>>(m);
	h.dspStep();

	auto exps = chainExpanders(h, m, 2);
	REQUIRE(m->presetTotal == 24);
	m->presetSetCount(24);

	h.disconnectExpander(exps[0], Test::Harness::SIDE_RIGHT);
	h.dspStep();
	REQUIRE(m->presetTotal == 16);
	// presetCount (member) is still 24 here -- process() never writes it back, only its local copy
	// clamps.
	m->presetLoad(20, false, true);

	REQUIRE(m->preset != 20);
}

TEST_CASE("expanderCleanUp() wipes an expander that belonged to another 8FACE mk2", "[EightFaceMk2][expander]") {
	// v2.2.0 "Improved robustness for expander +8". An expander whose ctrlUniqueId doesn't match
	// the controller's (and isn't the legacy -2 sentinel, and isn't already claimed by this exact
	// controller's id) is treated as belonging to a different 8FACE mk2 -- its content must be
	// wiped via onReset() before being adopted, so slots don't leak content from the previous
	// owner. EightFaceMk2.cpp:270 calls this during the chain-walk whenever ctrlUniqueId mismatches.
	Test::Harness h{Test::UiMode::UiPresent};
	EightFaceMk2Module<8>* ownerA = h.addModule<EightFaceMk2Module<8>>(createEightFaceMk2Module);
	h.addWidget<EightFaceMk2Widget<8>>(ownerA);
	h.dspStep();

	auto exps = chainExpanders(h, ownerA, 1);
	EightFaceMk2ExModule<8>* ex = exps[0];
	REQUIRE(ex->ctrlUniqueId == ownerA->ctrlUniqueId);

	// Give the expander some content while owned by A, then simulate it having been claimed by a
	// different, already-initialized controller B: assign a distinct ctrlUniqueId and a
	// ctrlModuleId that isn't B's own id (mirroring "moved to another rack region, seen by B
	// before B's own scan clears it").
	ex->presetSlotUsed[0] = true;
	ex->textLabel[0] = "leftover from A";

	EightFaceMk2Module<8>* ownerB = h.addModule<EightFaceMk2Module<8>>(createEightFaceMk2Module);
	h.addWidget<EightFaceMk2Widget<8>>(ownerB);
	h.dspStep();

	// The test RNG (rack::random::uniform(), unseeded here) is deterministic and can hand
	// ctrlUniqueId the exact same value to both owners -- collapsing the very mismatch this test
	// depends on. Force them distinct so the test's premise holds regardless of what the RNG
	// produced.
	ownerB->ctrlUniqueId = ownerA->ctrlUniqueId + 1;

	// Move the expander from A's chain to B's chain.
	h.disconnectExpander(ownerA, Test::Harness::SIDE_RIGHT);
	h.connectExpander(ownerB, ex);
	h.dspStep();

	// B's scan sees ex->ctrlUniqueId == ownerA->ctrlUniqueId != ownerB->ctrlUniqueId, with
	// ctrlModuleId already set to ownerA's id (not -1, not B's) -- expanderCleanUp()'s condition
	// "(t->ctrlModuleId >= 0 && t->ctrlModuleId != Module::id)" fires, wiping it.
	REQUIRE(ex->presetSlotUsed[0] == false);
	REQUIRE(ex->textLabel[0] == "");
	REQUIRE(ex->ctrlUniqueId == ownerB->ctrlUniqueId);
	REQUIRE(ex->ctrlModuleId == ownerB->id);
}

TEST_CASE("A -2 (pre-uniqueId) expander is adopted rather than wiped", "[EightFaceMk2][expander]") {
	// The legacy branch at EightFaceMk2.cpp:739: ctrlUniqueId == -2 (the sentinel a patch from
	// before ctrlUniqueId existed loads with, per EightFaceMk2Base::dataFromJson) must be adopted
	// silently -- content surviving from a pre-2.2.0 patch must not be wiped just because it has
	// never had an owner id stamped on it yet.
	Test::Harness h{Test::UiMode::UiPresent};
	EightFaceMk2Module<8>* m = h.addModule<EightFaceMk2Module<8>>(createEightFaceMk2Module);
	h.addWidget<EightFaceMk2Widget<8>>(m);
	h.dspStep();

	auto* ex = h.addModule<EightFaceMk2ExModule<8>>("EightFaceMk2Ex");
	h.addWidget<EightFaceMk2ExWidget<8>>(ex);

	// EightFaceMk2ExModule's own constructor/onReset sets ctrlUniqueId = -1 (not -2), so the
	// legacy state is set up explicitly here, as if dataFromJson() had just loaded an old patch.
	ex->ctrlUniqueId = -2;
	ex->presetSlotUsed[0] = true;
	ex->textLabel[0] = "legacy content";

	h.connectExpander(m, ex);
	h.dspStep();

	REQUIRE(ex->presetSlotUsed[0] == true);
	REQUIRE(ex->textLabel[0] == "legacy content");
	// Still adopted into the chain -- ctrlUniqueId is stamped to the controller's, same as any
	// freshly-connected expander, just without the wipe.
	REQUIRE(ex->ctrlUniqueId == m->ctrlUniqueId);
	REQUIRE(m->presetTotal == 16);
}

TEST_CASE("+8's faceSlotCmd forwards to the controller with the offset applied", "[EightFaceMk2][expander]") {
	// EightFaceMk2Ex::faceSlotCmd() resolves the controller via
	// APP->scene->rack->getModule(ctrlModuleId), not a cached pointer -- but
	// Test::Harness::addWidget() only parents a widget under APP->scene, not APP->scene->rack
	// (see DispatchFixture's comment in EightFaceMk2.test.dispatch.hpp). So m's widget is
	// registered directly through Test::registerModule() here, the same way the binding and
	// dispatch suites register a widget that must resolve through the rack scene.
	Test::Harness h{Test::UiMode::UiPresent};
	EightFaceMk2Module<8>* m = h.addModule<EightFaceMk2Module<8>>(createEightFaceMk2Module);
	EightFaceMk2Widget<8>* mw = Test::createWidget<EightFaceMk2Widget<8>>(m);
	Test::registerModule(m, mw);
	h.dspStep();

	auto exps = chainExpanders(h, m, 1);
	EightFaceMk2ExModule<8>* ex = exps[0];
	REQUIRE(ex->ctrlOffset == 1);

	SECTION("SAVE at local index i lands at controller index i + ctrlOffset * NUM_PRESETS") {
		// faceSlotCmd(SAVE, 2) on the expander must behave like faceSlotCmd(SAVE, 10) on the
		// controller (EightFaceMk2Ex.cpp:87-96).
		ex->faceSlotCmd(SLOT_CMD::SAVE, 2);
		REQUIRE(m->presetSlotUsed[2] == false);
		REQUIRE(ex->presetSlotUsed[2] == true);
	}

	SECTION("CLEAR at local index i forwards to the same global index") {
		ex->faceSlotCmd(SLOT_CMD::SAVE, 3);
		REQUIRE(ex->presetSlotUsed[3] == true);
		ex->faceSlotCmd(SLOT_CMD::CLEAR, 3);
		REQUIRE(ex->presetSlotUsed[3] == false);
	}

	SECTION("Returns -1 when the controller is gone") {
		// EightFaceMk2Ex.cpp:90: "ModuleWidget* mw = APP->scene->rack->getModule(BASE::ctrlModuleId); if (!mw) return -1;"
		// -- the controller's widget must be resolvable via the rack scene, not a cached pointer.
		// Simulated the same way "A bound module deleted from the rack" is in the binding suite:
		// unregister the controller's widget while its module (and ex's ctrlModuleId) stay put.
		// Test::unregisterModule(m, mw) destroys mw (and removes m from the engine) -- there is
		// nothing to restore afterwards, so this section leaves m unregistered and skips the
		// shared teardown below rather than trying to resurrect a freed widget.
		REQUIRE(mw != nullptr);
		Test::unregisterModule(m, mw);
		mw = nullptr;

		int result = ex->faceSlotCmd(SLOT_CMD::SAVE, 0);
		REQUIRE(result == -1);
		// Nothing was saved anywhere -- the call was a safe no-op, not a crash and not a silent
		// save under the wrong module.
		REQUIRE(ex->presetSlotUsed[0] == false);
	}

	// mw was created and registered directly (not through h.addWidget()), so it must be torn down
	// the same way -- the harness's own teardown only knows about widgets it created itself. NULL
	// after the section above, which already destroyed it and left m unregistered.
	if (mw) Test::unregisterModule(m, mw);
}

TEST_CASE("presetCount is clamped to presetTotal", "[EightFaceMk2][expander]") {
	// Review #5. presetSetCount()/dataFromJson() can leave the member `presetCount` above
	// presetTotal (e.g. a patch saved with expanders attached, reloaded with fewer of them
	// present) -- process() must never use the raw member past the number of slots that actually
	// exist. EightFaceMk2.cpp:280: "int presetCount = std::min(this->presetCount, presetTotal);"
	// is the local, clamped value every CV/button branch actually reads.
	Test::Harness h{Test::UiMode::UiPresent};
	EightFaceMk2Module<8>* m = h.addModule<EightFaceMk2Module<8>>(createEightFaceMk2Module);
	h.addWidget<EightFaceMk2Widget<8>>(m);
	h.dspStep();

	// No expander: presetTotal == 8. Force the member above it, as a stale patch value would.
	m->presetCount = 16;
	m->slotCvMode = SLOTCVMODE::TRIG_FWD;
	m->preset = 0;
	for (int i = 0; i < 8; i++) m->presetSlotUsed[i] = true;

	h.connectInput(m, EightFaceMk2Module<8>::INPUT_CV, 10.f);
	h.dspStep();
	h.connectInput(m, EightFaceMk2Module<8>::INPUT_CV, 0.f);
	h.dspStep();
	// Priming pulse (first-ever SchmittTrigger transition, same pattern as SequencingFixture) --
	// throw away its result and pulse again to observe real wraparound behavior.
	int primed = m->preset;
	(void)primed;

	for (int i = 0; i < 10; i++) {
		h.connectInput(m, EightFaceMk2Module<8>::INPUT_CV, 10.f);
		h.dspStep();
		h.connectInput(m, EightFaceMk2Module<8>::INPUT_CV, 0.f);
		h.dspStep();
		// TRIG_FWD wraps at the clamped count (8), never advancing into the nonexistent 9-16
		// range that the raw (unclamped) member would allow.
		REQUIRE(m->preset >= 0);
		REQUIRE(m->preset < 8);
	}
}
