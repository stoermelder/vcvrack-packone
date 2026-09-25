// TransitPad + Transit end-to-end patch save/restore tests.
//
// Everything in TransitPad.test.expander.hpp builds the chain and drives it
// live within a single session. None of it closes and reopens the patch: the
// one JSON round-trip in that file (snapshotsUsed corruption) calls
// dataFromJson() on the pad alone -- Transit is connected and running, but
// its own toJson()/fromJson() are never called, so Transit's JSON restore
// path (and the taskProcessorUi rebind below) is never exercised there.
//
// Expander wiring is not part of either module's JSON — Rack reconnects
// expanders by rack position when a patch loads, which connectPad() stands
// in for here — and Transit::dataFromJson() re-binds its sourceHandles
// through taskProcessorUi (enqueued, UI-thread) rather than synchronously,
// which a live-session test never exercises because nothing there calls
// fromJson() in the first place. This file is the one exercising both.
//
// Split out of TransitPad.test.module.hpp; see TransitPad.test.cpp for how
// this file is wired into the test binary. Reuses the TestParamModule,
// connectPad(), bindParam(), PadRig, connectMixInputs() and setMixVoltage()
// helpers declared in TransitPad.test.expander.hpp (same enclosing namespace).

// Helper: drain the same two queues Transit::dataFromJson() enqueues its
// ParamHandle clear/re-add onto, matching what TransitWidget::step() does
// for a live GUI every frame. A headless fromJson() with no widget attached
// never runs this on its own.
static void drainTransitBindQueues(TransitModule<12>* transit) {
	transit->taskProcessorUi.process();
	transit->taskProcessorDsp.process();
}


TEST_CASE("e2e: Transit+TransitPad chain survives a full patch save/restore", "[TransitPad][Transit][e2e]") {
	Test::Harness h;
	PadRig r = PadRig::make(h);

	// Build the chain: bind PARAM_A, save two presets, connect the pad, and
	// park the mix point so snapshots A and B blend unevenly (3:1 toward B) --
	// a value no accidental default state could reproduce.
	r.bind();
	r.save(0, 0.2f);
	r.save(1, 0.8f);
	connectPad(h, r.transit, r.pad);
	r.pad->setSnapshotsUsed(2);
	r.pad->nodes.setAmountImmediate(1, 3.f);
	r.run(20);

	float valueBeforeSave = r.paramValue();
	REQUIRE(valueBeforeSave == Catch::Approx(0.65f).margin(0.001f));

	// "Close" the patch: serialize both modules exactly as Rack's patch saver
	// would (Module::toJson(), which wraps params + dataToJson()).
	json_t* transitJ = r.transit->toJson();
	json_t* padJ = r.pad->toJson();

	// "Reopen" the patch: fresh module instances, same target so PARAM_A's
	// moduleId in "sourceMaps" still resolves (idFix is a same-session no-op
	// here, exactly as for a real reload of the same patch file).
	TransitPadModule<>* pad2 = h.addModule<TransitPadModule<>>("TransitPad");
	TransitModule<12>* transit2 = h.addModule<TransitModule<12>>("Transit");

	// Rack's real load order: params first, then dataFromJson() -- see the
	// Mix-cursor regression test in TransitPad.test.module.hpp for why this
	// order matters for the pad. fromJson() covers both steps for each module.
	transit2->fromJson(transitJ);
	pad2->fromJson(padJ);
	json_decref(transitJ);
	json_decref(padJ);

	// Expander wiring is a rack-position fact, not JSON: reconnect it exactly
	// as connectPad() would for a freshly-placed pair, and drain the
	// UI-enqueued rebind Transit queued from within dataFromJson().
	h.connectExpander(transit2, pad2);
	transit2->setProcessDivision(1);
	drainTransitBindQueues(transit2);
	h.dspStep();

	REQUIRE(transit2->isXyPadActive());
	REQUIRE(pad2->masterModule == transit2);
	REQUIRE(pad2->snapshotsUsed == 2);

	// Re-run the restored chain over the same number of steps as before saving
	// and confirm it reproduces the exact same blended output -- nothing about
	// the restore silently changed the bound parameter, the presets, the
	// snapshot bindings, or the per-node amount.
	h.dspSteps(20);
	REQUIRE(r.paramValue() == Catch::Approx(valueBeforeSave).margin(0.001f));

	// The restored chain must still react live, not just replay a frozen value:
	// moving the mix point onto snapshot B alone should reach its preset exactly.
	connectMixInputs(pad2);
	setMixVoltage(pad2, 5.f, -5.f);
	h.dspSteps(20);
	REQUIRE(r.paramValue() == Catch::Approx(0.8f).margin(0.001f));
}


