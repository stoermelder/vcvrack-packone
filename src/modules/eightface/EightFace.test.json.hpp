// Construction, JSON round-trip and serialization tests.

TEST_CASE("Construction and initialization", "[EightFace]") {
	Test::ModuleScaffold<EightFaceModule<8>> mods{createEightFaceModule};
	EightFaceModule<8>* m = mods.create("EightFace");
	EightFaceWidget* mw = Test::createWidget<EightFaceWidget>("EightFace");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
}

TEST_CASE("EightFaceX2 Construction and initialization", "[EightFace]") {
	Test::ModuleScaffold<EightFaceModule<16>> mods;
	EightFaceModule<16>* m = mods.create("EightFaceX2");
	EightFaceX2Widget* mw = Test::createWidget<EightFaceX2Widget>("EightFaceX2");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
}

TEST_CASE("Preset JSON null-guards against a populated module", "[EightFace][JSON]") {
	// A freshly constructed module has every presetSlot empty, so
	// the fuzz helpers never visit the nested "slot" payload (only written when presetSlotUsed[i]
	// -- EightFace.cpp:514-517). Populate a slot before handing dataToJson()'s output to the
	// fuzzers, so the nested-object path is actually exercised.
	Test::ModuleScaffold<EightFaceModule<8>> mods{createEightFaceModule};
	auto module = mods.create("EightFace");
	module->presetSlotUsed[0] = true;
	module->presetSlot[0] = json_pack("{s:i, s:i, s:i}", "id", 1, "leftModuleId", -1, "rightModuleId", -1);
	module->pluginSlug = "Stoermelder-P1";
	module->modelSlug = "Glue";
	module->moduleName = "Stoermelder Glue";

	SECTION("All top-level properties are null-guarded in dataFromJson()") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetNullGuards(module, rootJ);
		json_decref(rootJ);
	}

	SECTION("All properties tolerate wrong-typed values") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetTypeConfusion(module, rootJ);
		json_decref(rootJ);
	}

	SECTION("All arrays tolerate being oversized") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetOversizedArrays(module, rootJ);
		json_decref(rootJ);
	}
}

