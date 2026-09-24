// TransitPad + Transit expander-chain tests: expander discovery/teardown,
// presetProcessXyPad interpolation, bindSnapshot, and the full XY-pad chain
// from CV/params through to Transit's bound-parameter output.
// Split out of a single TransitPad.test.module.hpp; see TransitPad.test.cpp
// for how this file is wired into the test binary.

// ============================================================
// Transit + TransitPad integration: process() interpolation
// ============================================================

// Helper module with parameters that Transit can bind and control
struct TestParamModule : rack::Module {
	enum ParamIds { PARAM_A, PARAM_B, NUM_PARAMS };
	TestParamModule() {
		config(NUM_PARAMS, 0, 0, 0);
		configParam(PARAM_A, 0.f, 1.f, 0.5f, "A");
		configParam(PARAM_B, 0.f, 1.f, 0.5f, "B");
	}
};

// Helper: wire Transit → TransitPad as right expander and let Transit discover it.
// Also forces presetProcessDivision=1 so the XY-pad result is written every frame.
// Wiring only — no stepping: Transit discovers the pad (moduleChangedFlag) on its
// next tick, which is always inside h.dspSteps()/r.run(). Stepping here would
// tick the pad before the test has set it up, computing weights from whatever
// geometry (position/radius/amount) happens to be live at that moment — only
// nodes below the *current* snapshotsUsed are ever recomputed on later ticks,
// so a premature weight computed for one of those indices before setup is done
// can persist. Expander-discovery tests add their own h.dspStep().
static void connectPad(Test::Harness& h, TransitModule<12>* transit, TransitPadModule<>* pad) {
	h.connectExpander(transit, pad);
	transit->setProcessDivision(1);
}

// Helper: bind a parameter and flush the task queue into sourceHandles.
// taskProcessorDsp.process() applies the bind synchronously, but the trailing
// Transit tick is still required: it runs the moduleChangedFlag discovery block,
// which initializes presetTotal — presetSave() dereferences getSlot() and SEGVs
// without it. The tick is Transit-only and direct (not h.dspStep()): the harness
// steps every registered module, and ticking the pad before the test has set it
// up would compute weights from whatever geometry happens to be live at that
// moment for the pad's currently-active nodes (see connectPad() above for why
// that can outlive the test's actual setup). Transit ignores args.frame, so this
// is behavior-identical to a harness step for Transit — same deliberate
// exception as MidiCatMem's pre-flip asserts.
static void bindParam(Test::Harness& h, TransitModule<12>* transit, int moduleId, int paramId) {
	(void)h;
	transit->bindAddParameterRequest(moduleId, paramId);
	transit->taskProcessorDsp.process();
	transit->process(Test::makeProcessArgs(0));
}

// Standard rig for the end-to-end tests: Transit + TransitPad expander + a
// target module whose PARAM_A Transit binds and drives. Call connectPad()
// after saving presets (same setup order as the tests below).
// The harness is borrowed from the test body (Harness is non-copyable, so the
// rig cannot own it); transit + pad are added pad-first so the harness ticks
// the pad before Transit (it steps in registration order), matching the old
// manual sequence where the pad computed snapshot weights before Transit read
// them. The target is adopted by the harness too, so all three share its
// exception-safe teardown.
struct PadRig {
	Test::Harness& h;
	TransitPadModule<>* pad = nullptr;
	TransitModule<12>* transit = nullptr;
	TestParamModule* target = nullptr;

	explicit PadRig(Test::Harness& h) : h(h) {}

	static PadRig make(Test::Harness& h) {
		PadRig r(h);
		r.pad = r.h.addModule<TransitPadModule<>>("TransitPad");
		r.transit = r.h.addModule<TransitModule<12>>("Transit");
		r.target = r.h.adoptModule(new TestParamModule);
		return r;
	}
	void bind() {
		bindParam(h, transit, target->id, TestParamModule::PARAM_A);
	}
	void save(int slot, float value) {
		target->params[TestParamModule::PARAM_A].setValue(value);
		transit->presetSave(slot);
	}
	float paramValue() {
		return target->params[TestParamModule::PARAM_A].getValue();
	}
	void run(int n) {
		h.dspSteps(n);
	}
};

// Connect the mix-position CV inputs (simulates cables)
static void connectMixInputs(TransitPadModule<>* pad) {
	pad->inputs[TransitPadModule<>::MIX_X_INPUT].channels = 1;
	pad->inputs[TransitPadModule<>::MIX_Y_INPUT].channels = 1;
}

// Drive the mix point to ((x+5)/10, (y+5)/10) — ±5V maps to the pad corners
static void setMixVoltage(TransitPadModule<>* pad, float xVolt, float yVolt) {
	pad->inputs[TransitPadModule<>::MIX_X_INPUT].setVoltage(xVolt);
	pad->inputs[TransitPadModule<>::MIX_Y_INPUT].setVoltage(yVolt);
}


TEST_CASE("Transit detects TransitPad as right expander", "[TransitPad][Transit]") {
	Test::Harness h;
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");

	// Flush initial expandersChanged so transitPad is properly initialised to nullptr
	h.dspStep();
	REQUIRE_FALSE(transit->isXyPadActive());

	connectPad(h, transit, pad);
	h.dspStep();

	REQUIRE(transit->isXyPadActive());
	// TransitPad sets masterModule back-pointer
	REQUIRE(pad->masterModule == transit);
}


TEST_CASE("Transit sets slotCvMode to OFF when TransitPad is connected", "[TransitPad][Transit]") {
	Test::Harness h;
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");

	transit->slotCvMode = SLOTCVMODE::TRIG_FWD;
	connectPad(h, transit, pad);
	h.dspStep();

	REQUIRE(transit->slotCvMode == SLOTCVMODE::OFF);
}


TEST_CASE("isXyPadActive(true) also requires the pad's own Pad-active switch", "[TransitPad][Transit]") {
	Test::Harness h;
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");

	// No pad connected yet: both forms agree.
	h.dspStep();
	REQUIRE_FALSE(transit->isXyPadActive());
	REQUIRE_FALSE(transit->isXyPadActive(true));

	// Pad connected and active (default): both forms agree again.
	connectPad(h, transit, pad);
	h.dspStep();
	REQUIRE(transit->isXyPadActive());
	REQUIRE(transit->isXyPadActive(true));

	// Pad connected but switched off: only the plain form still reports true.
	pad->params[TransitPadModule<>::ON_PARAM].setValue(0.f);
	h.dspStep();
	REQUIRE(transit->isXyPadActive());
	REQUIRE_FALSE(transit->isXyPadActive(true));
}


TEST_CASE("Transit disconnects from TransitPad when expander is removed", "[TransitPad][Transit]") {
	Test::Harness h;
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");

	connectPad(h, transit, pad);
	h.dspStep();
	REQUIRE(transit->isXyPadActive());

	// Disconnect
	h.disconnectExpander(transit, Test::Harness::SIDE_RIGHT);
	h.dspStep();

	REQUIRE_FALSE(transit->isXyPadActive());
}


