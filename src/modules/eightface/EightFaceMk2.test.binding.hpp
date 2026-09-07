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

TEST_CASE("Paste with nothing copied (presetCopy == -1) is a safe no-op", "[EightFaceMk2][binding]") {
	// FIXED (review #7). presetCopyPaste(-1, i) used to call expSlot(-1) unguarded, reading
	// &BASE::slot[-1] out of bounds. Fixed by rejecting source < 0 up front.
	Test::ModuleScaffold<EightFaceMk2Module<8>> mods{createEightFaceMk2Module};
	EightFaceMk2Module<8>* m = mods.create("EightFaceMk2");
	m->process(Test::makeProcessArgs(0));

	REQUIRE(m->presetCopy == -1);
	m->faceSlotCmd(SLOT_CMD::PASTE, 2);

	// Nothing was pasted -- slot 2 stays exactly as a fresh module leaves it.
	REQUIRE(m->presetSlotUsed[2] == false);
	REQUIRE(m->EightFaceMk2Base<8>::preset[2].empty());
}

// Fixture for SHIFT_BACK/SHIFT_FRONT: one bound module, plus a helper to snapshot a slot with a
// distinctive label so a shift's source/destination is identifiable by more than just
// presetSlotUsed. Labels are asserted alongside preset content because presetShiftBack/Front move
// *expSlotLabel(...) by hand (EightFaceMk2.cpp:714/728), a separate line from presetCopyPaste()'s
// own json copy -- a label left behind would be silently wrong without a mismatch anywhere else.
struct ShiftFixture {
	Test::ModuleScaffold<EightFaceMk2Module<8>> mods{createEightFaceMk2Module};
	EightFaceMk2Module<8>* m;
	EightFaceMk2Module<8>* boundM;
	EightFaceMk2Widget<8>* boundMw;

	ShiftFixture() {
		m = mods.create("EightFaceMk2");
		boundM = mods.create("EightFaceMk2");
		boundMw = Test::createWidget<EightFaceMk2Widget<8>>(boundM);
		Test::registerModule(boundM, boundMw);
		m->bindModule(boundM);
		m->process(Test::makeProcessArgs(0));
	}

	~ShiftFixture() {
		Test::unregisterModule(boundM, boundMw);
	}

	// presetSave() always captures every bound module's current toJson() -- boxColor is a real
	// field EightFaceMk2Module's own dataToJson()/dataFromJson() round-trips, so a distinct color
	// per save makes each slot's captured payload itself distinguishable, on top of the label.
	void save(int slot, const std::string& label, const std::string& colorHex) {
		NVGcolor prevColor = boundM->boxColor;
		boundM->boxColor = color::fromHexString(colorHex);
		m->presetSave(slot);
		boundM->boxColor = prevColor;
		m->textLabel[slot] = label;
	}

	std::string colorOf(int slot) {
		REQUIRE(m->EightFaceMk2Base<8>::preset[slot].size() == 1);
		// presetSave() stores mw->toJson()'s output, which wraps Module::dataToJson() under a
		// "data" key (Rack/src/engine/Module.cpp's Module::toJson()) -- boxColor lives there, not
		// at the top level.
		json_t* dataJ = json_object_get(m->EightFaceMk2Base<8>::preset[slot][0], "data");
		json_t* colorJ = dataJ ? json_object_get(dataJ, "boxColor") : nullptr;
		return colorJ ? json_string_value(colorJ) : "";
	}
};

TEST_CASE("SHIFT_BACK moves slots down from the initiating slot; the last slot is dropped", "[EightFaceMk2][binding]") {
	// Manual: "Shift back... Moves all snapshots one slot backward, beginning from the initiating
	// slot. If the last slot is used it gets deleted, also the number of currently active slots is
	// unaffected." presetShiftBack(p) (EightFaceMk2.cpp:709-721) walks from presetTotal-2 down to p,
	// copying each slot i's content to i+1, so the highest slot (presetTotal-1) is overwritten
	// first and its original content is what's actually dropped; p itself is cleared last, once
	// nothing else reads it.
	ShiftFixture f;
	f.save(1, "B", "#0000b0");
	f.save(2, "C", "#00c000");
	// Slot 7 (presetTotal-1) holds content that SHIFT_BACK from slot 1 must drop.
	f.save(7, "Z", "#f0f0f0");
	REQUIRE(f.m->presetCount == 8);

	f.m->faceSlotCmd(SLOT_CMD::SHIFT_BACK, 1);

	// Slot 0 is below p -- untouched (never populated here, stays empty).
	REQUIRE(f.m->presetSlotUsed[0] == false);
	// The initiating slot is cleared once everything has shifted away from it.
	REQUIRE(f.m->presetSlotUsed[1] == false);
	REQUIRE(f.m->textLabel[1] == "");
	// Old slot 1's content ("B") is now in slot 2.
	REQUIRE(f.m->presetSlotUsed[2] == true);
	REQUIRE(f.m->textLabel[2] == "B");
	REQUIRE(f.colorOf(2) == "#0000b0");
	// Old slot 2's content ("C") is now in slot 3.
	REQUIRE(f.m->presetSlotUsed[3] == true);
	REQUIRE(f.m->textLabel[3] == "C");
	REQUIRE(f.colorOf(3) == "#00c000");
	// Slots 4-6 were empty and stay empty.
	for (int i = 4; i <= 6; i++) REQUIRE(f.m->presetSlotUsed[i] == false);
	// Slot 7's original content ("Z") is gone -- overwritten by slot 6 (empty) shifting into it,
	// per the manual's "last slot... gets deleted".
	REQUIRE(f.m->presetSlotUsed[7] == false);
	REQUIRE(f.m->textLabel[7] == "");
	// "the number of currently active slots is unaffected"
	REQUIRE(f.m->presetCount == 8);
}