TEST_CASE("JSON round-trip preserves state", "[EightFace][JSON]") {
	Test::ModuleScaffold<EightFaceModule<8>> mods{createEightFaceModule};
	EightFaceModule<8>* m = mods.create("EightFace");

	SECTION("Scalars") {
		// Distinct, non-default values for every scalar stored to JSON
		m->panelTheme = 1;
		m->dispatch.guiSafeMode = GUISAFEMODE::GUI;
		m->side = SIDE::RIGHT;
		m->pluginSlug = "Stoermelder-P1";
		m->modelSlug = "Glue";
		m->realPluginSlug = "Stoermelder-P1";
		m->realModelSlug = "Glue";
		m->moduleName = "Stoermelder Glue";
		m->slotCvMode = SLOTCVMODE::TRIG_PINGPONG;
		m->preset = 3;
		m->presetCount = 6;
		m->presetCountLongPress = false;

		json_t* rootJ = m->dataToJson();
		REQUIRE(rootJ != nullptr);

		auto restored = mods.create("EightFace");
		restored->dataFromJson(rootJ);

		REQUIRE(restored->panelTheme == 1);
		REQUIRE(restored->dispatch.guiSafeMode == GUISAFEMODE::GUI);
		REQUIRE(restored->side == SIDE::RIGHT);
		REQUIRE(restored->pluginSlug == "Stoermelder-P1");
		REQUIRE(restored->modelSlug == "Glue");
		REQUIRE(restored->realPluginSlug == "Stoermelder-P1");
		REQUIRE(restored->realModelSlug == "Glue");
		REQUIRE(restored->moduleName == "Stoermelder Glue");
		REQUIRE(restored->slotCvMode == SLOTCVMODE::TRIG_PINGPONG);
		REQUIRE(restored->preset == 3);
		REQUIRE(restored->presetCount == 6);
		REQUIRE(restored->presetCountLongPress == false);

		json_decref(rootJ);
	}

	SECTION("Presets array with nested slot objects") {
		// Populate two slots with representative payloads, leave the rest unused.
		auto makeSlotJson = [](int id) {
			json_t* slotJ = json_object();
			json_object_set_new(slotJ, "id", json_integer(id));
			json_object_set_new(slotJ, "leftModuleId", json_integer(-1));
			json_object_set_new(slotJ, "rightModuleId", json_integer(-1));
			return slotJ;
		};
		m->presetSlotUsed[0] = true;
		m->presetSlot[0] = makeSlotJson(101);
		m->presetSlotUsed[5] = true;
		m->presetSlot[5] = makeSlotJson(105);

		json_t* rootJ = m->dataToJson();
		REQUIRE(rootJ != nullptr);

		// Serialization: one entry per slot, payload only for used slots
		json_t* presetsJ = json_object_get(rootJ, "presets");
		REQUIRE(json_is_array(presetsJ));
		REQUIRE(json_array_size(presetsJ) == 8);
		REQUIRE(json_is_true(json_object_get(json_array_get(presetsJ, 0), "slotUsed")));
		REQUIRE(json_object_get(json_array_get(presetsJ, 0), "slot") != nullptr);
		REQUIRE(json_is_false(json_object_get(json_array_get(presetsJ, 1), "slotUsed")));
		REQUIRE(json_object_get(json_array_get(presetsJ, 1), "slot") == nullptr);

		auto restored = mods.create("EightFace");
		restored->dataFromJson(rootJ);

		// Every slot restores its used-flag; payloads come back as deep copies
		for (int i = 0; i < restored->presetMax; i++) {
			CATCH_INFO("Slot " << i);
			bool expectedUsed = (i == 0 || i == 5);
			REQUIRE(restored->presetSlotUsed[i] == expectedUsed);
			if (expectedUsed) {
				REQUIRE(restored->presetSlot[i] != m->presetSlot[i]);
				REQUIRE(json_equal(restored->presetSlot[i], m->presetSlot[i]) == 1);
			}
			else {
				REQUIRE(restored->presetSlot[i] == nullptr);
			}
		}

		json_decref(rootJ);
	}

}


TEST_CASE("dataFromJson tolerates an oversized presets array", "[EightFace][JSON]") {
	Test::ModuleScaffold<EightFaceModule<8>> mods{createEightFaceModule};
	// A preset written by a build with more slots (or a hand-edited patch) must
	// not write past the fixed-size presetSlotUsed[]/presetSlot[] arrays.
	EightFaceModule<8>* m = mods.create("EightFace");

	json_t* rootJ = json_object();
	json_t* presetsJ = json_array();
	for (int i = 0; i < 20; i++) {
		json_t* itemJ = json_object();
		// The first (in-bounds) and all out-of-bounds entries claim slots
		bool used = (i == 0) || (i >= 8);
		json_object_set_new(itemJ, "slotUsed", json_boolean(used));
		if (used) {
			json_t* slotJ = json_object();
			json_object_set_new(slotJ, "marker", json_integer(i));
			json_object_set_new(itemJ, "slot", slotJ);
		}
		json_array_append_new(presetsJ, itemJ);
	}
	json_object_set_new(rootJ, "presets", presetsJ);

	REQUIRE_NOTHROW(m->dataFromJson(rootJ));

	// The in-bounds entry applied normally and kept its exact payload; the
	// out-of-bounds entries were dropped rather than smashing slot 0's storage
	// (before the bound check, presetSlotUsed[8] aliased the first byte of
	// presetSlot[0], corrupting the pointer).
	REQUIRE(m->presetSlotUsed[0] == true);
	REQUIRE(json_integer_value(json_object_get(m->presetSlot[0], "marker")) == 0);
	for (int i = 1; i < m->presetMax; i++) {
		REQUIRE(m->presetSlotUsed[i] == false);
	}

	json_decref(rootJ);
}


// ---- Serialization gaps: autoload, preset clamp, PARAM_RW, onReset, guiSafeMode default -------

