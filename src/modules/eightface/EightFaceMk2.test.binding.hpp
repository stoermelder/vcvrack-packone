// Binding and slot commands: bind/unbind, save/clear, copy/paste.

TEST_CASE("bindModule() adds one entry; binding the same module twice is a no-op", "[EightFaceMk2][binding]") {
	Test::ModuleScaffold<EightFaceMk2Module<8>> mods{createEightFaceMk2Module};
	EightFaceMk2Module<8>* m = mods.create("EightFaceMk2");
	EightFaceMk2Module<8>* boundM = mods.create("EightFaceMk2");
	EightFaceMk2Widget<8>* boundMw = Test::createWidget<EightFaceMk2Widget<8>>(boundM);
	Test::registerModule(boundM, boundMw);

	std::string warn1 = m->bindModule(boundM);
	REQUIRE(m->boundModules.size() == 1);
	REQUIRE(m->boundModules[0]->moduleId == boundM->id);
	REQUIRE(m->boundModules[0]->pluginSlug == boundM->model->plugin->slug);
	REQUIRE(m->boundModules[0]->modelSlug == boundM->model->slug);
	// EightFaceMk2Module's own toJson() is well under the 400 kB warning threshold.
	REQUIRE(warn1.empty());

	// bindModule() (EightFaceMk2.cpp:490) scans boundModules for a matching moduleId first --
	// binding an already-bound module must not append a second entry.
	std::string warn2 = m->bindModule(boundM);
	REQUIRE(m->boundModules.size() == 1);
	REQUIRE(warn2.empty());

	Test::unregisterModule(boundM, boundMw);
}

TEST_CASE("bindModule(nullptr) is a safe no-op", "[EightFaceMk2][binding]") {
	// EightFaceMk2.cpp:489: "if (!m) return std::string();" -- guards bindModuleExpander() calling
	// through with no expander present.
	Test::ModuleScaffold<EightFaceMk2Module<8>> mods{createEightFaceMk2Module};
	EightFaceMk2Module<8>* m = mods.create("EightFaceMk2");

	std::string warn = m->bindModule(nullptr);
	REQUIRE(warn.empty());
	REQUIRE(m->boundModules.empty());
}

TEST_CASE("bindModuleExpander() binds the module on the left", "[EightFaceMk2][binding]") {
	// Manual: "Bind module (left)". bindModuleExpander() (EightFaceMk2.cpp:514-519) reads
	// Module::leftExpander directly rather than through the rack scene, so wiring it is a direct
	// field assignment -- same pattern as IntermixEnv.test.cpp:139-140 for rightExpander.
	Test::ModuleScaffold<EightFaceMk2Module<8>> mods{createEightFaceMk2Module};
	EightFaceMk2Module<8>* m = mods.create("EightFaceMk2");
	EightFaceMk2Module<8>* leftM = mods.create("EightFaceMk2");
	EightFaceMk2Widget<8>* leftMw = Test::createWidget<EightFaceMk2Widget<8>>(leftM);
	Test::registerModule(leftM, leftMw);

	m->leftExpander.moduleId = leftM->id;
	m->leftExpander.module = leftM;

	std::string warn = m->bindModuleExpander();
	REQUIRE(warn.empty());
	REQUIRE(m->boundModules.size() == 1);
	REQUIRE(m->boundModules[0]->moduleId == leftM->id);

	Test::unregisterModule(leftM, leftMw);
}

TEST_CASE("bindModuleExpander() with no module on the left is a safe no-op", "[EightFaceMk2][binding]") {
	// EightFaceMk2.cpp:516: "if (exp->moduleId < 0) return std::string();" -- the default,
	// nothing-connected state (Module::Expander::moduleId defaults to -1).
	Test::ModuleScaffold<EightFaceMk2Module<8>> mods{createEightFaceMk2Module};
	EightFaceMk2Module<8>* m = mods.create("EightFaceMk2");

	REQUIRE(m->leftExpander.moduleId == -1);
	std::string warn = m->bindModuleExpander();
	REQUIRE(warn.empty());
	REQUIRE(m->boundModules.empty());
}

