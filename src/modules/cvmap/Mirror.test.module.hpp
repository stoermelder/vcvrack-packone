// MIRROR test cases. Included by Mirror.test.cpp inside namespace __module.
// Not a standalone header: Mirror.test.hpp supplies everything these cases use.

TEST_CASE("Construction and initialization", "[Mirror]") {
	Test::ModuleScaffold<MirrorModule> mods;
	MirrorModule* m = mods.create("Mirror");
	MirrorWidget* mw = Test::createWidget<MirrorWidget>("Mirror");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
}

TEST_CASE("Preset JSON null-guards", "[Mirror][JSON]") {
	Test::ModuleScaffold<MirrorModule> mods;
	auto module = mods.create("Mirror");

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

TEST_CASE("JSON round-trip preserves state", "[Mirror][JSON]") {
	Test::ModuleScaffold<MirrorModule> mods;
	MirrorModule* m = mods.create("Mirror");

	m->targetModuleIds = {11, 22, 33};
	// Distinctive paramId on EVERY CV input
	for (int i = 0; i < 8; i++) {
		m->cvParamId[i] = 10 + i;
	}

	json_t* j = m->dataToJson();

	MirrorModule* m2 = mods.create("Mirror");
	m2->dataFromJson(j);
	json_decref(j);

	REQUIRE(m2->targetModuleIds.size() == 3);
	REQUIRE(m2->targetModuleIds[0] == 11);
	REQUIRE(m2->targetModuleIds[1] == 22);
	REQUIRE(m2->targetModuleIds[2] == 33);

	for (int i = 0; i < 8; i++) {
		REQUIRE(m2->cvParamId[i] == 10 + i);
	}
}

// ---- Parameter mapping ---------------------------------------------------------------------
//
// Mirror is the plugin's most ParamHandle-dense module (33 references) and had no mapping test
// at all before Harness gained mapping support: the setup cost — a registered source module, a
// real left-expander link with a resolved moduleId, and handles created dynamically by
// bindToSource() — is exactly the boilerplate that kept these untested.

// A stand-in for the module a user puts to Mirror's left/right. Uses a real model so
// bindToSource() can read model->plugin->slug, which it stores and later compares.
static rack::Module* createMirrorPeer(Test::Harness& h) {
	// Any registered module with params works; Macro is a small one in the same pack.
	return h.addModule<rack::Module>("Macro");
}

TEST_CASE("bindToSource creates a handle per source parameter", "[Mirror][mapping]") {
	Test::Harness h;
	MirrorModule* m = h.addModule<MirrorModule>("Mirror");
	rack::Module* source = createMirrorPeer(h);

	// The real wiring: connectExpander assigns Expander::moduleId as Rack does, which
	// bindToSource() reads. A hand-wired `leftExpander.module = source` leaves moduleId at -1
	// and bindToSource() returns immediately, silently doing nothing.
	h.connectExpander(source, m);
	REQUIRE(m->leftExpander.moduleId == source->id);

	m->bindToSource();

	REQUIRE(m->sourceHandles.size() == source->params.size());
	REQUIRE(m->sourceModuleId == source->id);

	// Every handle must fully resolve — ids AND module pointer.
	for (size_t i = 0; i < m->sourceHandles.size(); i++) {
		h.requireMapped(m->sourceHandles[i], source, int(i));
	}
}

TEST_CASE("bindToSource does nothing without a left neighbour", "[Mirror][mapping]") {
	Test::Harness h;
	MirrorModule* m = h.addModule<MirrorModule>("Mirror");

	m->bindToSource();

	REQUIRE(m->sourceHandles.empty());
}

TEST_CASE("Mirror propagates a source parameter to its target", "[Mirror][mapping]") {
	// The module's reason to exist, and previously unverified end to end.
	Test::Harness h;
	MirrorModule* m = h.addModule<MirrorModule>("Mirror");
	rack::Module* source = createMirrorPeer(h);
	rack::Module* target = createMirrorPeer(h);

	h.connectExpander(source, m);
	m->bindToSource();
	REQUIRE_FALSE(m->sourceHandles.empty());

	// bindToTarget() resolves its neighbour through APP->scene->rack rather than the engine,
	// so it needs a widget in the rack; map the target handles directly instead, which is the
	// state bindToTarget() would have produced.
	for (size_t i = 0; i < m->sourceHandles.size(); i++) {
		rack::ParamHandle* targetHandle = new rack::ParamHandle;
		APP->engine->addParamHandle(targetHandle);
		m->targetHandles.push_back(targetHandle);
		h.mapParam(targetHandle, target, int(i));
	}

	// A value inside the target param's own range: Macro's params are 0..1, and
	// setImmediateValue clamps, so an out-of-range value would silently assert against the
	// clamped bound instead of the value under test.
	const float value = h.distinctValueFor(m->sourceHandles[0]);
	REQUIRE(value != Catch::Approx(h.mappedValue(m->targetHandles[0])));

	// Turn the source knob, then let Mirror run. processDivider divides by 32, so a single
	// step is not enough — this is exactly the kind of rate detail the harness makes explicit.
	h.setMappedValue(m->sourceHandles[0], value);
	h.dspSteps(64);

	REQUIRE(h.mappedValue(m->sourceHandles[0]) == Catch::Approx(value));
	REQUIRE(h.mappedValue(m->targetHandles[0]) == Catch::Approx(value));
}

TEST_CASE("Mirror's handles do not outlive their target", "[Mirror][mapping]") {
	// Mirror creates handles with `new` and cleans them up on a UI-thread task queue, so a
	// target destroyed while handles still claim it is a real lifetime question here.
	Test::Harness h;
	MirrorModule* m = h.addModule<MirrorModule>("Mirror");

	int64_t sourceId = -1;
	{
		Test::Harness inner;
		rack::Module* source = createMirrorPeer(inner);
		sourceId = source->id;
		h.connectExpander(source, m);
		m->bindToSource();
		REQUIRE_FALSE(m->sourceHandles.empty());
		REQUIRE(m->sourceHandles[0]->module == source);

		// Rack clears the neighbour when a module leaves the rack; without this the expander
		// pointer would dangle independently of the handles under test.
		h.disconnectExpander(m, Test::Harness::SIDE_LEFT);
	}

	// The engine nulled the module pointer on unregistration; no dangling target.
	REQUIRE(m->sourceHandles[0]->module == nullptr);
	REQUIRE(m->sourceHandles[0]->moduleId == sourceId);
	REQUIRE_FALSE(h.isMapped(m->sourceHandles[0]));
}