TEST_CASE("autoload defers past patch load and fires once the expander pointer resolves", "[EightFace][JSON]") {
	// FIXED. The TODO formerly at EightFace.cpp:582-583 ("presetLoad might fail on patch-load if
	// this module is loaded before the expanded module") misdiagnosed the mechanism as load order;
	// the real cause is timing, not order. Module::Expander::module is only populated once per
	// engine step (Rack/src/engine/Engine.cpp's "Update expander pointers"), strictly after every
	// module's Module::fromJson() -- and therefore dataFromJson() -- has already run during patch
	// deserialization (Rack/src/engine/Module.cpp sets only .moduleId from JSON, never .module).
	// So dispatching the load directly from dataFromJson() could never work, regardless of which
	// module a patch lists first. Fixed by deferring: dataFromJson() only records
	// `pendingAutoload`, and process() consumes it once `connected == 2` (the same "expander
	// resolved AND verified as the right model" gate presetLoad()'s other paths already use).
	Test::Harness h{Test::UiMode::UiPresent};
	EightFaceModule<8>* boundM = h.addModule<EightFaceModule<8>>(createEightFaceModule);
	EightFaceWidget* boundMw = Test::createWidget<EightFaceWidget>(boundM);
	Test::registerModule(boundM, boundMw);

	EightFaceModule<8>* restored = h.addModule<EightFaceModule<8>>(createEightFaceModule);
	h.addWidget<EightFaceWidget>(restored);

	// Slot 0 holds a real, distinctive preset so a successful autoload is observable.
	boundM->panelTheme = 77;
	restored->presetSlotUsed[0] = true;
	restored->presetSlot[0] = boundMw->toJson();
	boundM->panelTheme = 0;

	json_t* rootJ = restored->dataToJson();
	json_object_set_new(rootJ, "autoload", json_integer((int)AUTOLOAD::FIRST));

	// The expander id is known (as it would be from a real patch's "leftModuleId" key) but its
	// pointer is unresolved -- exactly Module::fromJson()'s state right after patch
	// deserialization, before the engine has stepped even once.
	restored->side = SIDE::LEFT;
	restored->leftExpander.moduleId = boundM->id;
	restored->leftExpander.module = nullptr;

	restored->dataFromJson(rootJ);
	json_decref(rootJ);
	REQUIRE(restored->pendingAutoload == true);

	// One process() call with the pointer still unresolved must not crash and must leave the
	// autoload still pending -- this is the exact moment the old code would have silently no-op'd
	// (or, before that, dereferenced a null exp->module if the guard were ever weakened).
	h.dspStep();
	REQUIRE(restored->pendingAutoload == true);
	REQUIRE(restored->preset == -1);

	// Now simulate Engine::stepBlock()'s "Update expander pointers" resolving the id into a real
	// pointer -- the harness's dspStep() doesn't do this itself (there's no second EightFace module
	// occupying the physical expander slot), so it's set directly here, matching what the engine
	// would do on the next real step.
	restored->leftExpander.module = boundM;
	restored->realPluginSlug = boundM->model->plugin->slug;
	restored->realModelSlug = boundM->model->slug;

	h.dspStep();

	// The deferred autoload fired exactly once: pendingAutoload consumed, slot 0 applied.
	REQUIRE(restored->pendingAutoload == false);
	h.uiFrame();
	REQUIRE(boundM->panelTheme == 77);

	Test::unregisterModule(boundM, boundMw);
}

TEST_CASE("autoload survives a JSON round-trip", "[EightFace][JSON]") {
	// FIXED. `autoload` is documented "[Stored to JSON]" (EightFace.cpp) and exposed on the context
	// menu, but dataToJson()/dataFromJson() never carried it -- present since the feature was
	// introduced (confirmed via git history: no commit ever touched the "autoload" JSON key before
	// this fix). The autoload-on-load switch further down dataFromJson() was dead code as a result,
	// since `autoload` was always AUTOLOAD::OFF at that point (reset by the constructor).
	Test::ModuleScaffold<EightFaceModule<8>> mods{createEightFaceModule};
	EightFaceModule<8>* m = mods.create("EightFace");
	m->autoload = AUTOLOAD::LASTACTIVE;

	json_t* rootJ = m->dataToJson();
	REQUIRE(rootJ != nullptr);

	auto restored = mods.create("EightFace");
	restored->dataFromJson(rootJ);
	json_decref(rootJ);

	REQUIRE(restored->autoload == AUTOLOAD::LASTACTIVE);
}