TEST_CASE("presetProcessXyPad: single snapshot with full weight applies preset exactly", "[TransitPad][Transit]") {
	Test::Harness h;
	Test::ModuleScaffold<TransitPadModule<>> padMods;
	TransitPadModule<>* pad = padMods.create("TransitPad");
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");
	TestParamModule* target = h.adoptModule(new TestParamModule);

	// Bind target parameter and save preset 0 with value 0.25
	bindParam(h, transit, target->id, TestParamModule::PARAM_A);
	target->params[TestParamModule::PARAM_A].setValue(0.25f);
	transit->presetSave(0);

	// Connect pad, set snapshot 0 → preset slot 0, weight 1.0
	connectPad(h, transit, pad);
	pad->snapshots[0][0].id = 0;
	pad->snapshots[0][0].weight = 1.f;

	// Drive target param away so we can verify Transit writes it
	target->params[TestParamModule::PARAM_A].setValue(0.99f);
	h.dspSteps(5);

	REQUIRE(target->params[TestParamModule::PARAM_A].getValue() == Catch::Approx(0.25f).margin(0.001f));
}


TEST_CASE("isPadActive: defaults to true and tracks ON_PARAM", "[TransitPad]") {
	Test::Harness h;
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");

	REQUIRE(pad->isPadActive());

	pad->params[TransitPadModule<>::ON_PARAM].setValue(0.f);
	REQUIRE_FALSE(pad->isPadActive());

	pad->params[TransitPadModule<>::ON_PARAM].setValue(1.f);
	REQUIRE(pad->isPadActive());
}


TEST_CASE("presetProcessXyPad: switching Pad active off freezes the last blend", "[TransitPad][Transit]") {
	Test::Harness h;
	Test::ModuleScaffold<TransitPadModule<>> padMods;
	TransitPadModule<>* pad = padMods.create("TransitPad");
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");
	TestParamModule* target = h.adoptModule(new TestParamModule);

	bindParam(h, transit, target->id, TestParamModule::PARAM_A);
	target->params[TestParamModule::PARAM_A].setValue(0.25f);
	transit->presetSave(0);

	connectPad(h, transit, pad);
	pad->snapshots[0][0].id = 0;
	pad->snapshots[0][0].weight = 1.f;

	target->params[TestParamModule::PARAM_A].setValue(0.99f);
	h.dspSteps(5);
	REQUIRE(target->params[TestParamModule::PARAM_A].getValue() == Catch::Approx(0.25f).margin(0.001f));

	// Switch the pad off, then rebind a different snapshot to a different
	// preset and change its weight -- none of this should reach the target
	// param while the pad is disengaged, so the user can freely rework the
	// snapshots without disturbing the module's current sound.
	pad->params[TransitPadModule<>::ON_PARAM].setValue(0.f);
	h.dspStep();

	target->params[TestParamModule::PARAM_A].setValue(0.6f);
	transit->presetSave(1);
	pad->snapshots[0][0].id = 1;
	pad->snapshots[0][0].weight = 1.f;

	// Drive the target away from both presets; it must stay put since Transit
	// isn't reading the pad's weights at all right now.
	target->params[TestParamModule::PARAM_A].setValue(0.5f);
	h.dspSteps(5);
	REQUIRE(target->params[TestParamModule::PARAM_A].getValue() == Catch::Approx(0.5f).margin(0.001f));

	// Switching back on resumes blending from the pad's current (edited) state.
	pad->params[TransitPadModule<>::ON_PARAM].setValue(1.f);
	h.dspSteps(5);
	REQUIRE(target->params[TestParamModule::PARAM_A].getValue() == Catch::Approx(0.6f).margin(0.001f));
}


TEST_CASE("Pad active overrides Write mode: buttons don't save/clear and the blend still applies", "[TransitPad][Transit]") {
	Test::Harness h;
	Test::ModuleScaffold<TransitPadModule<>> padMods;
	TransitPadModule<>* pad = padMods.create("TransitPad");
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");
	TestParamModule* target = h.adoptModule(new TestParamModule);

	bindParam(h, transit, target->id, TestParamModule::PARAM_A);
	target->params[TestParamModule::PARAM_A].setValue(0.25f);
	transit->presetSave(0);
	REQUIRE(transit->getSlot(3)->isUsed() == false);

	connectPad(h, transit, pad);
	pad->snapshots[0][0].id = 0;
	pad->snapshots[0][0].weight = 1.f;
	h.dspSteps(5);

	// Switch TRANSIT's own front-panel mode to WRITE while the pad is active.
	transit->params[TransitModule<12>::PARAM_CTRLMODE].setValue((float)CTRLMODE::WRITE);
	h.dspStep();

	// A short press on slot 3's button would normally save the current param
	// value into that slot in WRITE mode -- while the pad is active this must
	// not happen, since the pad is meant to fully override Write mode.
	target->params[TestParamModule::PARAM_A].setValue(0.99f);
	transit->params[TransitModule<12>::PARAM_PRESET + 3].setValue(10.f);
	h.dspSteps(128);
	transit->params[TransitModule<12>::PARAM_PRESET + 3].setValue(0.f);
	h.dspSteps(128);
	REQUIRE(transit->getSlot(3)->isUsed() == false);

	// The pad's blend must still drive the target param despite CTRLMODE
	// sitting on WRITE, since only the pad's own ON/OFF switch controls this.
	h.dspSteps(5);
	REQUIRE(target->params[TestParamModule::PARAM_A].getValue() == Catch::Approx(0.25f).margin(0.001f));

	// Switching the pad off restores WRITE mode's normal button behavior.
	pad->params[TransitPadModule<>::ON_PARAM].setValue(0.f);
	h.dspStep();

	target->params[TestParamModule::PARAM_A].setValue(0.4f);
	transit->params[TransitModule<12>::PARAM_PRESET + 3].setValue(10.f);
	h.dspSteps(128);
	transit->params[TransitModule<12>::PARAM_PRESET + 3].setValue(0.f);
	h.dspSteps(128);
	REQUIRE(transit->getSlot(3)->isUsed() == true);
	REQUIRE(transit->getSlot(3)->getPreset()->at(0) == Catch::Approx(0.4f));
}


TEST_CASE("Pad active: slot button press doesn't start a fade or disturb the frozen blend", "[TransitPad][Transit]") {
	// Regression test: while the pad is active, effCtrlMode is forced to READ
	// regardless of the front-panel switch, so a slot-button press used to fall
	// through to presetLoad() (the Read/Auto branch) and set processing=true,
	// starting a fade that raced presetProcessXyPad on the shared divider --
	// whichever process function won the race, the button press corrupted the
	// pad's blend, and once the pad was switched off the leftover fade played
	// out instead of leaving the parameter at its last blended value.
	Test::Harness h;
	Test::ModuleScaffold<TransitPadModule<>> padMods;
	TransitPadModule<>* pad = padMods.create("TransitPad");
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");
	TestParamModule* target = h.adoptModule(new TestParamModule);

	bindParam(h, transit, target->id, TestParamModule::PARAM_A);
	target->params[TestParamModule::PARAM_A].setValue(0.25f);
	transit->presetSave(0);
	target->params[TestParamModule::PARAM_A].setValue(0.75f);
	transit->presetSave(3);
	// presetSave(p) also sets preset = p as a side effect; reset it to -1 so the
	// button press below is the only thing that could change it to 3.
	transit->preset = -1;

	connectPad(h, transit, pad);
	pad->snapshots[0][0].id = 0;
	pad->snapshots[0][0].weight = 1.f;
	h.dspSteps(5);
	REQUIRE(target->params[TestParamModule::PARAM_A].getValue() == Catch::Approx(0.25f).margin(0.001f));
	REQUIRE_FALSE(transit->processing);

	// Habit press: user presses slot 3's button expecting a normal Write- or
	// Read-mode action (whatever the front-panel switch happens to say).
	transit->params[TransitModule<12>::PARAM_PRESET + 3].setValue(10.f);
	h.dspSteps(128);
	transit->params[TransitModule<12>::PARAM_PRESET + 3].setValue(0.f);
	h.dspSteps(128);

	// No fade was started, and the pad's blend is undisturbed.
	REQUIRE_FALSE(transit->processing);
	REQUIRE(transit->preset != 3);
	h.dspSteps(5);
	REQUIRE(target->params[TestParamModule::PARAM_A].getValue() == Catch::Approx(0.25f).margin(0.001f));

	// Switching the pad off must leave the parameter at the frozen blend, not
	// resume/play out some leftover fade toward slot 3.
	pad->params[TransitPadModule<>::ON_PARAM].setValue(0.f);
	h.dspSteps(10);
	REQUIRE(target->params[TestParamModule::PARAM_A].getValue() == Catch::Approx(0.25f).margin(0.001f));
}