TEST_CASE("e2e: bindSnapshot rebindings survive save/restore and keep driving Transit", "[TransitPad][Transit][e2e]") {
	Test::Harness h;
	PadRig r = PadRig::make(h);

	r.bind();
	r.save(0, 0.1f);
	r.save(5, 0.9f);
	connectPad(h, r.transit, r.pad);

	// Rebind pad point A away from its default slot 0 onto slot 5, and park
	// the mix point on it so it alone drives the blend.
	r.pad->setSnapshotsUsed(1);
	r.pad->bindSnapshot(0, 5);
	connectMixInputs(r.pad);
	setMixVoltage(r.pad, -5.f, -5.f);
	r.run(20);
	REQUIRE(r.paramValue() == Catch::Approx(0.9f).margin(0.001f));

	json_t* transitJ = r.transit->toJson();
	json_t* padJ = r.pad->toJson();

	TransitPadModule<>* pad2 = h.addModule<TransitPadModule<>>("TransitPad");
	TransitModule<12>* transit2 = h.addModule<TransitModule<12>>("Transit");
	transit2->fromJson(transitJ);
	pad2->fromJson(padJ);
	json_decref(transitJ);
	json_decref(padJ);

	h.connectExpander(transit2, pad2);
	transit2->setProcessDivision(1);
	drainTransitBindQueues(transit2);
	h.dspStep();

	// The rebind (A -> slot 5, not the factory default slot 0) must have
	// survived: snapshots[][] is TransitPad's own JSON, not Transit's.
	REQUIRE(pad2->snapshots[pad2->currentSet][0].id == 5);

	connectMixInputs(pad2);
	setMixVoltage(pad2, -5.f, -5.f);
	h.dspSteps(20);
	REQUIRE(r.paramValue() == Catch::Approx(0.9f).margin(0.001f));

	// Unbinding on the restored pad must still reach Transit -- confirms the
	// restored chain is live wiring, not a one-shot replay of saved state.
	r.target->params[TestParamModule::PARAM_A].setValue(0.42f);
	pad2->bindSnapshot(0, -1);
	h.dspSteps(20);
	REQUIRE(r.paramValue() == Catch::Approx(0.42f).margin(0.001f));
}