TEST_CASE("SHIFT_FRONT moves slots up; the first slot is dropped", "[EightFaceMk2][binding]") {
	// Manual: "Shift front... Moves all snapshots one slot forward, beginning from the initiating
	// slot. If the first slot is used it gets deleted." presetShiftFront(p) (EightFaceMk2.cpp:
	// 723-735) walks from 1 up to p, copying each slot i's content to i-1 -- so slot 0's original
	// content is overwritten first (and is what's dropped); p itself is cleared last.
	ShiftFixture f;
	// Slot 0 holds content that SHIFT_FRONT ending at slot 2 must drop.
	f.save(0, "A", "#a00000");
	f.save(1, "B", "#0000b0");
	f.save(2, "C", "#00c000");

	f.m->faceSlotCmd(SLOT_CMD::SHIFT_FRONT, 2);

	// Old slot 1's content ("B") is now in slot 0, overwriting "A".
	REQUIRE(f.m->presetSlotUsed[0] == true);
	REQUIRE(f.m->textLabel[0] == "B");
	REQUIRE(f.colorOf(0) == "#0000b0");
	// Old slot 2's content ("C") is now in slot 1.
	REQUIRE(f.m->presetSlotUsed[1] == true);
	REQUIRE(f.m->textLabel[1] == "C");
	REQUIRE(f.colorOf(1) == "#00c000");
	// The initiating slot is cleared once everything has shifted away from it.
	REQUIRE(f.m->presetSlotUsed[2] == false);
	REQUIRE(f.m->textLabel[2] == "");
	// Slot 0's original content ("A") is gone.
	for (int i = 3; i < 8; i++) REQUIRE(f.m->presetSlotUsed[i] == false);
}

TEST_CASE("SHIFT_BACK/SHIFT_FRONT at the edge (p == 0 / p == presetTotal-1) touch only one slot", "[EightFaceMk2][binding]") {
	// The loop bounds (i from presetTotal-2 down to p for BACK; i from 1 up to p for FRONT) admit
	// an empty range at the extremes -- SHIFT_BACK from the last slot and SHIFT_FRONT from the
	// first slot degenerate to "clear just this slot", which must not crash and must not disturb
	// any other slot.
	ShiftFixture f;
	f.save(0, "A", "#a00000");
	f.save(7, "Z", "#f0f0f0");

	SECTION("SHIFT_BACK from the last slot only clears it") {
		f.m->faceSlotCmd(SLOT_CMD::SHIFT_BACK, 7);
		REQUIRE(f.m->presetSlotUsed[7] == false);
		REQUIRE(f.m->presetSlotUsed[0] == true);
		REQUIRE(f.m->textLabel[0] == "A");
	}

	SECTION("SHIFT_FRONT from the first slot only clears it") {
		f.m->faceSlotCmd(SLOT_CMD::SHIFT_FRONT, 0);
		REQUIRE(f.m->presetSlotUsed[0] == false);
		REQUIRE(f.m->presetSlotUsed[7] == true);
		REQUIRE(f.m->textLabel[7] == "Z");
	}
}

TEST_CASE("bindModule() returns a warning string above the ~400 kB threshold", "[EightFaceMk2][binding]") {
	// EightFaceMk2.cpp:505: "if (size > 400000)". bindModule()'s own toJson()/json_dumps() measures
	// the bound module's *own* serialized size at bind time, not any particular preset slot -- so
	// inflating the bound module's own textLabel[] (part of EightFaceMk2Base::dataToJson(), which
	// ModuleWidget::toJson() includes) is enough to cross the threshold without a dedicated mock
	// module.
	//
	// FIXED (review): EightFaceMk2.cpp:506 used to pass b->moduleName (a std::string, by value) as
	// the vararg matched against string::f's "%s" -- UB, since string::f(const char*, ...) is a
	// real C varargs function and a non-POD class cannot be passed through "...". Fixed by passing
	// .c_str(). Note the old code happened to still assert correctly here too (this exact UB
	// silently "worked" on this ABI/opt level) -- the fix is required by the standard regardless of
	// whether this test can observe the old behavior breaking.
	Test::ModuleScaffold<EightFaceMk2Module<8>> mods{createEightFaceMk2Module};
	EightFaceMk2Module<8>* m = mods.create("EightFaceMk2");
	EightFaceMk2Module<8>* boundM = mods.create("EightFaceMk2");
	EightFaceMk2Widget<8>* boundMw = Test::createWidget<EightFaceMk2Widget<8>>(boundM);
	Test::registerModule(boundM, boundMw);

	// Comfortably over 400,000 bytes of dumped JSON on its own -- one label already pushes the
	// whole dump (8 slots x label + surrounding JSON structure) past the threshold.
	boundM->textLabel[0] = std::string(500000, 'x');

	std::string warn = m->bindModule(boundM);
	std::string moduleName = m->boundModules[0]->moduleName;
	// Not re-computing the exact kb figure (that would just re-run json_dumps()) -- assert the
	// message names the bound module correctly and is otherwise well-formed text, not garbage.
	REQUIRE(warn.find("The preset size of " + moduleName + " is about ") == 0);
	REQUIRE(warn.find("kb, which might cause performance issues.") != std::string::npos);

	Test::unregisterModule(boundM, boundMw);
}