TEST_CASE("LIGHT_CV stops blinking while the pad is active, resumes once it's switched off", "[TransitPad][Transit]") {
	Test::Harness h;
	Test::ModuleScaffold<TransitPadModule<>> padMods;
	TransitPadModule<>* pad = padMods.create("TransitPad");
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");

	// Connecting the pad forces slotCvMode to OFF, which would otherwise make
	// LIGHT_CV blink continuously as a "SEL-mode disabled" indicator -- that's
	// only a meaningful indication while the pad isn't actually overriding
	// everything itself.
	connectPad(h, transit, pad);
	h.dspStep();
	REQUIRE(transit->slotCvMode == SLOTCVMODE::OFF);

	// Run well past several blink half-periods (lightBlinkSlow toggles every
	// ~0.4s); brightness must stay at 0 throughout, not just off-phase.
	for (int i = 0; i < 20; i++) {
		h.dspSteps((int)(h.sampleRate() * 0.1f));
		REQUIRE(transit->lights[TransitModule<12>::LIGHT_CV].getBrightness() == 0.f);
	}

	// Switching the pad off must let the blink resume.
	pad->params[TransitPadModule<>::ON_PARAM].setValue(0.f);
	h.dspStep();

	bool sawLit = false;
	for (int i = 0; i < 20 && !sawLit; i++) {
		h.dspSteps((int)(h.sampleRate() * 0.1f));
		if (transit->lights[TransitModule<12>::LIGHT_CV].getBrightness() > 0.f) sawLit = true;
	}
	REQUIRE(sawLit);
}


TEST_CASE("presetProcessXyPad: two equal-weight snapshots produce the midpoint", "[TransitPad][Transit]") {
	Test::Harness h;
	Test::ModuleScaffold<TransitPadModule<>> padMods;
	TransitPadModule<>* pad = padMods.create("TransitPad");
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");
	TestParamModule* target = h.adoptModule(new TestParamModule);

	bindParam(h, transit, target->id, TestParamModule::PARAM_A);

	// Save preset 0 = 0.2, preset 1 = 0.8
	target->params[TestParamModule::PARAM_A].setValue(0.2f);
	transit->presetSave(0);
	target->params[TestParamModule::PARAM_A].setValue(0.8f);
	transit->presetSave(1);

	connectPad(h, transit, pad);
	// snapshots[0][0] → slot 0, snapshots[0][1] → slot 1 (ids set by initExtra)
	pad->snapshots[0][0].weight = 1.f;
	pad->snapshots[0][1].weight = 1.f;

	h.dspSteps(5);

	// (0.2 * 1 + 0.8 * 1) / (1 + 1) = 0.5
	REQUIRE(target->params[TestParamModule::PARAM_A].getValue() == Catch::Approx(0.5f).margin(0.001f));
}


TEST_CASE("presetProcessXyPad: unequal weights produce correctly weighted average", "[TransitPad][Transit]") {
	Test::Harness h;
	Test::ModuleScaffold<TransitPadModule<>> padMods;
	TransitPadModule<>* pad = padMods.create("TransitPad");
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");
	TestParamModule* target = h.adoptModule(new TestParamModule);

	bindParam(h, transit, target->id, TestParamModule::PARAM_A);

	// preset 0 = 0.0, preset 1 = 1.0
	target->params[TestParamModule::PARAM_A].setValue(0.f);
	transit->presetSave(0);
	target->params[TestParamModule::PARAM_A].setValue(1.f);
	transit->presetSave(1);

	connectPad(h, transit, pad);
	// Weight 1:3 toward preset 1
	pad->snapshots[0][0].weight = 1.f;
	pad->snapshots[0][1].weight = 3.f;

	h.dspSteps(5);

	// (0.0 * 1 + 1.0 * 3) / (1 + 3) = 0.75
	REQUIRE(target->params[TestParamModule::PARAM_A].getValue() == Catch::Approx(0.75f).margin(0.001f));
}


TEST_CASE("presetProcessXyPad: snapshot with id=-1 is skipped", "[TransitPad][Transit]") {
	Test::Harness h;
	Test::ModuleScaffold<TransitPadModule<>> padMods;
	TransitPadModule<>* pad = padMods.create("TransitPad");
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");
	TestParamModule* target = h.adoptModule(new TestParamModule);

	bindParam(h, transit, target->id, TestParamModule::PARAM_A);

	// Only preset 1 saved
	target->params[TestParamModule::PARAM_A].setValue(0.7f);
	transit->presetSave(1);

	connectPad(h, transit, pad);
	// snapshot 0: id=-1 (unbound), snapshot 1: id=1 with full weight
	pad->snapshots[0][0].id = -1;
	pad->snapshots[0][0].weight = 1.f; // weight set but id is -1 → ignored
	pad->snapshots[0][1].weight = 1.f; // this one should take effect

	h.dspSteps(5);

	REQUIRE(target->params[TestParamModule::PARAM_A].getValue() == Catch::Approx(0.7f).margin(0.001f));
}


TEST_CASE("presetProcessXyPad: snapshot pointing to unused slot is skipped", "[TransitPad][Transit]") {
	Test::Harness h;
	Test::ModuleScaffold<TransitPadModule<>> padMods;
	TransitPadModule<>* pad = padMods.create("TransitPad");
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");
	TestParamModule* target = h.adoptModule(new TestParamModule);

	bindParam(h, transit, target->id, TestParamModule::PARAM_A);

	// Only preset 1 saved; preset 0 is empty
	target->params[TestParamModule::PARAM_A].setValue(0.6f);
	transit->presetSave(1);

	// Set a sentinel value to detect if the param gets written
	target->params[TestParamModule::PARAM_A].setValue(0.42f);

	connectPad(h, transit, pad);
	// snapshot 0 → empty slot 0 (not saved), weight 1.0 → should be skipped
	// snapshot 1 → slot 1 with weight 0 → skipped too
	// Total weight = 0 → no write → param stays at 0.42
	pad->snapshots[0][0].weight = 1.f; // points at slot 0 which is unused

	h.dspSteps(5);

	REQUIRE(target->params[TestParamModule::PARAM_A].getValue() == Catch::Approx(0.42f).margin(0.001f));
}


TEST_CASE("presetProcessXyPad: all zero weights leave parameters unchanged", "[TransitPad][Transit]") {
	Test::Harness h;
	Test::ModuleScaffold<TransitPadModule<>> padMods;
	TransitPadModule<>* pad = padMods.create("TransitPad");
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");
	TestParamModule* target = h.adoptModule(new TestParamModule);

	bindParam(h, transit, target->id, TestParamModule::PARAM_A);

	target->params[TestParamModule::PARAM_A].setValue(0.3f);
	transit->presetSave(0);

	target->params[TestParamModule::PARAM_A].setValue(0.55f);

	connectPad(h, transit, pad);
	// All snapshot weights remain 0 (initialized that way in initExtra)

	h.dspSteps(5);

	// No write should occur → param stays at 0.55
	REQUIRE(target->params[TestParamModule::PARAM_A].getValue() == Catch::Approx(0.55f).margin(0.001f));
}


