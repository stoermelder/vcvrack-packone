// mk1-specific behavior: connection detection, SIDE selection, LED states, 8FACEx2.

// RAII unregister for a module/widget pair registered directly (Test::registerModule) rather than
// through a Harness -- so a failing REQUIRE (which throws through Catch2) still unregisters the
// widget instead of leaking it into APP->scene->rack, where it survives until process exit and
// crashes there (rack::app::RackWidget::clear() finds a widget whose module was already deleted by
// the enclosing ModuleScaffold). Mirrors why ModuleScaffold itself exists (test_context.hpp).
template <typename ModuleT, typename WidgetT>
struct BoundGuard {
	ModuleT* m;
	WidgetT* mw;
	BoundGuard(ModuleT* m, WidgetT* mw) : m(m), mw(mw) {}
	~BoundGuard() { Test::unregisterModule(m, mw); }
	BoundGuard(const BoundGuard&) = delete;
	BoundGuard& operator=(const BoundGuard&) = delete;
};

// A neutral "somebody else's module" -- any Model differing from what modelSlug/realModelSlug
// name is enough to exercise the mismatch branch of the connection check.
TEST_CASE("connected reflects model match: 0 absent, 1 mismatch, 2 match", "[EightFace][connection]") {
	// EightFace.cpp:199-203: "connected" gates the entire process() body -- nothing in read/auto/
	// write mode runs unless it is 2, so this is the LED (and the module's whole behavior) the
	// manual's red/white triangle describes.
	Test::ModuleScaffold<EightFaceModule<8>> mods{createEightFaceModule};
	EightFaceModule<8>* m = mods.create("EightFace");

	SECTION("No expander connected: 0") {
		m->process(Test::makeProcessArgs(0));
		REQUIRE(m->connected == 0);
	}

	SECTION("Expander connected, modelSlug empty (never bound yet): treated as a match, 2") {
		// The "==\"\"" half of EightFace.cpp:202's condition: a fresh 8FACE with nothing saved yet
		// must still show green/connected against whatever is plugged in, or a user could never
		// perform the first Write.
		EightFaceModule<8>* other = mods.create("EightFace");
		EightFaceWidget* otherMw = Test::createWidget<EightFaceWidget>(other);
		Test::registerModule(other, otherMw);
		BoundGuard<EightFaceModule<8>, EightFaceWidget> guard(other, otherMw);
		m->leftExpander.moduleId = other->id;
		m->leftExpander.module = other;

		m->process(Test::makeProcessArgs(0));
		REQUIRE(m->connected == 2);
	}

	SECTION("Expander connected, model matches realPluginSlug/realModelSlug: 2") {
		EightFaceModule<8>* boundM = mods.create("EightFace");
		EightFaceWidget* boundMw = Test::createWidget<EightFaceWidget>(boundM);
		connectForTest(m, boundM, boundMw);
		BoundGuard<EightFaceModule<8>, EightFaceWidget> guard(boundM, boundMw);

		m->process(Test::makeProcessArgs(0));
		REQUIRE(m->connected == 2);
	}

	SECTION("Expander connected, model does not match: 1") {
		EightFaceModule<8>* boundM = mods.create("EightFace");
		EightFaceWidget* boundMw = Test::createWidget<EightFaceWidget>(boundM);
		connectForTest(m, boundM, boundMw);
		BoundGuard<EightFaceModule<8>, EightFaceWidget> guard(boundM, boundMw);
		// Claim the bound module is something else entirely -- the mismatch case. modelSlug must
		// also be non-empty, or EightFace.cpp:202's "modelSlug == \"\"" half of the OR always
		// short-circuits to a match regardless of realModelSlug/realPluginSlug.
		m->modelSlug = "SomethingElse";
		m->realModelSlug = "SomethingElse";

		m->process(Test::makeProcessArgs(0));
		REQUIRE(m->connected == 1);
	}
}

TEST_CASE("SIDE::LEFT vs RIGHT selects the correct expander", "[EightFace][connection]") {
	// EightFace.cpp:199: "Expander* exp = side == SIDE::LEFT ? &leftExpander : &rightExpander;" --
	// only the expander on the selected side is ever read, regardless of what is plugged into the
	// other side. leftM and rightM must be genuinely different Models (EightFace vs EightFaceX2)
	// so that "claim the other side's identity" is an actual mismatch rather than two instances of
	// the same model comparing equal.
	Test::ModuleScaffold<EightFaceModule<8>> mods8{createEightFaceModule};
	Test::ModuleScaffold<EightFaceModule<16>> mods16;
	EightFaceModule<8>* m = mods8.create("EightFace");
	EightFaceModule<8>* leftM = mods8.create("EightFace");
	EightFaceWidget* leftMw = Test::createWidget<EightFaceWidget>(leftM);
	EightFaceModule<16>* rightM = mods16.create("EightFaceX2");
	EightFaceX2Widget* rightMw = Test::createWidget<EightFaceX2Widget>(rightM);
	Test::registerModule(leftM, leftMw);
	Test::registerModule(rightM, rightMw);
	BoundGuard<EightFaceModule<8>, EightFaceWidget> leftGuard(leftM, leftMw);
	BoundGuard<EightFaceModule<16>, EightFaceX2Widget> rightGuard(rightM, rightMw);
	m->leftExpander.moduleId = leftM->id;
	m->leftExpander.module = leftM;
	m->rightExpander.moduleId = rightM->id;
	m->rightExpander.module = rightM;
	// modelSlug must be non-empty for the mismatch branch to actually compare models
	// (EightFace.cpp:202's "modelSlug == \"\"" half otherwise always matches).
	m->modelSlug = "nonempty";

	SECTION("SIDE::LEFT reads the left expander") {
		m->side = SIDE::LEFT;
		m->realModelSlug = leftM->model->slug;
		m->realPluginSlug = leftM->model->plugin->slug;
		m->process(Test::makeProcessArgs(0));
		REQUIRE(m->connected == 2);
		// Prove it is actually the left module being read: claiming the right module's identity
		// must now read as a mismatch, since the left module is what is actually being compared.
		m->realModelSlug = rightM->model->slug;
		m->realPluginSlug = rightM->model->plugin->slug;
		m->process(Test::makeProcessArgs(1));
		REQUIRE(m->connected == 1);
	}

	SECTION("SIDE::RIGHT reads the right expander") {
		m->side = SIDE::RIGHT;
		m->realModelSlug = rightM->model->slug;
		m->realPluginSlug = rightM->model->plugin->slug;
		m->process(Test::makeProcessArgs(0));
		REQUIRE(m->connected == 2);
		m->realModelSlug = leftM->model->slug;
		m->realPluginSlug = leftM->model->plugin->slug;
		m->process(Test::makeProcessArgs(1));
		REQUIRE(m->connected == 1);
	}
}