// A single bound parameter and two snapshots, as above, can't catch bugs that
// only show up once several independently-interpolated params and several
// sets are all live at once -- e.g. JSON that mixes up which set a snapshot's
// id/weight belongs to, or which of several sourceHandles a preset's values
// line up with once the vectors have more than one entry. This builds a
// four-snapshot, two-parameter, two-set chain and checks every combination
// still resolves correctly after save/restore.
//
// Everything here is observed the same way a user would: by reading the
// bound target parameters that Transit actually writes, driven purely
// through the pad's public inputs/params (mix CV, set buttons, bindSnapshot).
// Nothing reads dist[]/weight/node params or any other internal state.
TEST_CASE("e2e: multi-snapshot, multi-parameter, multi-set chain survives save/restore", "[TransitPad][Transit][e2e]") {
	Test::Harness h;
	PadRig r = PadRig::make(h);
	r.bind();
	bindParam(h, r.transit, r.target->id, TestParamModule::PARAM_B);

	// Four presets, each with a distinct (A, B) pair so a mixed-up index shows
	// up as a wrong value on either parameter, not just a coincidental match.
	auto save = [&](int slot, float a, float b) {
		r.target->params[TestParamModule::PARAM_A].setValue(a);
		r.target->params[TestParamModule::PARAM_B].setValue(b);
		r.transit->presetSave(slot);
	};
	save(0, 0.0f, 1.0f);
	save(1, 0.2f, 0.8f);
	save(2, 0.6f, 0.4f);
	save(3, 1.0f, 0.0f);

	connectPad(h, r.transit, r.pad);
	r.pad->setSnapshotsUsed(4);

	// Set 1: rebind the same four pad points to the reverse slot order, so the
	// two sets disagree about which preset each snapshot reaches -- a save/
	// restore that flattened per-set bindings into one shared array would
	// show up as set 1 reproducing set 0's output instead of its own.
	r.pad->currentSet = 1;
	r.pad->bindSnapshot(0, 3);
	r.pad->bindSnapshot(1, 2);
	r.pad->bindSnapshot(2, 1);
	r.pad->bindSnapshot(3, 0);
	r.pad->currentSet = 0;

	// Park the mix point on snapshot A so each set's output is unambiguously
	// that set's slot-0 binding (set 0: slot 0, set 1: slot 3).
	connectMixInputs(r.pad);
	setMixVoltage(r.pad, -5.f, -5.f);
	r.run(20);

	REQUIRE(r.target->params[TestParamModule::PARAM_A].getValue() == Catch::Approx(0.0f).margin(0.001f));
	REQUIRE(r.target->params[TestParamModule::PARAM_B].getValue() == Catch::Approx(1.0f).margin(0.001f));

	r.pad->params[TransitPadModule<>::SET_PARAM + 1].setValue(1.f);
	r.run(100);
	r.pad->params[TransitPadModule<>::SET_PARAM + 1].setValue(0.f);
	r.run(20);
	REQUIRE(r.target->params[TestParamModule::PARAM_A].getValue() == Catch::Approx(1.0f).margin(0.001f));
	REQUIRE(r.target->params[TestParamModule::PARAM_B].getValue() == Catch::Approx(0.0f).margin(0.001f));

	// Leave the pad on set 1 so the restore has to pick that up from JSON
	// rather than defaulting back to set 0.
	json_t* transitJ = r.transit->toJson();
	json_t* padJ = r.pad->toJson();

	TransitPadModule<>* pad2 = h.addModule<TransitPadModule<>>("TransitPad");
	TransitModule<12>* transit2 = h.addModule<TransitModule<12>>("Transit");
	transit2->fromJson(transitJ);
	pad2->fromJson(padJ);
	json_decref(transitJ);
	json_decref(padJ);

	h.connectExpander(transit2, pad2);
	transit2->setProcessDivision(1);
	drainTransitBindQueues(transit2);
	h.dspStep();

	connectMixInputs(pad2);
	setMixVoltage(pad2, -5.f, -5.f);
	h.dspSteps(20);

	// Still on set 1 (D->0): both params must land on preset 3's values.
	REQUIRE(r.target->params[TestParamModule::PARAM_A].getValue() == Catch::Approx(1.0f).margin(0.001f));
	REQUIRE(r.target->params[TestParamModule::PARAM_B].getValue() == Catch::Approx(0.0f).margin(0.001f));

	// Switching back to set 0 (A->0) must reach the other binding, proving
	// both sets' bindings came back distinct rather than collapsed together.
	pad2->params[TransitPadModule<>::SET_PARAM + 0].setValue(1.f);
	h.dspSteps(100);
	pad2->params[TransitPadModule<>::SET_PARAM + 0].setValue(0.f);
	h.dspSteps(20);
	REQUIRE(r.target->params[TestParamModule::PARAM_A].getValue() == Catch::Approx(0.0f).margin(0.001f));
	REQUIRE(r.target->params[TestParamModule::PARAM_B].getValue() == Catch::Approx(1.0f).margin(0.001f));
}