TEST_CASE("presetProcessXyPad: switching TransitPad sets changes interpolation output", "[TransitPad][Transit]") {
	Test::Harness h;
	Test::ModuleScaffold<TransitPadModule<>> padMods;
	TransitPadModule<>* pad = padMods.create("TransitPad");
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");
	TestParamModule* target = h.adoptModule(new TestParamModule);

	bindParam(h, transit, target->id, TestParamModule::PARAM_A);

	// preset 0 = 0.1, preset 1 = 0.9
	target->params[TestParamModule::PARAM_A].setValue(0.1f);
	transit->presetSave(0);
	target->params[TestParamModule::PARAM_A].setValue(0.9f);
	transit->presetSave(1);

	connectPad(h, transit, pad);

	// Set 0: snapshot 0 → preset 0, weight 1.0
	pad->snapshots[0][0].weight = 1.f;
	// Set 1: snapshot 1 → preset 1, weight 1.0
	pad->snapshots[1][1].weight = 1.f;

	// Activate set 0
	pad->currentSet = 0;
	h.dspSteps(5);
	REQUIRE(target->params[TestParamModule::PARAM_A].getValue() == Catch::Approx(0.1f).margin(0.001f));

	// Activate set 1
	pad->currentSet = 1;
	h.dspSteps(5);
	REQUIRE(target->params[TestParamModule::PARAM_A].getValue() == Catch::Approx(0.9f).margin(0.001f));
}


TEST_CASE("presetProcessXyPad: interpolates two bound parameters independently", "[TransitPad][Transit]") {
	Test::Harness h;
	Test::ModuleScaffold<TransitPadModule<>> padMods;
	TransitPadModule<>* pad = padMods.create("TransitPad");
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");
	TestParamModule* target = h.adoptModule(new TestParamModule);

	bindParam(h, transit, target->id, TestParamModule::PARAM_A);
	bindParam(h, transit, target->id, TestParamModule::PARAM_B);

	// preset 0: A=0.2, B=0.8 / preset 1: A=0.6, B=0.4
	target->params[TestParamModule::PARAM_A].setValue(0.2f);
	target->params[TestParamModule::PARAM_B].setValue(0.8f);
	transit->presetSave(0);

	target->params[TestParamModule::PARAM_A].setValue(0.6f);
	target->params[TestParamModule::PARAM_B].setValue(0.4f);
	transit->presetSave(1);

	connectPad(h, transit, pad);
	// Equal weights → midpoint for both params
	pad->snapshots[0][0].weight = 1.f;
	pad->snapshots[0][1].weight = 1.f;

	h.dspSteps(5);

	REQUIRE(target->params[TestParamModule::PARAM_A].getValue() == Catch::Approx(0.4f).margin(0.001f));
	REQUIRE(target->params[TestParamModule::PARAM_B].getValue() == Catch::Approx(0.6f).margin(0.001f));
}


// ============================================================
// End-to-end signal chain:
// mix position (CV/sequence) → dist[] → radius/amount → weight → Transit param
// Unlike the tests above, the weights are never assigned directly — they are
// computed by pad->process() from the mix-position inputs.
// ============================================================

TEST_CASE("XY-pad chain: mix position CV drives the target parameter between presets", "[TransitPad][Transit]") {
	Test::Harness h;
	PadRig r = PadRig::make(h);
	r.bind();
	r.save(0, 0.0f);
	r.save(1, 1.0f);
	connectPad(h, r.transit, r.pad);

	// Default layout: snapshot A at (0,0) bound to slot 0, B at (1,0) bound to slot 1
	r.pad->snapshotsUsed = 2;
	connectMixInputs(r.pad);

	// Mix point on corner A → only preset 0 contributes
	setMixVoltage(r.pad, -5.f, -5.f);
	r.run(5);
	REQUIRE(r.paramValue() == Catch::Approx(0.0f).margin(0.001f));

	// Mix point on corner B → only preset 1 contributes
	setMixVoltage(r.pad, 5.f, -5.f);
	r.run(5);
	REQUIRE(r.paramValue() == Catch::Approx(1.0f).margin(0.001f));

	// Mix point halfway between them → equal weights → midpoint
	setMixVoltage(r.pad, 0.f, -5.f);
	r.run(5);
	REQUIRE(r.paramValue() == Catch::Approx(0.5f).margin(0.001f));
}


TEST_CASE("XY-pad chain: amount scales snapshot weight and shifts the blend", "[TransitPad][Transit]") {
	Test::Harness h;
	PadRig r = PadRig::make(h);
	r.bind();
	r.save(0, 0.0f);
	r.save(1, 1.0f);
	connectPad(h, r.transit, r.pad);

	// Both snapshots equidistant (0.5) from the mix point at (0.5, 0)
	r.pad->snapshotsUsed = 2;
	connectMixInputs(r.pad);
	setMixVoltage(r.pad, 0.f, -5.f);

	// Default amount 1.0: equal weights → midpoint blend
	r.run(5);
	REQUIRE(r.pad->snapshots[0][0].weight == Catch::Approx(0.55f).margin(0.001f));
	REQUIRE(r.pad->snapshots[0][1].weight == Catch::Approx(0.55f).margin(0.001f));
	REQUIRE(r.paramValue() == Catch::Approx(0.5f).margin(0.001f));

	// Halving snapshot B's amount halves its weight and pulls the blend toward A:
	// (0 * 0.55 + 1 * 0.275) / (0.55 + 0.275) = 1/3
	r.pad->nodes.setAmountImmediate(1, 0.5f);
	r.run(5);
	REQUIRE(r.pad->snapshots[0][1].weight == Catch::Approx(0.275f).margin(0.001f));
	REQUIRE(r.paramValue() == Catch::Approx(1.f / 3.f).margin(0.001f));
}


TEST_CASE("XY-pad chain: radius cuts off snapshot contribution at the boundary", "[TransitPad][Transit]") {
	Test::Harness h;
	PadRig r = PadRig::make(h);
	r.bind();
	r.save(0, 0.25f);
	connectPad(h, r.transit, r.pad);

	// Snapshot A at (0,0); the mix point moves along the x-axis so dist == mix.x
	// (X voltage → mix.x = v/10 + 0.5)
	r.pad->snapshotsUsed = 1;
	connectMixInputs(r.pad);
	setMixVoltage(r.pad, 0.f, -5.f);

	// Default radius 1.0: dist 0.5 is well inside
	r.run(5);
	REQUIRE(r.pad->snapshots[0][0].weight == Catch::Approx(0.55f).margin(0.001f));

	// Shrinking the radius to 0.6 shrinks the weight at the same point
	r.pad->nodes.setRadiusImmediate(0, 0.6f);
	r.run(5);
	REQUIRE(r.pad->snapshots[0][0].weight == Catch::Approx((0.6f - 0.5f) / 0.6f * 1.1f).margin(0.001f));

	// Outside the radius the weight is exactly zero and nothing is written
	setMixVoltage(r.pad, 2.f, -5.f);
	r.run(5);
	REQUIRE(r.pad->snapshots[0][0].weight == 0.f);
	r.target->params[TestParamModule::PARAM_A].setValue(0.9f);
	r.run(5);
	REQUIRE(r.paramValue() == Catch::Approx(0.9f).margin(0.001f));

	// Back inside the radius the preset value takes over again
	setMixVoltage(r.pad, 0.f, -5.f);
	r.run(5);
	REQUIRE(r.paramValue() == Catch::Approx(0.25f).margin(0.001f));
}


