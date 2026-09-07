// Construction, JSON round-trip and serialization tests.

TEST_CASE("Construction and initialization", "[EightFaceMk2]") {
	Test::ModuleScaffold<EightFaceMk2Module<8>> mods{createEightFaceMk2Module};
	EightFaceMk2Module<8>* m = mods.create("EightFaceMk2");
	EightFaceMk2Widget<8>* mw = Test::createWidget<EightFaceMk2Widget<8>>("EightFaceMk2");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
}

TEST_CASE("EightFaceMk2Ex Construction and initialization", "[EightFaceMk2]") {
	Test::ModuleScaffold<EightFaceMk2ExModule<8>> mods;
	EightFaceMk2ExModule<8>* m = mods.create("EightFaceMk2Ex");
	EightFaceMk2ExWidget<8>* mw = Test::createWidget<EightFaceMk2ExWidget<8>>("EightFaceMk2Ex");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
}

TEST_CASE("Preset JSON null-guards", "[EightFaceMk2][JSON]") {
	Test::ModuleScaffold<EightFaceMk2Module<8>> mods{createEightFaceMk2Module};
	auto module = mods.create("EightFaceMk2");

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

TEST_CASE("JSON round-trip preserves presets", "[EightFaceMk2][JSON]") {
	Test::ModuleScaffold<EightFaceMk2Module<8>> mods{createEightFaceMk2Module};
	EightFaceMk2Module<8>* m = mods.create("EightFaceMk2");

	// Distinctive label on EVERY slot
	for (int i = 0; i < 8; i++) {
		m->textLabel[i] = "Slot" + std::to_string(i);
	}
	m->presetSlotUsed[0] = true;
	m->presetSlotUsed[5] = true;
	// The vector owns the json_t; both modules free their own copies in their dtors.
	// Qualified: EightFaceMk2Module declares `int preset` (active slot), shadowing the base array.
	m->EightFaceMk2Base<8>::preset[0].push_back(json_pack("{s:i}", "id", 123));
	m->EightFaceMk2Base<8>::preset[5].push_back(json_pack("{s:i}", "id", 456));
	m->EightFaceMk2Base<8>::preset[5].push_back(json_pack("{s:i}", "id", 789));

	json_t* j = m->dataToJson();

	EightFaceMk2Module<8>* m2 = mods.create("EightFaceMk2");
	m2->dataFromJson(j);
	json_decref(j);

	SECTION("All slot labels") {
		for (int i = 0; i < 8; i++) {
			REQUIRE(m2->textLabel[i] == "Slot" + std::to_string(i));
		}
	}

	SECTION("Slot 0: single preset object") {
		REQUIRE(m2->presetSlotUsed[0] == true);
		REQUIRE(m2->EightFaceMk2Base<8>::preset[0].size() == 1);
		REQUIRE(json_integer_value(json_object_get(m2->EightFaceMk2Base<8>::preset[0][0], "id")) == 123);
	}

	SECTION("Slot 5: two preset objects survive in order") {
		REQUIRE(m2->presetSlotUsed[5] == true);
		REQUIRE(m2->EightFaceMk2Base<8>::preset[5].size() == 2);
		REQUIRE(json_integer_value(json_object_get(m2->EightFaceMk2Base<8>::preset[5][0], "id")) == 456);
		REQUIRE(json_integer_value(json_object_get(m2->EightFaceMk2Base<8>::preset[5][1], "id")) == 789);
	}

}


TEST_CASE("applyPreset does not decrement refcount of slot-owned json objects", "[EightFaceMk2]") {
	Test::ModuleScaffold<EightFaceMk2Module<8>> mods{createEightFaceMk2Module};
	// Regression test: commit 84866bc incorrectly added json_decref(vJ) inside the GUI-thread
	// apply path. vJ pointers queued for the UI thread are owned by slot->preset -- applying a
	// preset must not touch the refcount or the preset slot's json_t* becomes a dangling pointer.

	EightFaceMk2Module<8>* m = mods.create("EightFaceMk2");
	// A second module instance acts as the "bound" module whose preset is being loaded.
	EightFaceMk2Module<8>* boundM = mods.create("EightFaceMk2");
	EightFaceMk2Widget<8>* boundMw = Test::createWidget<EightFaceMk2Widget<8>>(boundM);
	bindForTest(m, boundM, boundMw);
	// process() is what establishes presetTotal/N[], which expSlot() (and so applyPreset())
	// needs; ModuleScaffold itself never steps the module.
	m->process(Test::makeProcessArgs(0));

	// Use Unsafe mode: applyPreset() calls boundMw->module->fromJson(vJ), and the dispatch runs
	// inline here because taskWorker is a NullTaskWorker and guiSafeMode != WORKER routes through
	// guiTasks.enqueue(), not the worker -- draining it directly with drain() keeps this test
	// about the refcount, not about which thread drains the queue.
	m->dispatch.guiSafeMode = GUISAFEMODE::GUI;

	json_t* vJ = m->toJson();
	size_t refcount = vJ->refcount;

	EightFaceMk2Slot* slot = m->faceSlot(0);
	slot->preset->push_back(vJ);
	*(slot->presetSlotUsed) = true;
	m->boundModules[0]->moduleId = boundM->id;

	m->presetPrev = -1;
	m->applyPreset(m->dispatch.allLoader(), -1, 0);
	m->dispatch.drain();

	REQUIRE(json_typeof(vJ) == JSON_OBJECT);
	REQUIRE(vJ->refcount == refcount);

	slot->preset->clear();
	json_decref(vJ);

	// bindForTest() parented boundMw under APP->scene->rack via registerModule(); a bare
	// destroyWidget() would trip Widget's "!parent" destructor assert, so unregister first.
	// ModuleScaffold still owns deleting boundM itself (unregisterModule only un-parents it).
	Test::unregisterModule(boundM, boundMw);
}


// ---- Serialization: boundModules, ctrlUniqueId, preset clamp, PARAM_RW, onReset, boxColor -----

TEST_CASE("boundModules round-trips id/slugs/name, and needsGuiThread is recomputed on load", "[EightFaceMk2][JSON]") {
	// needsGuiThread is NOT stored (EightFaceMk2.cpp:794-798's boundModuleJ has no such key) -- it
	// is recomputed from EightFace::guiModuleSlugs every dataFromJson() (:859-860). So the round
	// trip has two things to prove: the stored fields survive, and the derived one is freshly
	// looked up rather than defaulting to false or carrying over some other stale value.
	Test::ModuleScaffold<EightFaceMk2Module<8>> mods{createEightFaceMk2Module};
	EightFaceMk2Module<8>* m = mods.create("EightFaceMk2");

	// One ordinary bound module, one on the allowlist (Stoermelder-P1/MidiCat, EightFace.hpp:20).
	auto* ordinary = new EightFaceMk2Module<8>::BoundModule;
	ordinary->moduleId = 111;
	ordinary->pluginSlug = "SomePlugin";
	ordinary->modelSlug = "SomeModel";
	ordinary->moduleName = "Ordinary";
	ordinary->needsGuiThread = true;  // deliberately wrong going in -- load must correct it to false
	m->boundModules.push_back(ordinary);

	auto* allowlisted = new EightFaceMk2Module<8>::BoundModule;
	allowlisted->moduleId = 222;
	allowlisted->pluginSlug = "Stoermelder-P1";
	allowlisted->modelSlug = "MidiCat";
	allowlisted->moduleName = "Allowlisted";
	allowlisted->needsGuiThread = false;  // deliberately wrong going in -- load must correct it to true
	m->boundModules.push_back(allowlisted);

	json_t* j = m->dataToJson();

	EightFaceMk2Module<8>* m2 = mods.create("EightFaceMk2");
	m2->dataFromJson(j);
	json_decref(j);

	REQUIRE(m2->boundModules.size() == 2);

	EightFaceMk2Module<8>::BoundModule* b0 = m2->boundModules[0];
	REQUIRE(b0->moduleId == 111);
	REQUIRE(b0->pluginSlug == "SomePlugin");
	REQUIRE(b0->modelSlug == "SomeModel");
	REQUIRE(b0->moduleName == "Ordinary");
	REQUIRE(b0->needsGuiThread == false);

	EightFaceMk2Module<8>::BoundModule* b1 = m2->boundModules[1];
	REQUIRE(b1->moduleId == 222);
	REQUIRE(b1->pluginSlug == "Stoermelder-P1");
	REQUIRE(b1->modelSlug == "MidiCat");
	REQUIRE(b1->moduleName == "Allowlisted");
	REQUIRE(b1->needsGuiThread == true);
}

TEST_CASE("A boundModules entry missing pluginSlug/modelSlug/moduleName does not crash", "[EightFaceMk2][JSON]") {
	// FIXED (review #17). json_string_value() returns NULL for a missing/non-string key, and
	// dataFromJson() used to hand that straight to std::string's constructor -- UB, not merely a
	// wrong value. Hand-built JSON here (not a round-trip) since dataToJson() always writes all
	// three keys; a hand-edited or corrupted patch is the only way this shape reaches dataFromJson.
	Test::ModuleScaffold<EightFaceMk2Module<8>> mods{createEightFaceMk2Module};
	EightFaceMk2Module<8>* m = mods.create("EightFaceMk2");

	json_t* rootJ = json_object();
	json_t* boundModulesJ = json_array();

	json_t* missingAllJ = json_object();
	json_object_set_new(missingAllJ, "moduleId", json_integer(111));
	json_array_append_new(boundModulesJ, missingAllJ);

	json_t* wrongTypeJ = json_object();
	json_object_set_new(wrongTypeJ, "moduleId", json_integer(222));
	json_object_set_new(wrongTypeJ, "pluginSlug", json_integer(42));
	json_object_set_new(wrongTypeJ, "modelSlug", json_null());
	json_array_append_new(boundModulesJ, wrongTypeJ);

	json_object_set_new(rootJ, "boundModules", boundModulesJ);

	m->dataFromJson(rootJ);
	json_decref(rootJ);

	// No crash reaching here is the primary assertion. Both entries fall back to an empty string
	// rather than propagating NULL.
	REQUIRE(m->boundModules.size() == 2);
	REQUIRE(m->boundModules[0]->pluginSlug == "");
	REQUIRE(m->boundModules[0]->modelSlug == "");
	REQUIRE(m->boundModules[0]->moduleName == "");
	REQUIRE(m->boundModules[1]->pluginSlug == "");
	REQUIRE(m->boundModules[1]->modelSlug == "");
}

TEST_CASE("ctrlUniqueId: absent in JSON becomes -2, then is rewritten to this module's id on load", "[EightFaceMk2][JSON]") {
	// EightFaceMk2Base::dataFromJson (EightFaceMk2Base.hpp:90-91) reads ctrlUniqueId or defaults to
	// -2 when the key is missing -- the legacy-patch case, predating ctrlUniqueId's introduction.
	// EightFaceMk2::dataFromJson then adopts it as this instance's own id (EightFaceMk2.cpp:867:
	// "if (BASE::ctrlUniqueId == -2) BASE::ctrlUniqueId = Module::id;"), which is what lets
	// expanderCleanUp() (:733-738) treat a legacy expander as belonging to its controller instead
	// of wiping it -- the -2 adoption path a hand-edited or pre-uniqueId patch takes.
	Test::ModuleScaffold<EightFaceMk2Module<8>> mods{createEightFaceMk2Module};

	SECTION("Key absent from the JSON entirely") {
		EightFaceMk2Module<8>* m = mods.create("EightFaceMk2");
		json_t* rootJ = json_object();
		m->dataFromJson(rootJ);
		json_decref(rootJ);

		REQUIRE(m->ctrlUniqueId == m->id);
		REQUIRE(m->ctrlUniqueId != -2);
	}

	SECTION("Round trip through a fresh module preserves a real ctrlUniqueId") {
		EightFaceMk2Module<8>* m = mods.create("EightFaceMk2");
		int64_t original = m->ctrlUniqueId;
		REQUIRE(original != -2);

		json_t* j = m->dataToJson();
		EightFaceMk2Module<8>* m2 = mods.create("EightFaceMk2");
		m2->dataFromJson(j);
		json_decref(j);

		// A real (non-legacy) id survives the round trip unchanged -- it is NOT rewritten to m2's
		// own id, since the rewrite is conditional on BASE::ctrlUniqueId == -2 specifically.
		REQUIRE(m2->ctrlUniqueId == original);
		REQUIRE(m2->ctrlUniqueId != m2->id);
	}
}

TEST_CASE("preset >= presetCount on load clamps to -1", "[EightFaceMk2][JSON]") {
	// EightFaceMk2.cpp:829-831. Guards a shortened presetCount (or a hand-edited/corrupted patch)
	// from leaving `preset` pointing past the active range.
	Test::ModuleScaffold<EightFaceMk2Module<8>> mods{createEightFaceMk2Module};
	EightFaceMk2Module<8>* m = mods.create("EightFaceMk2");

	json_t* rootJ = json_object();
	json_object_set_new(rootJ, "presetCount", json_integer(4));
	json_object_set_new(rootJ, "preset", json_integer(4));  // == presetCount: out of range
	m->dataFromJson(rootJ);
	json_decref(rootJ);

	REQUIRE(m->presetCount == 4);
	REQUIRE(m->preset == -1);
}

TEST_CASE("PARAM_RW is forced to Read (0) after loading a patch", "[EightFaceMk2][JSON]") {
	// EightFaceMk2.cpp:868, unconditional at the end of dataFromJson(). A saved Write- or
	// Auto-mode patch must not resume mutating slots (Write) or auto-saving (Auto) the instant it
	// loads -- Read is the only safe post-load default, regardless of what was saved.
	Test::ModuleScaffold<EightFaceMk2Module<8>> mods{createEightFaceMk2Module};
	EightFaceMk2Module<8>* m = mods.create("EightFaceMk2");
	m->params[EightFaceMk2Module<8>::PARAM_RW].setValue((float)CTRLMODE::WRITE);

	json_t* j = m->dataToJson();

	EightFaceMk2Module<8>* m2 = mods.create("EightFaceMk2");
	m2->params[EightFaceMk2Module<8>::PARAM_RW].setValue((float)CTRLMODE::WRITE);
	m2->dataFromJson(j);
	json_decref(j);

	REQUIRE(m2->params[EightFaceMk2Module<8>::PARAM_RW].getValue() == 0.f);
}

TEST_CASE("onReset clears slots, labels and bound modules without leaking or double-freeing", "[EightFaceMk2]") {
	// Review #16. Run under ASan (the test binary already links it) so the decref/clear pairing in
	// onReset() (EightFaceMk2.cpp:189-201) actually gets checked: presetSlotUsed[i] must still be
	// true when the json_t* vector is walked and decref'd, or the objects leak; and boundModules'
	// entries must be deleted exactly once, or a double-free/UAF follows.
	Test::ModuleScaffold<EightFaceMk2Module<8>> mods{createEightFaceMk2Module};
	EightFaceMk2Module<8>* m = mods.create("EightFaceMk2");

	m->textLabel[0] = "Slot0";
	m->presetSlotUsed[0] = true;
	m->EightFaceMk2Base<8>::preset[0].push_back(json_pack("{s:i}", "id", 1));
	m->EightFaceMk2Base<8>::preset[0].push_back(json_pack("{s:i}", "id", 2));

	auto* b = new EightFaceMk2Module<8>::BoundModule;
	b->moduleId = 999;
	b->pluginSlug = "SomePlugin";
	b->modelSlug = "SomeModel";
	b->moduleName = "Bound";
	m->boundModules.push_back(b);

	Module::ResetEvent re;
	m->onReset(re);

	REQUIRE(m->presetSlotUsed[0] == false);
	REQUIRE(m->EightFaceMk2Base<8>::preset[0].empty());
	REQUIRE(m->textLabel[0] == "");
	REQUIRE(m->boundModules.empty());

	// A second reset on the now-empty module must not double-free anything it already cleared.
	m->onReset(re);
}

TEST_CASE("boxColor hex round-trips, and a malformed string does not crash dataFromJson", "[EightFaceMk2][JSON]") {
	Test::ModuleScaffold<EightFaceMk2Module<8>> mods{createEightFaceMk2Module};

	SECTION("Round trip") {
		EightFaceMk2Module<8>* m = mods.create("EightFaceMk2");
		m->boxColor = color::fromHexString("#abcdef");

		json_t* j = m->dataToJson();
		EightFaceMk2Module<8>* m2 = mods.create("EightFaceMk2");
		m2->dataFromJson(j);
		json_decref(j);

		REQUIRE(color::toHexString(m2->boxColor) == "#abcdef");
	}

	SECTION("Malformed hex string is tolerated") {
		EightFaceMk2Module<8>* m = mods.create("EightFaceMk2");
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "boxColor", json_string("not-a-color"));
		m->dataFromJson(rootJ);
		json_decref(rootJ);
		// No crash is the assertion; color::fromHexString's own parsing failure mode (NVG_TRANSPARENT
		// or similar) is implementation detail, not part of this module's contract.
	}

	SECTION("Wrong-typed value (not a string) is ignored, not applied") {
		EightFaceMk2Module<8>* m = mods.create("EightFaceMk2");
		NVGcolor before = m->boxColor;
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "boxColor", json_integer(42));
		m->dataFromJson(rootJ);
		json_decref(rootJ);

		// dataFromJson guards with json_is_string() (:822) -- a non-string value must leave
		// boxColor untouched rather than being coerced or crashing json_string_value().
		REQUIRE(color::toHexString(m->boxColor) == color::toHexString(before));
	}
}