// A +T (TransitEx) sitting between Transit and the pad is a supported,
// documented chain (manual: "up to fourteen +T expanders can be chained
// between TRANSIT and TRANSIT-PAD"), but nothing above ever saves/restores
// one. It matters here specifically because a +T's presets are guarded by
// ctrlUniqueId (Transit.cpp: expanderCleanUp()): on every chain-discovery
// tick, Transit resets any expander whose ctrlUniqueId doesn't match its own
// -- which is JSON-persisted state on both sides. A restore that reconnects
// Transit and the +T without their saved ctrlUniqueIds agreeing again would
// silently wipe every preset stored on the +T on the very first tick after
// load. Observed purely through target's bound parameter, same as the chains
// above -- nothing here reads ctrlUniqueId or any other internal field.
TEST_CASE("e2e: Transit+TransitEx+TransitPad chain survives a full patch save/restore", "[TransitPad][Transit][TransitEx][e2e]") {
	Test::Harness h;
	TransitModule<12>* transit = h.addModule<TransitModule<12>>("Transit");
	TransitExModule<12>* ex = h.addModule<TransitExModule<12>>("TransitEx");
	TransitPadModule<>* pad = h.addModule<TransitPadModule<>>("TransitPad");
	TestParamModule* target = h.adoptModule(new TestParamModule);

	h.connectChain(transit, ex, pad);
	transit->setProcessDivision(1);
	h.dspStep();

	bindParam(h, transit, target->id, TestParamModule::PARAM_A);

	// Slot 11 is Transit's own last slot, slot 12 the +T's first -- saving on
	// both sides of that boundary means the restored chain has to get the
	// +T's presets back intact, not just Transit's own.
	target->params[TestParamModule::PARAM_A].setValue(0.3f);
	transit->presetSave(11);
	target->params[TestParamModule::PARAM_A].setValue(0.9f);
	transit->presetSave(12);

	pad->setSnapshotsUsed(1);
	pad->bindSnapshot(0, 12);
	pad->inputs[TransitPadModule<>::MIX_X_INPUT].channels = 1;
	pad->inputs[TransitPadModule<>::MIX_Y_INPUT].channels = 1;
	pad->inputs[TransitPadModule<>::MIX_X_INPUT].setVoltage(-5.f);
	pad->inputs[TransitPadModule<>::MIX_Y_INPUT].setVoltage(-5.f);
	target->params[TestParamModule::PARAM_A].setValue(0.55f);
	h.dspSteps(20);

	REQUIRE(target->params[TestParamModule::PARAM_A].getValue() == Catch::Approx(0.9f).margin(0.001f));

	// "Close" the patch: serialize all three modules exactly as Rack's patch
	// saver would.
	json_t* transitJ = transit->toJson();
	json_t* exJ = ex->toJson();
	json_t* padJ = pad->toJson();

	// "Reopen" the patch: fresh instances, restored in Rack's real order
	// (params then dataFromJson, which fromJson() already does for each).
	TransitModule<12>* transit2 = h.addModule<TransitModule<12>>("Transit");
	TransitExModule<12>* ex2 = h.addModule<TransitExModule<12>>("TransitEx");
	TransitPadModule<>* pad2 = h.addModule<TransitPadModule<>>("TransitPad");
	transit2->fromJson(transitJ);
	ex2->fromJson(exJ);
	pad2->fromJson(padJ);
	json_decref(transitJ);
	json_decref(exJ);
	json_decref(padJ);

	// Expander wiring is a rack-position fact, not JSON: reconnect the chain
	// exactly as it was, then drain Transit's UI-enqueued sourceHandles rebind.
	h.connectChain(transit2, ex2, pad2);
	transit2->setProcessDivision(1);
	drainTransitBindQueues(transit2);
	h.dspStep();

	// Drive target to a sentinel value no legitimate blend of 0.3/0.9 would
	// ever produce. If ctrlUniqueId came back mismatched, expanderCleanUp()
	// would reset ex2's presets -- slot 12 would read as unused, the pad's
	// weight would contribute nothing, and this sentinel would silently
	// survive untouched instead of being overwritten with 0.9.
	target->params[TestParamModule::PARAM_A].setValue(0.55f);
	pad2->inputs[TransitPadModule<>::MIX_X_INPUT].channels = 1;
	pad2->inputs[TransitPadModule<>::MIX_Y_INPUT].channels = 1;
	pad2->inputs[TransitPadModule<>::MIX_X_INPUT].setVoltage(-5.f);
	pad2->inputs[TransitPadModule<>::MIX_Y_INPUT].setVoltage(-5.f);
	h.dspSteps(20);
	REQUIRE(target->params[TestParamModule::PARAM_A].getValue() == Catch::Approx(0.9f).margin(0.001f));

	// The chain must still be live wiring, not a frozen replay: rebinding the
	// same pad point to the other saved slot must reach it too.
	target->params[TestParamModule::PARAM_A].setValue(0.55f);
	pad2->bindSnapshot(0, 11);
	h.dspSteps(20);
	REQUIRE(target->params[TestParamModule::PARAM_A].getValue() == Catch::Approx(0.3f).margin(0.001f));
}