TEST_CASE("XY-pad chain: switching sets via button and CV changes the Transit output", "[TransitPad][Transit]") {
	Test::Harness h;
	PadRig r = PadRig::make(h);
	r.bind();
	r.save(0, 0.0f);
	r.save(1, 1.0f);
	connectPad(h, r.transit, r.pad);

	// Snapshot A sits near the mix point with a nonzero weight in every set;
	// which preset it reaches depends on the per-set binding
	r.pad->snapshotsUsed = 1;

	// Set 0 keeps the default binding to slot 0
	r.run(5);
	REQUIRE(r.paramValue() == Catch::Approx(0.0f).margin(0.001f));

	// Switch to set 1 via button, then rebind snapshot A to slot 1 there
	r.pad->params[TransitPadModule<>::SET_PARAM + 1].setValue(1.f);
	r.run(100);
	REQUIRE(r.pad->currentSet == 1);
	r.pad->bindSnapshot(0, 1);
	r.pad->params[TransitPadModule<>::SET_PARAM + 1].setValue(0.f);
	r.run(5);
	REQUIRE(r.paramValue() == Catch::Approx(1.0f).margin(0.001f));

	// Back to set 0 via button
	r.pad->params[TransitPadModule<>::SET_PARAM + 0].setValue(1.f);
	r.run(100);
	REQUIRE(r.pad->currentSet == 0);
	r.pad->params[TransitPadModule<>::SET_PARAM + 0].setValue(0.f);
	r.run(5);
	REQUIRE(r.paramValue() == Catch::Approx(0.0f).margin(0.001f));

	// Set selection via CV in VOLT mode: 1.25V → set 1, 0V → set 0
	r.pad->setCvMode = SETCVMODE::VOLT;
	r.pad->inputs[TransitPadModule<>::SET_CV_INPUT].channels = 1;
	r.pad->inputs[TransitPadModule<>::SET_CV_INPUT].setVoltage(1.25f);
	r.run(5);
	REQUIRE(r.pad->currentSet == 1);
	REQUIRE(r.paramValue() == Catch::Approx(1.0f).margin(0.001f));

	r.pad->inputs[TransitPadModule<>::SET_CV_INPUT].setVoltage(0.f);
	r.run(5);
	REQUIRE(r.pad->currentSet == 0);
	REQUIRE(r.paramValue() == Catch::Approx(0.0f).margin(0.001f));
}


TEST_CASE("XY-pad chain: motion sequence drives the mix position", "[TransitPad][Transit]") {
	Test::Harness h;
	PadRig r = PadRig::make(h);

	SECTION("Phase input sweeps the mix point along the sequence") {
		r.bind();
		// Snapshot A at (0,0) → slot 0, C at (1,1) → slot 2; slot 1 stays unused
		r.save(0, 0.0f);
		r.save(2, 1.0f);
		connectPad(h, r.transit, r.pad);
		r.pad->snapshotsUsed = 3;

		// Two-point linear sequence along the A→C diagonal
		r.pad->seqData[0][0].length = 2;
		r.pad->seqData[0][0].x[0] = 0.f; r.pad->seqData[0][0].y[0] = 0.f;
		r.pad->seqData[0][0].x[1] = 1.f; r.pad->seqData[0][0].y[1] = 1.f;

		r.pad->inputs[TransitPadModule<>::SEQ_PH_INPUT].channels = 1;

		r.pad->inputs[TransitPadModule<>::SEQ_PH_INPUT].setVoltage(0.f);
		r.run(5);
		REQUIRE(r.paramValue() == Catch::Approx(0.0f).margin(0.001f));

		r.pad->inputs[TransitPadModule<>::SEQ_PH_INPUT].setVoltage(10.f);
		r.run(5);
		REQUIRE(r.paramValue() == Catch::Approx(1.0f).margin(0.001f));

		r.pad->inputs[TransitPadModule<>::SEQ_PH_INPUT].setVoltage(5.f);
		r.run(5);
		REQUIRE(r.paramValue() == Catch::Approx(0.5f).margin(0.001f));
	}

	SECTION("Sequence-select input advances to the next sequence") {
		// Sequence 0 starts at (0,0), sequence 1 at (1,0)
		r.pad->seqData[0][0].length = 2;
		r.pad->seqData[0][0].x[0] = 0.f; r.pad->seqData[0][0].y[0] = 0.f;
		r.pad->seqData[0][0].x[1] = 1.f; r.pad->seqData[0][0].y[1] = 1.f;
		r.pad->seqData[0][1].length = 2;
		r.pad->seqData[0][1].x[0] = 1.f; r.pad->seqData[0][1].y[0] = 0.f;
		r.pad->seqData[0][1].x[1] = 1.f; r.pad->seqData[0][1].y[1] = 1.f;

		r.pad->inputs[TransitPadModule<>::SEQ_PH_INPUT].channels = 1;
		r.pad->inputs[TransitPadModule<>::SEQ_PH_INPUT].setVoltage(0.f);
		r.h.dspSteps(5);
		REQUIRE(r.pad->params[TransitPadModule<>::OUT_X_POS].getValue() == Catch::Approx(0.f).margin(0.001f));
		REQUIRE(r.pad->params[TransitPadModule<>::OUT_Y_POS].getValue() == Catch::Approx(0.f).margin(0.001f));

		fireTrigger(r.h, r.pad, TransitPadModule<>::SEQ_INPUT);
		REQUIRE(r.pad->seqSelected[0] == 1);

		r.h.dspSteps(5);
		REQUIRE(r.pad->params[TransitPadModule<>::OUT_X_POS].getValue() == Catch::Approx(1.f).margin(0.001f));
		REQUIRE(r.pad->params[TransitPadModule<>::OUT_Y_POS].getValue() == Catch::Approx(0.f).margin(0.001f));
	}
}


TEST_CASE("XY-pad chain: snapshotsUsed bounds which snapshots contribute weight", "[TransitPad][Transit]") {
	Test::Harness h;
	PadRig r = PadRig::make(h);
	r.bind();
	// Slots 0,1 = 0.0 and slots 2,3 = 1.0; snapshots A–D sit at the four
	// corners, all equidistant from the mix point at the centre (0.5, 0.5)
	r.save(0, 0.0f);
	r.save(1, 0.0f);
	r.save(2, 1.0f);
	r.save(3, 1.0f);
	connectPad(h, r.transit, r.pad);

	// Only A/B are active, so only they contribute: both hold 0.0
	r.pad->snapshotsUsed = 2;
	r.run(5);
	REQUIRE(r.paramValue() == Catch::Approx(0.0f).margin(0.001f));

	// Raising the count lets C/D join the blend
	r.pad->snapshotsUsed = 4;
	r.run(5);
	REQUIRE(r.paramValue() == Catch::Approx(0.5f).margin(0.001f));

	// ...and lowering it again drops them back out. This is the direction that
	// used to be broken: a snapshot that had already earned a weight kept it
	// forever, so C/D went on blending after the user shrank the pad and they
	// were no longer drawn or draggable.
	r.pad->snapshotsUsed = 2;
	r.run(5);
	REQUIRE(r.paramValue() == Catch::Approx(0.0f).margin(0.001f));
}