TEST_CASE("Preset LEDs: used, active-empty, beyond count, and current slot", "[EightFace][led]") {
	// EightFace.cpp:400-411's Read/Auto branch:
	//   green (index+1): bright when presetSlotUsed[i] AND i is not the current slot, dim (0.2)
	//                     when i < presetCount and unused, off when i >= presetCount or i is
	//                     current (the current slot shows blue instead, never green)
	//   blue  (index+2): the current slot only
	Test::ModuleScaffold<EightFaceModule<8>> mods{createEightFaceModule};
	EightFaceModule<8>* m = mods.create("EightFace");
	EightFaceModule<8>* boundM = mods.create("EightFace");
	EightFaceWidget* boundMw = Test::createWidget<EightFaceWidget>(boundM);
	connectForTest(m, boundM, boundMw);
	BoundGuard<EightFaceModule<8>, EightFaceWidget> guard(boundM, boundMw);

	m->presetCount = 4;
	m->preset = 1;
	// Slot 1: current -- shows blue, not green, regardless of presetSlotUsed.
	m->presetSlotUsed[1] = true;
	m->presetSlot[1] = json_pack("{s:i}", "id", 1);
	// Slot 3: used, not current -- bright green.
	m->presetSlotUsed[3] = true;
	m->presetSlot[3] = json_pack("{s:i}", "id", 3);
	// Slot 2: within presetCount, not current, never used -- dim.
	// Slot 5: beyond presetCount -- off.

	// lightDivider must fire once for the light-update block to run at all; setBrightnessSmooth()
	// jumps immediately on a brightness increase (Light.hpp), so no settling time is needed.
	for (uint32_t i = 0; i < m->lightDivider.getDivision() + 1; i++) {
		m->process(Test::makeProcessArgs(i));
	}

	REQUIRE(m->lights[EightFaceModule<8>::PRESET_LIGHT + 1 * 3 + 2].getBrightness() > 0.9f);  // current slot, blue
	REQUIRE(m->lights[EightFaceModule<8>::PRESET_LIGHT + 1 * 3 + 1].getBrightness() == 0.0f);  // current slot: no green
	REQUIRE(m->lights[EightFaceModule<8>::PRESET_LIGHT + 3 * 3 + 1].getBrightness() > 0.9f);  // used, not current: bright green
	REQUIRE(m->lights[EightFaceModule<8>::PRESET_LIGHT + 2 * 3 + 1].getBrightness() < 0.3f);  // active but empty, dim
	REQUIRE(m->lights[EightFaceModule<8>::PRESET_LIGHT + 2 * 3 + 1].getBrightness() > 0.0f);
	REQUIRE(m->lights[EightFaceModule<8>::PRESET_LIGHT + 5 * 3 + 1].getBrightness() == 0.0f);  // beyond count, off
}

TEST_CASE("EightFaceX2 (NUM_PRESETS == 16) behaves like EightFace across connection and sequencing", "[EightFace][x2]") {
	// v2.2.0 fixed a bug that hit ONLY 8FACEx2 -- the template
	// instantiation deserves its own coverage, not just an assumption that EightFaceModule<8>'s
	// tests generalize.
	Test::ModuleScaffold<EightFaceModule<16>> mods;
	EightFaceModule<16>* m = mods.create("EightFaceX2");
	REQUIRE(m->presetMax == 16);
	EightFaceModule<16>* boundM = mods.create("EightFaceX2");
	EightFaceX2Widget* boundMw = Test::createWidget<EightFaceX2Widget>(boundM);
	Test::registerModule(boundM, boundMw);
	BoundGuard<EightFaceModule<16>, EightFaceX2Widget> guard(boundM, boundMw);
	m->leftExpander.moduleId = boundM->id;
	m->leftExpander.module = boundM;
	m->pluginSlug = boundM->model->plugin->name;
	m->modelSlug = boundM->model->name;
	m->realPluginSlug = boundM->model->plugin->slug;
	m->realModelSlug = boundM->model->slug;

	m->process(Test::makeProcessArgs(0));
	REQUIRE(m->connected == 2);

	// A slot past 8FACE's own range (index 12) is reachable and behaves normally.
	m->presetSlotUsed[12] = true;
	m->presetSlot[12] = boundMw->toJson();
	m->presetCount = 16;
	m->preset = -1;
	m->presetPrev = -1;

	m->presetLoad(boundM, 12, false, true);
	m->dispatch.drain();

	REQUIRE(m->preset == 12);
}