TEST_CASE("unbindModule() removes the entry and its JSON from every slot, without leaking", "[EightFaceMk2][binding]") {
	// unbindModule() (EightFaceMk2.cpp:521-542) erases each slot's matching preset entry with
	// "slot->preset->erase(it)" and no json_decref(*it) first. Run under ASan (the test binary
	// already links it): if a leak regressed, this is the test that would need to catch it, even
	// though a leak alone will not fail a REQUIRE -- ASan's leak detector at process exit is what
	// actually catches a missing decref, not an assertion in this test body.
	Test::ModuleScaffold<EightFaceMk2Module<8>> mods{createEightFaceMk2Module};
	EightFaceMk2Module<8>* m = mods.create("EightFaceMk2");
	EightFaceMk2Module<8>* boundM = mods.create("EightFaceMk2");
	EightFaceMk2Widget<8>* boundMw = Test::createWidget<EightFaceMk2Widget<8>>(boundM);
	Test::registerModule(boundM, boundMw);

	m->bindModule(boundM);
	REQUIRE(m->boundModules.size() == 1);
	EightFaceMk2Module<8>::BoundModule* b = m->boundModules[0];

	// Two slots, both containing boundM's JSON -- unbindModule() must clear both.
	m->process(Test::makeProcessArgs(0));
	m->presetSave(0);
	m->presetSave(2);
	REQUIRE(m->presetSlotUsed[0] == true);
	REQUIRE(m->presetSlotUsed[2] == true);
	REQUIRE(m->EightFaceMk2Base<8>::preset[0].size() == 1);
	REQUIRE(m->EightFaceMk2Base<8>::preset[2].size() == 1);

	m->unbindModule(b);

	REQUIRE(m->boundModules.empty());
	REQUIRE(m->EightFaceMk2Base<8>::preset[0].empty());
	REQUIRE(m->EightFaceMk2Base<8>::preset[2].empty());

	Test::unregisterModule(boundM, boundMw);
}

TEST_CASE("unbindModule() clears presetSlotUsed when a slot becomes empty", "[EightFaceMk2][binding]") {
	// EightFaceMk2.cpp:533: "*(slot->presetSlotUsed) = slot->preset->size() > 0;" -- a slot whose
	// only entry was the unbound module must go back to unused, not stay marked used with an
	// empty preset vector (which the sequencing/write-mode code treats as a real, loadable slot).
	Test::ModuleScaffold<EightFaceMk2Module<8>> mods{createEightFaceMk2Module};
	EightFaceMk2Module<8>* m = mods.create("EightFaceMk2");
	EightFaceMk2Module<8>* boundM = mods.create("EightFaceMk2");
	EightFaceMk2Widget<8>* boundMw = Test::createWidget<EightFaceMk2Widget<8>>(boundM);
	Test::registerModule(boundM, boundMw);

	m->bindModule(boundM);
	EightFaceMk2Module<8>::BoundModule* b = m->boundModules[0];

	m->process(Test::makeProcessArgs(0));
	m->presetSave(0);
	REQUIRE(m->presetSlotUsed[0] == true);

	m->unbindModule(b);
	REQUIRE(m->presetSlotUsed[0] == false);

	Test::unregisterModule(boundM, boundMw);
}