// Regression: lowering "Number of snapshots" must stop the now-inactive pad
// points from contributing. Before the fix, process() only ever wrote weights
// for j < snapshotsUsed, so a snapshot that had earned a weight while the count
// was high kept that weight indefinitely — presetProcessXyPad iterates all of
// getPadFactors(), not just the active prefix, so TRANSIT kept blending a pad
// point that had vanished from the screen.
TEST_CASE("Lowering snapshotsUsed clears the weights of the now-inactive snapshots", "[TransitPad][Transit]") {
	Test::Harness h;
	PadRig r = PadRig::make(h);
	r.bind();
	r.save(0, 0.0f);
	r.save(4, 1.0f);
	connectPad(h, r.transit, r.pad);

	// Pad point A (bound to slot 0, value 0.0) and pad point E (bound to slot 4,
	// value 1.0) both sit exactly on the mix point, so both reach full weight
	// and the blend lands halfway between the two presets.
	r.pad->snapshotsUsed = 8;
	r.pad->snapshots[r.pad->currentSet][4].id = 4;
	r.pad->nodes.setXyImmediate(0, 0.5f, 0.5f);
	r.pad->nodes.setXyImmediate(4, 0.5f, 0.5f);
	r.run(20);
	REQUIRE(r.pad->snapshots[r.pad->currentSet][4].weight == Catch::Approx(1.0f).margin(0.001f));
	REQUIRE(r.paramValue() == Catch::Approx(0.5f).margin(0.001f));

	// Shrink the pad to A/B only. E is no longer an active pad point, so its
	// weight must be cleared and the blend must fall back to A alone.
	r.pad->snapshotsUsed = 2;
	r.run(20);
	REQUIRE(r.pad->snapshots[r.pad->currentSet][4].weight == 0.f);
	REQUIRE(r.paramValue() == Catch::Approx(0.0f).margin(0.001f));

	// Same via the patch-load path: dataFromJson writes snapshotsUsed directly,
	// so it has to be covered by the same clearing and not only by the menu.
	r.pad->snapshotsUsed = 8;
	r.run(20);
	REQUIRE(r.paramValue() == Catch::Approx(0.5f).margin(0.001f));

	json_t* rootJ = r.pad->dataToJson();
	json_object_set_new(rootJ, "snapshotsUsed", json_integer(2));
	r.pad->dataFromJson(rootJ);
	json_decref(rootJ);
	r.run(20);
	REQUIRE(r.pad->snapshots[r.pad->currentSet][4].weight == 0.f);
	REQUIRE(r.paramValue() == Catch::Approx(0.0f).margin(0.001f));
}


TEST_CASE("bindSnapshot binds and unbinds pad points to Transit slots", "[TransitPad]") {
	Test::Harness h;
	SECTION("Binding semantics") {
		TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");

		// Defaults: snapshots A–D bound to slot indexes 0–3, E–H unbound
		REQUIRE(m->snapshots[0][0].id == 0);
		REQUIRE(m->snapshots[0][3].id == 3);
		REQUIRE(m->snapshots[0][4].id == -1);

		m->bindSnapshot(4, 7);
		REQUIRE(m->snapshots[0][4].id == 7);

		// -1 unbinds
		m->bindSnapshot(4, -1);
		REQUIRE(m->snapshots[0][4].id == -1);

		// Binding applies to the current set only
		m->currentSet = 2;
		m->bindSnapshot(0, 6);
		REQUIRE(m->snapshots[2][0].id == 6);
		REQUIRE(m->snapshots[0][0].id == 0);
	}

	SECTION("Bound snapshot drives the Transit output; unbinding stops it") {
		Test::Harness h;
		PadRig r = PadRig::make(h);
		r.bind();
		r.save(5, 0.77f);
		connectPad(h, r.transit, r.pad);

		// Park the mix point on snapshot A (weight saturates at 1.0)
		r.pad->snapshotsUsed = 1;
		connectMixInputs(r.pad);
		setMixVoltage(r.pad, -5.f, -5.f);

		r.pad->bindSnapshot(0, 5);
		r.run(5);
		REQUIRE(r.paramValue() == Catch::Approx(0.77f).margin(0.001f));

		// Unbinding removes the last contribution → no write happens
		r.target->params[TestParamModule::PARAM_A].setValue(0.42f);
		r.pad->bindSnapshot(0, -1);
		r.run(5);
		REQUIRE(r.paramValue() == Catch::Approx(0.42f).margin(0.001f));
	}
}


// getItemLabel() drives the node-menu title/tooltip (getItemName()) and the
// set-button menu's per-snapshot label list -- neither of its four outputs
// had a direct test.
TEST_CASE("getItemLabel reports the right text for all four states", "[TransitPad]") {
	SECTION("No Transit connected") {
		Test::Harness h;
		TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");
		REQUIRE(m->masterModule == nullptr);

		REQUIRE(m->getItemLabel(0, 0) == "<No TRANSIT module>");
	}

	SECTION("Bound snapshot, slot has a custom label") {
		Test::Harness h;
		TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");
		TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
		connectPad(h, transit, pad);
		h.dspStep();

		// Snapshot A defaults to slot 0; give that slot a custom label.
		transit->textLabel[0] = "Verse";
		REQUIRE(pad->snapshots[pad->currentSet][0].id == 0);

		REQUIRE(pad->getItemLabel(pad->currentSet, 0) == "Snapshot #1: Verse");
	}

	SECTION("Bound snapshot, slot has no custom label") {
		Test::Harness h;
		TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");
		TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
		connectPad(h, transit, pad);
		h.dspStep();

		REQUIRE(transit->textLabel[0] == "");
		REQUIRE(pad->snapshots[pad->currentSet][0].id == 0);

		REQUIRE(pad->getItemLabel(pad->currentSet, 0) == "Snapshot #1");
	}

	SECTION("Unbound pad point") {
		Test::Harness h;
		TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");
		TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
		connectPad(h, transit, pad);
		h.dspStep();

		// Snapshot E (index 4) is unbound by default.
		REQUIRE(pad->snapshots[pad->currentSet][4].id == -1);

		REQUIRE(pad->getItemLabel(pad->currentSet, 4) == "No snapshot");
	}
}


// TransitPad has exactly one cursor (the Out point), always at id 0; confirm
// an out-of-range id is a silent no-op rather than acting as if it addressed
// Out, since a stray id reaching storage would otherwise corrupt it.

TEST_CASE("setCursorXyImmediate with an out-of-range id is a silent no-op", "[TransitPad]") {
	Test::Harness h;
	TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");

	m->setCursorXyImmediate(0, 0.2f, 0.3f);
	float xBefore = m->params[TransitPadModule<>::OUT_X_POS].getValue();
	float yBefore = m->params[TransitPadModule<>::OUT_Y_POS].getValue();

	REQUIRE_NOTHROW(m->setCursorXyImmediate(1, 0.9f, 0.9f));

	REQUIRE(m->params[TransitPadModule<>::OUT_X_POS].getValue() == Catch::Approx(xBefore));
	REQUIRE(m->params[TransitPadModule<>::OUT_Y_POS].getValue() == Catch::Approx(yBefore));
}

TEST_CASE("setCursorXyFiltered with an out-of-range id is a silent no-op", "[TransitPad]") {
	Test::Harness h;
	TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");

	m->setCursorXyImmediate(0, 0.2f, 0.3f);
	float xBefore = m->outUiX;
	float yBefore = m->outUiY;

	REQUIRE_NOTHROW(m->setCursorXyFiltered(1, 0.9f, 0.9f));

	REQUIRE(m->outUiX == Catch::Approx(xBefore));
	REQUIRE(m->outUiY == Catch::Approx(yBefore));
}