TEST_CASE("preset >= presetCount on load clamps to 0", "[EightFace][JSON]") {
	// EightFace.cpp:577-578, "if (preset >= presetCount) preset =
	// 0;" -- mk1 clamps to the FIRST slot, deliberately different from mk2's clamp to -1 (no active
	// slot at all). Guards a shortened presetCount (or a hand-edited/corrupted patch) from leaving
	// `preset` pointing past the active range.
	Test::ModuleScaffold<EightFaceModule<8>> mods{createEightFaceModule};
	EightFaceModule<8>* m = mods.create("EightFace");

	json_t* rootJ = json_object();
	json_object_set_new(rootJ, "presetCount", json_integer(4));
	json_object_set_new(rootJ, "preset", json_integer(4));  // == presetCount: out of range
	m->dataFromJson(rootJ);
	json_decref(rootJ);

	REQUIRE(m->presetCount == 4);
	REQUIRE(m->preset == 0);
}

TEST_CASE("CTRLMODE_PARAM is forced to Read (0) after loading a patch", "[EightFace][JSON]") {
	// EightFace.cpp:603, unconditional at the end of dataFromJson().
	// A saved Write- or Auto-mode patch must not resume mutating slots (Write) or auto-saving
	// (Auto) the instant it loads -- Read is the only safe post-load default, regardless of what
	// was saved.
	Test::ModuleScaffold<EightFaceModule<8>> mods{createEightFaceModule};
	EightFaceModule<8>* m = mods.create("EightFace");
	m->params[EightFaceModule<8>::CTRLMODE_PARAM].setValue((float)CTRLMODE::WRITE);

	json_t* rootJ = m->dataToJson();

	EightFaceModule<8>* m2 = mods.create("EightFace");
	m2->params[EightFaceModule<8>::CTRLMODE_PARAM].setValue((float)CTRLMODE::WRITE);
	m2->dataFromJson(rootJ);
	json_decref(rootJ);

	REQUIRE(m2->params[EightFaceModule<8>::CTRLMODE_PARAM].getValue() == 0.f);
}

TEST_CASE("onReset clears slots, connection state and bound-module identity without leaking or double-freeing", "[EightFace]") {
	// Review #16. Run under ASan (the test binary already links
	// it) so the decref/clear pairing in onReset() (EightFace.cpp:169-192) actually gets checked:
	// presetSlotUsed[i] must still be true when presetSlot[i] is decref'd, or the object leaks.
	Test::ModuleScaffold<EightFaceModule<8>> mods{createEightFaceModule};
	EightFaceModule<8>* m = mods.create("EightFace");

	m->presetSlotUsed[0] = true;
	m->presetSlot[0] = json_pack("{s:i}", "id", 1);
	m->presetSlotUsed[3] = true;
	m->presetSlot[3] = json_pack("{s:i}", "id", 2);
	m->pluginSlug = "SomePlugin";
	m->modelSlug = "SomeModel";
	m->realPluginSlug = "SomePlugin";
	m->realModelSlug = "SomeModel";
	m->moduleName = "Bound";
	m->connected = 2;
	m->preset = 3;
	m->autoload = AUTOLOAD::FIRST;

	Module::ResetEvent re;
	m->onReset(re);

	for (int i = 0; i < m->presetMax; i++) {
		REQUIRE(m->presetSlotUsed[i] == false);
		REQUIRE(m->presetSlot[i] == nullptr);
	}
	REQUIRE(m->pluginSlug == "");
	REQUIRE(m->modelSlug == "");
	REQUIRE(m->realPluginSlug == "");
	REQUIRE(m->realModelSlug == "");
	REQUIRE(m->moduleName == "");
	REQUIRE(m->connected == 0);
	REQUIRE(m->preset == -1);
	REQUIRE(m->autoload == AUTOLOAD::OFF);
	REQUIRE(m->dispatch.guiSafeMode == GUISAFEMODE::GUI_WITH_LOCK);

	// A second reset on the now-empty module must not double-free anything it already cleared.
	m->onReset(re);
}