TEST_CASE("A bound module deleted from the rack is skipped, not dereferenced", "[EightFaceMk2][binding]") {
	// Review #14 -- BoundModule::getModuleWidget() (EightFaceMk2.cpp:113) resolves through
	// APP->scene->rack->getModule(moduleId) every time, so a bound module removed from the rack
	// without 8Face being told (no unbindModule() call) leaves a BoundModule entry whose widget
	// resolution returns NULL. Every call site must check for that and skip, not dereference.
	// Simulated by unregistering the widget while leaving the BoundModule entry in place -- exactly
	// what "deleted from the rack, 8Face never notified" looks like from 8Face's side. Built by
	// hand rather than via DispatchFixture: the fixture's destructor unconditionally unregisters
	// its bound widget, which would double-unregister the one this test intentionally removes
	// early -- Test::unregisterModule() is not idempotent (RackWidget::removeModule() disconnects
	// cables etc. on a widget that is no longer parented, which crashes).
	Test::Harness h{Test::UiMode::UiPresent};
	EightFaceMk2Module<8>* m = h.addModule<EightFaceMk2Module<8>>(createEightFaceMk2Module);
	h.addWidget<EightFaceMk2Widget<8>>(m);
	m->dispatch.guiSafeMode = GUISAFEMODE::GUI_WITH_LOCK;

	EightFaceMk2Module<8>* boundM = h.addModule<EightFaceMk2Module<8>>(createEightFaceMk2Module);
	EightFaceMk2Widget<8>* boundMw = Test::createWidget<EightFaceMk2Widget<8>>(boundM);
	Test::registerModule(boundM, boundMw);
	m->bindModule(boundM);
	REQUIRE(m->boundModules.size() == 1);

	h.dspStep();
	m->presetSave(0);
	REQUIRE(m->presetSlotUsed[0] == true);
	REQUIRE(m->EightFaceMk2Base<8>::preset[0].size() == 1);

	// The bound widget is gone: getModuleWidget() now resolves to NULL for it. Unregistered here,
	// once, for the rest of the test -- boundM stays alive (owned by the harness) so its id still
	// matches the BoundModule entry, but APP->scene->rack->getModule(boundM->id) now returns NULL.
	Test::unregisterModule(boundM, boundMw);

	SECTION("presetLoad()/applyPreset() skips it") {
		m->presetLoad(0, false, true);
		h.uiFrame();
		// No crash reaching here is the primary assertion. dispatch.pendingGuiTasks() == 0 confirms
		// the task ran (and returned) rather than the test merely not having reached the drain.
		REQUIRE(m->dispatch.pendingGuiTasks() == 0);
	}

	SECTION("presetSave() skips it") {
		m->presetSave(1);
		// The slot is marked used (presetSave always sets this) but gains no entry for the
		// now-unresolvable bound module -- EightFaceMk2.cpp:648's "if (!mw) continue;".
		REQUIRE(m->presetSlotUsed[1] == true);
		REQUIRE(m->EightFaceMk2Base<8>::preset[1].empty());
	}

	SECTION("presetRandomize() skips it") {
		// No crash reaching here is the assertion -- randomizeAction() is never called against a
		// NULL widget.
		m->presetRandomize(1);
	}
}

TEST_CASE("presetSave captures every bound module; presetClear frees all and clears the label", "[EightFaceMk2][binding]") {
	Test::ModuleScaffold<EightFaceMk2Module<8>> mods{createEightFaceMk2Module};
	EightFaceMk2Module<8>* m = mods.create("EightFaceMk2");

	EightFaceMk2Module<8>* boundA = mods.create("EightFaceMk2");
	EightFaceMk2Widget<8>* boundAw = Test::createWidget<EightFaceMk2Widget<8>>(boundA);
	Test::registerModule(boundA, boundAw);
	EightFaceMk2Module<8>* boundB = mods.create("EightFaceMk2");
	EightFaceMk2Widget<8>* boundBw = Test::createWidget<EightFaceMk2Widget<8>>(boundB);
	Test::registerModule(boundB, boundBw);

	m->bindModule(boundA);
	m->bindModule(boundB);
	REQUIRE(m->boundModules.size() == 2);

	m->process(Test::makeProcessArgs(0));
	m->textLabel[0] = "Before clear";
	m->presetSave(0);

	REQUIRE(m->presetSlotUsed[0] == true);
	REQUIRE(m->EightFaceMk2Base<8>::preset[0].size() == 2);

	m->presetClear(0);

	REQUIRE(m->presetSlotUsed[0] == false);
	REQUIRE(m->EightFaceMk2Base<8>::preset[0].empty());
	REQUIRE(m->textLabel[0] == "");

	Test::unregisterModule(boundA, boundAw);
	Test::unregisterModule(boundB, boundBw);
}