TEST_CASE("XyScreenNodes setters with an out-of-range id are a silent no-op", "[TransitPad]") {
	Test::Harness h;
	// The node side of the same bound (COUNT, i.e. SNAPSHOTS here) predates
	// this stage — XyScreenNodes has always guarded on its own COUNT — but
	// had no direct test. Cover it alongside the cursor-side fix above.
	TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");

	m->nodes.setRadiusImmediate(0, 0.4f);
	m->nodes.setAmountImmediate(0, 0.6f);

	float radius0Before = m->nodes.radiusUi[0];
	float amount0Before = m->nodes.amountUi[0];
	float x0Before = m->nodes.uiX[0];

	REQUIRE_NOTHROW(m->nodes.setXyImmediate(8, 0.9f, 0.9f));
	REQUIRE_NOTHROW(m->nodes.setRadiusImmediate(8, 0.9f));
	REQUIRE_NOTHROW(m->nodes.setAmountImmediate(8, 0.9f));

	REQUIRE(m->nodes.uiX[0] == Catch::Approx(x0Before));
	REQUIRE(m->nodes.radiusUi[0] == Catch::Approx(radius0Before));
	REQUIRE(m->nodes.amountUi[0] == Catch::Approx(amount0Before));
}


// bindAddParameterRequest(presetLoading = true) skips
// the back-fill loop that keeps every slot's preset vector in sync with
// sourceHandles, so an older slot's preset can end up shorter than
// sourceHandles. presetProcessXyPad indexed that short vector by i
// unguarded (heap-buffer-overflow under ASan); presetProcess already had
// the `size() <= i` guard. Drive the real sequence rather than
// hand-shortening the vector, so the test tracks the actual patch-load
// code path. The sibling presetProcessPhase regression lives in
// Transit.test.cpp, since neither site is pad-specific but this one needs
// a TransitPad expander to reach.
// The out-of-bounds read does not reliably abort under ASan in this harness
// (confirmed on the sibling presetProcessPhase case: the redzone byte is
// genuinely poisoned per __asan_address_is_poisoned, but the generated
// check at this call site does not trip, unlike an isolated repro of the
// same pattern). So this asserts the documented contract behaviourally: the
// second (newer) parameter must never be written while it lacks a
// same-sized preset entry, and it is given a sentinel value no crossfade of
// target's values could ever produce.

TEST_CASE("presetProcessXyPad does not write a param whose preset is shorter than sourceHandles", "[TransitPad][Transit]") {
	Test::Harness h;
	PadRig r = PadRig::make(h);
	TestParamModule* target2 = h.adoptModule(new TestParamModule);

	// Bind one param, save slot 0: sourceHandles.size() == 1, preset[0].size() == 1
	r.bind();
	r.save(0, 0.5f);
	connectPad(h, r.transit, r.pad);

	// Bind a second param with presetLoading = true, as the patch-load path
	// does: sourceHandles.size() == 2, but preset[0].size() is still 1.
	r.transit->bindAddParameterRequest(target2->id, TestParamModule::PARAM_A, true);
	r.transit->taskProcessorDsp.process();
	target2->params[TestParamModule::PARAM_A].setValue(0.f);
	target2->params[TestParamModule::PARAM_A].setValue(1.f);

	// Park the mix point on snapshot A, the one bound to slot 0.
	r.pad->snapshotsUsed = 1;
	connectMixInputs(r.pad);
	setMixVoltage(r.pad, -5.f, -5.f);

	// Run the pad over slot 0. Before the fix this reads preset[0][1] out of
	// bounds and writes whatever it finds there into target2's param; after
	// the fix the loop breaks at i == 1 and target2 is left untouched.
	REQUIRE_NOTHROW(r.run(5));
	REQUIRE(r.paramValue() == Catch::Approx(0.5f).margin(0.001f));
	REQUIRE(target2->params[TestParamModule::PARAM_A].getValue() == 1.f);
}


// ============================================================
// Transit + TransitEx ("+T") + TransitPad chain
// Per the manual, up to 14 +T expanders can sit between TRANSIT and
// TRANSIT-PAD, and their snapshots are reachable from the pad just like the
// host TRANSIT's own. None of the tests above ever place a TransitEx in the
// chain, so the cross-expander slot addressing (Transit::getSlot() routing
// index >= NUM_PRESETS to N[index / NUM_PRESETS], i.e. onto the +T) is
// exercised here for the first time with a pad at the end of the chain.
// ============================================================

TEST_CASE("Transit+TransitEx+TransitPad: chain discovery reaches the pad through a +T expander", "[TransitPad][Transit][TransitEx]") {
	Test::Harness h;
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");
	TransitExModule<12>* ex = h.addModule<TransitExModule<12>>("TransitEx");
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");

	h.connectChain(transit, ex, pad);
	transit->setProcessDivision(1);
	h.dspStep();

	// The pad is still discovered as Transit's XY-pad master even though it
	// isn't Transit's direct right neighbour -- the walk must step over the
	// +T, not stop at the first non-pad expander it meets.
	REQUIRE(transit->isXyPadActive());
	REQUIRE(pad->masterModule == transit);
}


TEST_CASE("Transit+TransitEx+TransitPad: a snapshot bound to a slot on the +T drives the target", "[TransitPad][Transit][TransitEx]") {
	Test::Harness h;
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");
	TransitExModule<12>* ex = h.addModule<TransitExModule<12>>("TransitEx");
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
	TestParamModule* target = h.adoptModule(new TestParamModule);

	h.connectChain(transit, ex, pad);
	transit->setProcessDivision(1);
	// The chain walk (which sets presetTotal to cover the +T's 12 slots) runs
	// inside Transit::process() on moduleChangedFlag -- needed before saving
	// into a slot >= NUM_PRESETS, same as bindParam()'s own leading tick.
	h.dspStep();

	bindParam(h, transit, target->id, TestParamModule::PARAM_A);

	// Slot 12 is local slot 0 on the +T (index / NUM_PRESETS == 1 routes to
	// N[1], the first expander after Transit itself).
	target->params[TestParamModule::PARAM_A].setValue(0.65f);
	transit->presetSave(12);

	// Bind pad point A to that +T-hosted slot and park the mix point on it.
	pad->snapshotsUsed = 1;
	pad->bindSnapshot(0, 12);
	pad->inputs[TransitPadModule<>::MIX_X_INPUT].channels = 1;
	pad->inputs[TransitPadModule<>::MIX_Y_INPUT].channels = 1;
	pad->inputs[TransitPadModule<>::MIX_X_INPUT].setVoltage(-5.f);
	pad->inputs[TransitPadModule<>::MIX_Y_INPUT].setVoltage(-5.f);

	// Drive target away from the saved value first: presetSave() above set it
	// to 0.65 as a side effect of capturing it, so reading 0.65 back without
	// this would pass even if the pad never wrote anything at all.
	target->params[TestParamModule::PARAM_A].setValue(0.1f);
	h.dspSteps(20);

	REQUIRE(target->params[TestParamModule::PARAM_A].getValue() == Catch::Approx(0.65f).margin(0.001f));
}