TEST_CASE("guiSafeMode defaults to WORKER when the key is absent from JSON", "[EightFace][JSON]") {
	// EightFace.cpp:529, "guiSafeModeJ ? ... :
	// GUISAFEMODE::WORKER" -- a pre-existing patch saved before guiSafeMode was introduced has no
	// such key, and must fall back to WORKER (Unsafe fast), NOT the constructor's own default
	// (GUI_WITH_LOCK, set by onReset() at EightFace.cpp:178). The two defaults deliberately differ.
	Test::ModuleScaffold<EightFaceModule<8>> mods{createEightFaceModule};
	EightFaceModule<8>* m = mods.create("EightFace");
	REQUIRE(m->dispatch.guiSafeMode == GUISAFEMODE::GUI_WITH_LOCK);

	json_t* rootJ = json_object();
	m->dataFromJson(rootJ);
	json_decref(rootJ);

	REQUIRE(m->dispatch.guiSafeMode == GUISAFEMODE::WORKER);
}

TEST_CASE("Clearing the last used slot resets pluginSlug/modelSlug/moduleName", "[EightFace]") {
	// EightFace.cpp:518-525 -- presetClear() only wipes the bound-
	// module identity once EVERY slot has become empty; clearing one of several used slots must
	// leave the identity (and the other slot) alone.
	Test::ModuleScaffold<EightFaceModule<8>> mods{createEightFaceModule};
	EightFaceModule<8>* m = mods.create("EightFace");
	m->presetSlotUsed[0] = true;
	m->presetSlot[0] = json_pack("{s:i}", "id", 1);
	m->presetSlotUsed[1] = true;
	m->presetSlot[1] = json_pack("{s:i}", "id", 2);
	m->pluginSlug = "SomePlugin";
	m->modelSlug = "SomeModel";
	m->moduleName = "Bound Module";

	m->presetClear(0);
	// One slot is still used: identity survives.
	REQUIRE(m->pluginSlug == "SomePlugin");
	REQUIRE(m->modelSlug == "SomeModel");
	REQUIRE(m->moduleName == "Bound Module");
	REQUIRE(m->presetSlotUsed[1] == true);

	m->presetClear(1);
	// Now every slot is empty: identity is wiped.
	REQUIRE(m->pluginSlug == "");
	REQUIRE(m->modelSlug == "");
	REQUIRE(m->moduleName == "");
}

TEST_CASE("presetSetCount resets preset when it falls outside the new count", "[EightFace]") {
	// EightFace.cpp:487-492.
	Test::ModuleScaffold<EightFaceModule<8>> mods{createEightFaceModule};
	EightFaceModule<8>* m = mods.create("EightFace");

	SECTION("preset within the new count is untouched... except presetSetCount only guards >=") {
		// presetSetCount's guard is "if (preset >= p) preset = 0;" -- a preset strictly below the
		// new count is left alone.
		m->preset = 2;
		m->presetSetCount(5);
		REQUIRE(m->preset == 2);
		REQUIRE(m->presetCount == 5);
	}

	SECTION("preset outside (or equal to) the new count resets to 0") {
		m->preset = 5;
		m->presetSetCount(3);
		REQUIRE(m->preset == 0);
		REQUIRE(m->presetCount == 3);
	}

	SECTION("presetPrev and presetNext are cleared") {
		m->presetPrev = 2;
		m->presetNext = 4;
		m->presetSetCount(6);
		REQUIRE(m->presetPrev == -1);
		REQUIRE(m->presetNext == -1);
	}
}