TEST_CASE("Copy then paste duplicates a slot with a deep copy, not an aliased pointer", "[EightFaceMk2][binding]") {
	// presetCopyPaste() (EightFaceMk2.cpp:684-703) uses json_deep_copy(), not the source json_t*
	// directly -- clearing or mutating one slot must never affect the other.
	Test::ModuleScaffold<EightFaceMk2Module<8>> mods{createEightFaceMk2Module};
	EightFaceMk2Module<8>* m = mods.create("EightFaceMk2");
	EightFaceMk2Module<8>* boundM = mods.create("EightFaceMk2");
	EightFaceMk2Widget<8>* boundMw = Test::createWidget<EightFaceMk2Widget<8>>(boundM);
	Test::registerModule(boundM, boundMw);
	m->bindModule(boundM);

	m->process(Test::makeProcessArgs(0));
	m->presetSave(0);
	REQUIRE(m->presetSlotUsed[0] == true);
	json_t* sourceJ = m->EightFaceMk2Base<8>::preset[0][0];

	m->faceSlotCmd(SLOT_CMD::COPY, 0);
	m->faceSlotCmd(SLOT_CMD::PASTE, 3);

	REQUIRE(m->presetSlotUsed[3] == true);
	REQUIRE(m->EightFaceMk2Base<8>::preset[3].size() == 1);
	json_t* pastedJ = m->EightFaceMk2Base<8>::preset[3][0];

	// Different json_t* (not aliased)...
	REQUIRE(pastedJ != sourceJ);
	// ...but equal content.
	REQUIRE(json_equal(pastedJ, sourceJ) == 1);

	// Clearing the pasted slot must not touch the source's json_t*.
	m->presetClear(3);
	REQUIRE(json_typeof(sourceJ) == JSON_OBJECT);

	Test::unregisterModule(boundM, boundMw);
}

TEST_CASE("Paste with nothing copied (presetCopy == -1) is a safe no-op", "[EightFaceMk2][binding][.known-crash]") {
	// KNOWN CRASHING -- confirmed live, not yet fixed (review #7).
	// Tagged [.known-crash] (leading dot) rather than [!shouldfail]: this reproduces a genuine
	// SIGSEGV (confirmed under ASan), not a REQUIRE failure, and a crash aborts the whole test
	// binary -- it cannot be left in the default [EightFaceMk2] run without taking every other
	// test down with it. The leading dot hides it from a plain `testrun`/`testrun-one` pass; run it
	// deliberately with `--exclude-tag ""`-style explicit selection (or by name) once the fix
	// lands, then drop the tag so it rejoins the default run as a normal regression test.
	//
	// presetCopyPaste(-1, i) calls expSlot(-1). expSlot()'s only guard is "index >= presetTotal"
	// (EightFaceMk2.cpp:231), which a negative index passes; integer division/modulo then compute
	// n=0, index%NUM_PRESETS=-1, landing on faceSlot(-1) -> &BASE::slot[-1] -- an out-of-bounds
	// read on the fixed-size slot[] array. presetCopy defaults to -1 (EightFaceMk2.cpp:74) and
	// PASTE_PREVIEW/COPY never advance it past -1 unless a slot with content was actually copied,
	// so a paste with nothing copied is directly reachable from the slot's own context menu
	// (EightFaceMk2Base.hpp's PasteItem, disabled only via a `step()` check, not prevented at the
	// call site).
	Test::ModuleScaffold<EightFaceMk2Module<8>> mods{createEightFaceMk2Module};
	EightFaceMk2Module<8>* m = mods.create("EightFaceMk2");
	m->process(Test::makeProcessArgs(0));

	REQUIRE(m->presetCopy == -1);
	// This line crashes today (SIGSEGV in presetCopyPaste(-1, 2) via expSlot(-1)). No crash
	// reaching the assertions below is the fix's proof.
	m->faceSlotCmd(SLOT_CMD::PASTE, 2);

	// Nothing was pasted -- slot 2 stays exactly as a fresh module leaves it.
	REQUIRE(m->presetSlotUsed[2] == false);
	REQUIRE(m->EightFaceMk2Base<8>::preset[2].empty());
}