TEST_CASE("Transit+TransitEx+TransitPad: pad blends across a slot boundary spanning the +T", "[TransitPad][Transit][TransitEx]") {
	Test::Harness h;
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");
	TransitExModule<12>* ex = h.addModule<TransitExModule<12>>("TransitEx");
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
	TestParamModule* target = h.adoptModule(new TestParamModule);

	h.connectChain(transit, ex, pad);
	transit->setProcessDivision(1);
	h.dspStep();

	bindParam(h, transit, target->id, TestParamModule::PARAM_A);

	// Slot 11 is the host TRANSIT's own last slot; slot 12 is the +T's first.
	// A blend that spans exactly that boundary is the case most likely to
	// break if the chain walk or getSlot() mis-routes one side of it.
	target->params[TestParamModule::PARAM_A].setValue(0.0f);
	transit->presetSave(11);
	target->params[TestParamModule::PARAM_A].setValue(1.0f);
	transit->presetSave(12);

	pad->snapshotsUsed = 2;
	pad->bindSnapshot(0, 11);
	pad->bindSnapshot(1, 12);
	pad->inputs[TransitPadModule<>::MIX_X_INPUT].channels = 1;
	pad->inputs[TransitPadModule<>::MIX_Y_INPUT].channels = 1;

	// Snapshot A defaults to (0, 0), snapshot B to (1, 0) -- both at y=0, so
	// any point on their perpendicular bisector x=0.5 is equidistant from
	// both regardless of y. Mix X = 0V -> x=0.5; mix Y = -4V -> y=0.1, off
	// the A-B line on purpose so the test isn't tied to y=0 specifically.
	pad->inputs[TransitPadModule<>::MIX_X_INPUT].setVoltage(0.f);
	pad->inputs[TransitPadModule<>::MIX_Y_INPUT].setVoltage(-4.f);
	h.dspSteps(20);

	REQUIRE(target->params[TestParamModule::PARAM_A].getValue() == Catch::Approx(0.5f).margin(0.001f));
}


// Regression: in Transit -> +T -> Pad, deleting Transit only changes the +T's
// neighbour -- Rack's Engine::removeModule_NoLock() calls setExpanderModule()
// on Transit's other neighbour, not on every module further down the chain.
// TransitPadModule::onExpanderChange only fires on the pad's own direct
// neighbour, and TransitModule's destructor has no notion of which pad (if
// any) is further downstream, so masterModule was left pointing at freed
// memory: getItemLabel() (and the node menu, tooltip and viz overlay that all
// read masterModule) would dereference it. Fixed by having the pad itself
// re-walk left through any +T chain whenever the shared "Transit" topic fires
// -- which TransitEx's own onExpanderChange still does when its neighbour
// changes -- rather than relying on a notification that never reaches the pad
// directly.
//
// transit is added through the harness like any other module (so it's
// actually stepped -- only h.modules gets ticked by h.dspStep(), and a
// module created via the bare Test::createModule()/registerModule() pattern
// is invisible to that), then deliberately un-adopted before its real,
// deliberate mid-test destruction: erased from h.modules so the harness's
// own teardown doesn't try to destroy it a second time, engine-unregistered
// via Test::unregisterModule() (the real removeModule_NoLock() path this
// regression depends on -- not h.disconnectExpander(), which is harness-only
// and wouldn't reproduce Rack's own neighbour-pointer update), then deleted
// directly. ex/pad/target stay harness-owned and torn down as usual.
TEST_CASE("Transit+TransitEx+TransitPad: deleting Transit clears the pad's masterModule, not just disconnects it", "[TransitPad][Transit][TransitEx]") {
	Test::Harness h;
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");
	TransitExModule<12>* ex = h.addModule<TransitExModule<12>>("TransitEx");
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");

	h.connectChain(transit, ex, pad);
	transit->setProcessDivision(1);
	h.dspStep();

	REQUIRE(pad->masterModule == transit);
	REQUIRE(pad->getItemLabel(pad->currentSet, 0) != "<No TRANSIT module>");

	h.modules.erase(std::find(h.modules.begin(), h.modules.end(), transit));
	Test::unregisterModule(transit);
	delete transit;

	// The pad's own onExpanderChange never fired (its neighbour is still ex,
	// unchanged) -- masterModule is stale until the pad's next process() tick
	// re-walks the chain on the "Transit" topic notification ex's own
	// onExpanderChange already sent.
	h.dspStep();

	REQUIRE(pad->masterModule == nullptr);
	REQUIRE(pad->getItemLabel(pad->currentSet, 0) == "<No TRANSIT module>");
}


// onRandomize() respects the chain and snapshot setup: only active pad points
// (id < snapshotsUsed) are touched, and each is rebound to one of the host
// TRANSIT's (or a chained +T's) actually-used slots -- not just repositioned
// on screen with its old binding left untouched, and never left pointing at
// an empty slot.

TEST_CASE("onRandomize only touches active pad points", "[TransitPad]") {
	Test::Harness h;
	TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");

	m->snapshotsUsed = 3;
	for (uint8_t i = 0; i < 8; i++) m->nodes.setXyImmediate(i, 0.5f, 0.5f);

	Module::RandomizeEvent e;
	m->onRandomize(e);

	// Nodes above the active count must be untouched.
	for (uint8_t i = 3; i < 8; i++) {
		REQUIRE(m->nodes.getXFinal(i) == 0.5f);
		REQUIRE(m->nodes.getYFinal(i) == 0.5f);
	}
}

TEST_CASE("onRandomize leaves bindings untouched with no Transit connected", "[TransitPad]") {
	Test::Harness h;
	TransitPadModule<>* m = h.addModule<TransitPadModule<>>("TransitPad");
	REQUIRE(m->masterModule == nullptr);

	m->snapshotsUsed = 2;
	m->bindSnapshot(0, 5);
	m->bindSnapshot(1, -1);

	Module::RandomizeEvent e;
	m->onRandomize(e);

	REQUIRE(m->snapshots[m->currentSet][0].id == 5);
	REQUIRE(m->snapshots[m->currentSet][1].id == -1);
}

TEST_CASE("onRandomize leaves bindings untouched when the chain has no saved presets", "[TransitPad][Transit]") {
	Test::Harness h;
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
	connectPad(h, transit, pad);
	h.dspStep();
	REQUIRE(pad->masterModule == transit);

	// Fresh Transit: no slot has ever been saved to.
	pad->snapshotsUsed = 2;
	pad->bindSnapshot(0, 3);
	pad->bindSnapshot(1, -1);

	Module::RandomizeEvent e;
	pad->onRandomize(e);

	REQUIRE(pad->snapshots[pad->currentSet][0].id == 3);
	REQUIRE(pad->snapshots[pad->currentSet][1].id == -1);
}

TEST_CASE("onRandomize rebinds active pad points to only the chain's used slots", "[TransitPad][Transit]") {
	Test::Harness h;
	random::init();

	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
	connectPad(h, transit, pad);
	h.dspStep();

	// Save exactly two slots, distinct from every default binding, so a
	// rebind landing on either is unambiguous.
	transit->presetSave(6);
	transit->presetSave(9);
	pad->snapshotsUsed = 4;

	bool sawSlot6 = false, sawSlot9 = false;
	for (int trial = 0; trial < 30; trial++) {
		pad->bindSnapshot(0, -1);
		pad->bindSnapshot(1, -1);
		pad->bindSnapshot(2, -1);
		pad->bindSnapshot(3, -1);

		Module::RandomizeEvent e;
		pad->onRandomize(e);

		for (uint8_t i = 0; i < 4; i++) {
			int id = pad->snapshots[pad->currentSet][i].id;
			// Every active point must land on a used slot -- never unbound,
			// never one of the other (unsaved) default slots 0-3.
			REQUIRE((id == 6 || id == 9));
			if (id == 6) sawSlot6 = true;
			if (id == 9) sawSlot9 = true;
		}
	}
	// Across enough trials, both saved slots should turn up at least once --
	// otherwise a rebind that always picks the same slot would pass the
	// "only used slots" check above without actually exercising the choice.
	REQUIRE(sawSlot6);
	REQUIRE(sawSlot9);
}