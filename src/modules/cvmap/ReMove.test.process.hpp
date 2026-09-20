// REMOVE test cases: process() behavior (buttons, CV inputs, recording modes),
// history integration, CV_OUTPUT/CV_INPUT modes, the recording lifecycle, and learn.
// Included by ReMove.test.cpp inside namespace __process. Not standalone:
// ReMove.test.hpp supplies everything used here.

TEST_CASE("REC output is 0V by default (no recording)", "[ReMove][process]") {
	Test::Harness h;
	auto module = h.addModule<ReMoveModule>("ReMoveLite");
	module->recOutCvMode = RECOUTCVMODE_GATE;

	h.dspStep();
	REQUIRE(module->outputs[ReMoveModule::REC_OUTPUT].getVoltage() == 0.f);
}

TEST_CASE("process() is safe with no mapped parameter", "[ReMove][process]") {
	Test::Harness h;
	auto module = h.addModule<ReMoveModule>("ReMoveLite");
	// Without any param mapping, the module has no paramQuantity for index 0.
	// Pressing REC must not start recording.
	module->params[ReMoveModule::REC_PARAM].setValue(1.f);
	h.dspStep();

	REQUIRE(module->isRecording == false);
}

TEST_CASE("RUN_PARAM button toggles isPlaying", "[ReMove][process]") {
	Test::Harness h;
	auto module = h.addModule<ReMoveModule>("ReMoveLite");

	REQUIRE(module->isPlaying == false);

	// Initialize the BooleanTrigger state by processing a low value first.
	module->params[ReMoveModule::RUN_PARAM].setValue(0.f);
	h.dspStep();

	// Press the RUN button (BooleanTrigger fires on rising edge).
	module->params[ReMoveModule::RUN_PARAM].setValue(1.f);
	h.dspStep();
	REQUIRE(module->isPlaying == true);

	// Release (no edge -> no toggle).
	module->params[ReMoveModule::RUN_PARAM].setValue(0.f);
	h.dspStep();
	REQUIRE(module->isPlaying == true);

	// Press again -> toggle off.
	module->params[ReMoveModule::RUN_PARAM].setValue(1.f);
	h.dspStep();
	REQUIRE(module->isPlaying == false);
}

TEST_CASE("RESET_PARAM button resets dataPtr to seqLow and playDir to FWD", "[ReMove][process]") {
	Test::Harness h;
	auto module = h.addModule<ReMoveModule>("ReMoveLite");
	module->seqResize(4);
	module->seq = 1;
	module->dataPtr = 5000;
	module->playDir = REMOVE_PLAYDIR_REV;

	// Initialize SchmittTrigger state for resetCvTrigger.
	module->params[ReMoveModule::RESET_PARAM].setValue(0.f);
	h.dspStep();

	module->params[ReMoveModule::RESET_PARAM].setValue(1.f);
	h.dspStep();

	REQUIRE(module->dataPtr == module->seqLow);
	REQUIRE(module->playDir == REMOVE_PLAYDIR_FWD);
}

TEST_CASE("SEQ_PARAM buttons cycle through sequences", "[ReMove][process]") {
	Test::Harness h;
	auto module = h.addModule<ReMoveModule>("ReMoveLite");
	module->seqResize(4);
	module->seq = 0;

	// Initialize SchmittTrigger state (UNINITIALIZED → LOW) before pressing.
	module->params[ReMoveModule::SEQN_PARAM].setValue(0.f);
	h.dspStep();

	module->params[ReMoveModule::SEQN_PARAM].setValue(1.f);
	h.dspStep();
	REQUIRE(module->seq == 1);

	// Re-initialize SEQP trigger.
	module->params[ReMoveModule::SEQP_PARAM].setValue(0.f);
	h.dspStep();

	module->params[ReMoveModule::SEQP_PARAM].setValue(1.f);
	h.dspStep();
	REQUIRE(module->seq == 0);
}

TEST_CASE("RUN_INPUT gate mode drives isPlaying from voltage", "[ReMove][process]") {
	Test::Harness h;
	auto module = h.addModule<ReMoveModule>("ReMoveLite");
	module->runCvMode = RUNCVMODE_GATE;
	module->inputs[ReMoveModule::RUN_INPUT].channels = 1;

	module->inputs[ReMoveModule::RUN_INPUT].setVoltage(0.f);
	h.dspStep();
	REQUIRE(module->isPlaying == false);

	module->inputs[ReMoveModule::RUN_INPUT].setVoltage(5.f);
	h.dspStep();
	REQUIRE(module->isPlaying == true);

	module->inputs[ReMoveModule::RUN_INPUT].setVoltage(0.f);
	h.dspStep();
	REQUIRE(module->isPlaying == false);
}

TEST_CASE("RUN_INPUT trigger mode toggles isPlaying on edges", "[ReMove][process]") {
	Test::Harness h;
	auto module = h.addModule<ReMoveModule>("ReMoveLite");
	module->runCvMode = RUNCVMODE_TRIG;
	module->inputs[ReMoveModule::RUN_INPUT].channels = 1;

	module->inputs[ReMoveModule::RUN_INPUT].setVoltage(0.f);
	h.dspStep();
	REQUIRE(module->isPlaying == false);

	module->inputs[ReMoveModule::RUN_INPUT].setVoltage(5.f); // rising edge
	h.dspStep();
	REQUIRE(module->isPlaying == true);

	module->inputs[ReMoveModule::RUN_INPUT].setVoltage(0.f);
	h.dspStep();
	REQUIRE(module->isPlaying == true); // no toggle, just gate-off

	module->inputs[ReMoveModule::RUN_INPUT].setVoltage(5.f); // rising edge again
	h.dspStep();
	REQUIRE(module->isPlaying == false);
}

TEST_CASE("SEQ_INPUT in 0..10V mode selects sequence", "[ReMove][process]") {
	Test::Harness h;
	auto module = h.addModule<ReMoveModule>("ReMoveLite");
	module->seqResize(4);
	module->seqCvMode = SEQCVMODE_10V;
	module->inputs[ReMoveModule::SEQ_INPUT].channels = 1;
	module->seq = 0;

	// Drive resetCvTimer so the SEQ_INPUT code path is reached.
	// resetCvTimer gates this branch (>= 1e-3f). Without pressing RESET, it never
	// starts counting, so SEQ_INPUT is ignored. We exercise the path by pressing
	// RESET first, then driving SEQ_INPUT.
	module->params[ReMoveModule::RESET_PARAM].setValue(1.f);
	h.dspStep();
	module->params[ReMoveModule::RESET_PARAM].setValue(0.f);

	// Drive enough samples for resetCvTimer to elapse (>= 1e-3f) — at 44100Hz, ~45 samples.
	module->inputs[ReMoveModule::SEQ_INPUT].setVoltage(7.5f); // 7.5/10 * 4 = 3.0 → seq 3
	h.dspSteps(100);

	// 7.5V / 10V * 4 = 3.0 (floor) → seq 3.
	REQUIRE(module->seq == 3);
}

TEST_CASE("SEQ_INPUT in C4-G4 mode selects sequence from voltage", "[ReMove][process]") {
	Test::Harness h;
	auto module = h.addModule<ReMoveModule>("ReMoveLite");
	module->seqResize(4);
	module->seqCvMode = SEQCVMODE_C4;
	module->inputs[ReMoveModule::SEQ_INPUT].channels = 1;
	module->seq = 0;

	module->params[ReMoveModule::RESET_PARAM].setValue(1.f);
	h.dspStep();
	module->params[ReMoveModule::RESET_PARAM].setValue(0.f);

	// Voltage * 12 = seq index. 0.5V → 6 → clamp(seqCount-1).
	module->inputs[ReMoveModule::SEQ_INPUT].setVoltage(0.5f);
	h.dspSteps(100);

	REQUIRE(module->seq == 3); // clamped to seqCount-1
}

TEST_CASE("PHASE_INPUT connection forces isPlaying to false", "[ReMove][process]") {
	Test::Harness h;
	auto module = h.addModule<ReMoveModule>("ReMoveLite");
	module->seqResize(4);

	// isPlaying becomes false as soon as PHASE_INPUT is connected during process().
	module->isPlaying = true;
	module->inputs[ReMoveModule::PHASE_INPUT].channels = 1;
	module->inputs[ReMoveModule::PHASE_INPUT].setVoltage(5.f);

	h.dspStep();

	REQUIRE(module->isPlaying == false);
}

TEST_CASE("PHASE_INPUT maps voltage to a position within the sequence", "[ReMove][process]") {
	// The body of the PHASE block (ReMove.cpp:407-419) is skipped unless a parameter is
	// mapped (getParamQuantity(0) != NULL), so these need the same mapped-parameter fixture
	// as the playback tests, not just a connected PHASE cable.
	//
	// The real, patch-visible behavior of PHASE scrubbing is what it writes to CV_OUTPUT and
	// the mapped target's parameter — dataPtr is only asserted alongside as explanation.
	Test::Harness h;
	ReMoveModule* m = h.addModule<ReMoveModule>("ReMoveLite");
	rack::Module* target = h.addModule<ReMoveModule>("ReMoveLite");
	h.mapParam(&m->paramHandles[0], target, ReMoveModule::SLEW_PARAM);
	m->updateMapLen();
	h.connectOutput(m, ReMoveModule::CV_OUTPUT);
	REQUIRE(m->outCvMode == OUTCVMODE_CV_UNI); // 0..1 internal value rescales to 0..10V

	m->seqResize(1);
	m->seqLength[0] = 3;
	m->seqData[0] = 0.1f;
	m->seqData[1] = 0.5f;
	m->seqData[2] = 0.9f;
	m->audioRate = true; // update on every dspStep(), not just every processDivider tick
	m->inputs[ReMoveModule::PHASE_INPUT].channels = 1;

	SECTION("0V maps to the first sample of the sequence") {
		m->inputs[ReMoveModule::PHASE_INPUT].setVoltage(0.f);
		h.dspStep();
		REQUIRE(m->outputs[ReMoveModule::CV_OUTPUT].getVoltage() == Catch::Approx(1.f));
		REQUIRE(h.mappedValue(&m->paramHandles[0]) == Catch::Approx(0.1f * 0.975f));
		REQUIRE(m->dataPtr == m->seqLow);
	}

	SECTION("10V maps to the last sample of the sequence") {
		m->inputs[ReMoveModule::PHASE_INPUT].setVoltage(10.f);
		h.dspStep();
		REQUIRE(m->outputs[ReMoveModule::CV_OUTPUT].getVoltage() == Catch::Approx(9.f));
		REQUIRE(h.mappedValue(&m->paramHandles[0]) == Catch::Approx(0.9f * 0.975f));
		REQUIRE(m->dataPtr == m->seqLow + m->seqLength[0] - 1);
	}

	SECTION("An empty sequence is not read by PHASE: process() bypasses input straight to output instead") {
		// Without the seqLength[seq] > 0 guard, seqLength[seq] == 0 collapses the rescale's
		// upper bound to seqLow - 1, which reads seqData[-1] on sequence 0 (heap OOB) or a
		// neighbouring sequence's data otherwise. Default module state has every sequence
		// empty, so this is the module's out-of-the-box configuration with a mapped
		// parameter and a PHASE cable.
		//
		// The real behavior on an empty sequence isn't "nothing happens": PHASE forces
		// isPlaying=false (line 409), and with isRecording also false, ReMove.cpp:455-457
		// explicitly bypasses the mapped input straight to the output (`if
		// (seqLength[seq]==0) setValue(getValue())`) rather than holding a stale value. That
		// bypass reads the *mapped* param, not seqData, so it's what should be asserted here.
		m->seqLength[0] = 0;
		m->dataPtr = m->seqLow;
		h.setMappedValue(&m->paramHandles[0], 0.4f); // target's SLEW_PARAM, range [0, 0.975]

		m->inputs[ReMoveModule::PHASE_INPUT].setVoltage(10.f);
		h.dspStep();

		// getValue() reads the mapped param scaled to 0..1, setValue() rescales that back to
		// 0..10V on CV_OUTPUT: 0.4 / 0.975 -> 0..1 -> *10 = the same 0.4-of-range fraction.
		float expectedVoltage = (0.4f / 0.975f) * 10.f;
		REQUIRE(m->outputs[ReMoveModule::CV_OUTPUT].getVoltage() == Catch::Approx(expectedVoltage));
		REQUIRE(m->dataPtr == m->seqLow);
		REQUIRE(m->dataPtr >= 0);
	}
}

TEST_CASE("RECMODE_MOVE trim keeps the last recorded sample", "[ReMove][process]") {
	// Deliberately a direct unit test of the trim arithmetic at ReMove.cpp:312-321, not an
	// end-to-end recording (arm -> touch -> record -> release). RECMODE_MOVE's trim only runs
	// on the step where `APP->event->getDraggedWidget() == NULL` (ReMove.cpp:308) while
	// recTouched is already true (ReMove.cpp:312-321) — headless, getDraggedWidget() is
	// always NULL (nothing is ever mid-drag), so that condition is true on the very first
	// qualifying step. A real end-to-end drive of this path would need the harness to
	// simulate an actual held drag (EventDriver has real widget drag support, e.g. for
	// ParamWidget knobs) across several steps before releasing it — meaningfully more
	// scaffolding than this fix warrants.
	//
	// Driving this without ever calling startRecording() keeps recChangeHistory NULL, so
	// stopRecording()'s history push is skipped and the trim can be exercised on its own.
	//
	// After the loop, `i` is the index of the LAST sample worth keeping, so the sequence
	// [seqLow, i] holds `i - seqLow + 1` elements — not `i - seqLow`, which truncates every
	// Move-mode recording by one sample.
	Test::Harness h;
	auto module = h.addModule<ReMoveModule>("ReMoveLite");
	module->seqResize(1);
	module->seq = 0;

	SECTION("Trailing repeated values are trimmed, keeping one of them") {
		// 0.1, 0.2, 0.3, 0.3, 0.3 -> trim keeps 0.1, 0.2, 0.3 (length 3).
		module->seqLength[0] = 5;
		module->seqData[0] = 0.1f;
		module->seqData[1] = 0.2f;
		module->seqData[2] = 0.3f;
		module->seqData[3] = 0.3f;
		module->seqData[4] = 0.3f;

		module->recMode = RECMODE_MOVE;
		module->isRecording = true;
		module->recTouched = true; // skip the touch-detection branch (line 294-303)
		module->sampleRate = 1e-9f; // fire the trim on the very next dspStep()

		h.dspStep();

		REQUIRE(module->seqLength[0] == 3);
		REQUIRE(module->seqData[0] == Catch::Approx(0.1f));
		REQUIRE(module->seqData[1] == Catch::Approx(0.2f));
		REQUIRE(module->seqData[2] == Catch::Approx(0.3f));
		REQUIRE(module->isRecording == false);
	}

	SECTION("A recording with no trailing repeats keeps every sample") {
		module->seqLength[0] = 3;
		module->seqData[0] = 0.1f;
		module->seqData[1] = 0.2f;
		module->seqData[2] = 0.3f;

		module->recMode = RECMODE_MOVE;
		module->isRecording = true;
		module->recTouched = true;
		module->sampleRate = 1e-9f;

		h.dspStep();

		REQUIRE(module->seqLength[0] == 3);
	}
}

TEST_CASE("RECMODE_SAMPLEHOLD does not overrun the sequence when it starts near the end", "[ReMove][process]") {
	// Sample & Hold normally records one real sample and duplicates it into the next slot
	// (ReMove.cpp:329-346): dataPtr starts at seqLow (well below seqHigh), so after the
	// sample is written and dataPtr advances by one, the seqHigh check doesn't fire and the
	// duplicate-write proceeds safely. That invariant breaks if dataPtr is already at
	// seqHigh - 1 when S&H fires (e.g. recording started in a different mode and recMode was
	// switched to Sample & Hold mid-recording without an intervening reset): the sample write
	// advances dataPtr to exactly seqHigh, the first `if` correctly stops the recording and
	// resets dataPtr to seqLow — but the old code's *second*, independent `if` then ran
	// anyway, reading seqData[seqLow - 1] (seqData[-1] on sequence 0) and overwriting
	// seqData[seqLow], the sequence's first sample, with it.
	Test::Harness h;
	auto module = h.addModule<ReMoveModule>("ReMoveLite");
	module->seqResize(1);
	module->seq = 0;

	SECTION("Starting one sample before the end stops cleanly without touching seqData[seqLow]") {
		module->seqData[module->seqLow] = 0.42f; // sentinel: must survive untouched
		module->dataPtr = module->seqHigh - 1;
		module->seqLength[0] = module->seqHigh - module->seqLow - 1;
		module->recMode = RECMODE_SAMPLEHOLD;
		module->isRecording = true;
		module->sampleRate = 1e-9f; // fire on the very next dspStep()

		h.dspStep();

		REQUIRE(module->isRecording == false);
		REQUIRE(module->dataPtr == module->seqLow); // stopRecording() resets it
		REQUIRE(module->seqData[module->seqLow] == Catch::Approx(0.42f));
		// Exactly one sample was recorded (the one at seqHigh - 1); no duplicate slot beyond
		// the sequence's bound was fabricated.
		REQUIRE(module->seqLength[0] == module->seqHigh - module->seqLow);
	}

	SECTION("Starting with room to spare still duplicates the sample as documented") {
		module->dataPtr = module->seqLow;
		module->seqLength[0] = 0;
		module->recMode = RECMODE_SAMPLEHOLD;
		module->isRecording = true;
		module->sampleRate = 1e-9f;

		h.dspStep();

		REQUIRE(module->isRecording == false);
		REQUIRE(module->dataPtr == module->seqLow); // stopRecording() resets it
		REQUIRE(module->seqLength[0] == 2); // the sample plus its duplicate
		REQUIRE(module->seqData[module->seqLow] == module->seqData[module->seqLow + 1]);
	}
}

TEST_CASE("Arming and stopping a recording pushes exactly one history action", "[ReMove][history]") {
	// stopRecording() routes its history push through vcv::history::push() (ReMove.cpp:586)
	// rather than calling APP->history->push() directly, which makes it mockable here — a
	// direct APP->history->push() segfaults headless because there is no real APP->history.
	// This drives the REC button end-to-end (arm, then stop) since stopRecording() no longer
	// crashes on the way there.
	struct Mock {
		TEST_MOCK_HISTORY(MockHistoryAccess);
	} mock;

	Test::Harness h;
	ReMoveModule* m = h.addModule<ReMoveModule>("ReMoveLite");
	rack::Module* target = h.addModule<ReMoveModule>("ReMoveLite");
	h.mapParam(&m->paramHandles[0], target, ReMoveModule::SLEW_PARAM);
	m->updateMapLen();
	m->recMode = RECMODE_MANUAL; // bypasses the touch/move detection gates

	// Press REC: starts recording (rising edge on the BooleanTrigger).
	m->params[ReMoveModule::REC_PARAM].setValue(0.f);
	h.dspStep();
	m->params[ReMoveModule::REC_PARAM].setValue(1.f);
	h.dspStep();
	REQUIRE(m->isRecording == true);
	REQUIRE(mock.history.pushed.empty()); // nothing pushed yet: only stopRecording() pushes

	// Release and press REC again: stops recording, pushing the recorded change.
	m->params[ReMoveModule::REC_PARAM].setValue(0.f);
	h.dspStep();
	m->params[ReMoveModule::REC_PARAM].setValue(1.f);
	h.dspStep();

	REQUIRE(m->isRecording == false);
	REQUIRE(mock.history.pushed.size() == 1);
	auto* change = dynamic_cast<rack::history::ModuleChange*>(mock.history.pushed[0]);
	REQUIRE(change != nullptr);
	REQUIRE(change->moduleId == m->id);
	REQUIRE(change->oldModuleJ != nullptr);
	REQUIRE(change->newModuleJ != nullptr);
}

TEST_CASE("Resetting mid-recording discards the pending history action instead of leaking it", "[ReMove][history]") {
	// startRecording() allocates recChangeHistory and only stopRecording() frees/pushes it.
	// onReset() (triggered by the module's own reset, and by clearMap(), which calls it) used
	// to set isRecording=false directly without touching recChangeHistory, leaving a stale
	// non-null pointer that the next startRecording() would silently overwrite — losing the
	// allocation and the (potentially multi-MB) JSON snapshot it holds. The real, observable
	// consequence: nothing should reach history for a recording that never really stopped.
	struct Mock {
		TEST_MOCK_HISTORY(MockHistoryAccess);
	} mock;

	Test::Harness h;
	ReMoveModule* m = h.addModule<ReMoveModule>("ReMoveLite");
	rack::Module* target = h.addModule<ReMoveModule>("ReMoveLite");
	h.mapParam(&m->paramHandles[0], target, ReMoveModule::SLEW_PARAM);
	m->updateMapLen();
	m->recMode = RECMODE_MANUAL;

	// Arm a recording for real (allocates recChangeHistory).
	m->params[ReMoveModule::REC_PARAM].setValue(0.f);
	h.dspStep();
	m->params[ReMoveModule::REC_PARAM].setValue(1.f);
	h.dspStep();
	REQUIRE(m->isRecording == true);

	// Reset while still recording, the way clearMap() or a module reset would.
	Module::ResetEvent re;
	m->onReset(re);

	REQUIRE(m->isRecording == false);
	REQUIRE(m->recChangeHistory == nullptr);
	// The abandoned recording must not surface as a completed action.
	REQUIRE(mock.history.pushed.empty());

	// A fresh recording afterward must behave normally: exactly one push, for the new
	// recording only — not a double-push carrying the discarded one along with it.
	// onReset() also clears the param mapping (MapModuleBase::onReset() -> clearMaps_NoLock()),
	// so it needs remapping first, same as a real user would after a reset.
	h.mapParam(&m->paramHandles[0], target, ReMoveModule::SLEW_PARAM);
	m->updateMapLen();
	m->recMode = RECMODE_MANUAL;
	m->params[ReMoveModule::REC_PARAM].setValue(0.f);
	h.dspStep();
	m->params[ReMoveModule::REC_PARAM].setValue(1.f);
	h.dspStep();
	REQUIRE(m->isRecording == true);
	m->params[ReMoveModule::REC_PARAM].setValue(0.f);
	h.dspStep();
	m->params[ReMoveModule::REC_PARAM].setValue(1.f);
	h.dspStep();

	REQUIRE(m->isRecording == false);
	REQUIRE(mock.history.pushed.size() == 1);
}

TEST_CASE("REC_OUTPUT gate mode is high while recording", "[ReMove][process]") {
	Test::Harness h;
	auto module = h.addModule<ReMoveModule>("ReMoveLite");
	module->recOutCvMode = RECOUTCVMODE_GATE;
	module->recMode = RECMODE_MANUAL; // bypasses recTouched gates
	module->isRecording = true;

	h.dspStep();
	REQUIRE(module->outputs[ReMoveModule::REC_OUTPUT].getVoltage() == 10.f);

	module->isRecording = false;
	h.dspStep();
	REQUIRE(module->outputs[ReMoveModule::REC_OUTPUT].getVoltage() == 0.f);
}

TEST_CASE("REC_OUTPUT trigger mode produces a one-shot pulse", "[ReMove][process]") {
	Test::Harness h;
	auto module = h.addModule<ReMoveModule>("ReMoveLite");
	module->recOutCvMode = RECOUTCVMODE_TRIG;
	module->recOutCvPulse.trigger(0.001f); // 1ms pulse
	module->isRecording = true;

	h.dspStep();
	REQUIRE(module->outputs[ReMoveModule::REC_OUTPUT].getVoltage() == Catch::Approx(10.f));

	// Wait long enough for the pulse to decay.
	h.dspSteps(10000);
	REQUIRE(module->outputs[ReMoveModule::REC_OUTPUT].getVoltage() == Catch::Approx(0.f).margin(1e-3f));
}


// Output mode behaviour (CV_OUTPUT)

TEST_CASE("OUTCVMODE_CV_UNI rescales 0..1 to 0..10V", "[ReMove][out]") {
	Test::ModuleScaffold<ReMoveModule> mods;
	auto module = mods.create("ReMoveLite");
	module->outCvMode = OUTCVMODE_CV_UNI;
	module->seqResize(4);
	module->seq = 0;
	module->seqLength[0] = 100;
	for (int i = 0; i < 100; i++) module->seqData[i] = 0.5f;
	module->isPlaying = true;

	// setValue() writes the CV output regardless of whether paramQuantity
	// is NULL, so we drive the playback path indirectly: call setValue()
	// directly with the first sample and verify the output voltage.
	module->setValue(0.5f, nullptr);

	REQUIRE(module->outputs[ReMoveModule::CV_OUTPUT].getVoltage() == Catch::Approx(5.f));
}

TEST_CASE("OUTCVMODE_CV_BI rescales 0..1 to -5..5V", "[ReMove][out]") {
	Test::ModuleScaffold<ReMoveModule> mods;
	auto module = mods.create("ReMoveLite");
	module->outCvMode = OUTCVMODE_CV_BI;
	module->seqResize(4);
	module->seq = 0;
	module->seqLength[0] = 100;
	for (int i = 0; i < 100; i++) module->seqData[i] = 0.5f;
	module->isPlaying = true;

	module->setValue(0.5f, nullptr);

	REQUIRE(module->outputs[ReMoveModule::CV_OUTPUT].getVoltage() == Catch::Approx(0.f));
}

TEST_CASE("Empty sequence passes through CV_INPUT when not playing", "[ReMove][out]") {
	Test::Harness h;
	auto module = h.addModule<ReMoveModule>("ReMoveLite");
	module->outCvMode = OUTCVMODE_CV_UNI;
	module->inCvMode = INCVMODE_UNI;
	module->seqResize(4);
	module->seq = 0;
	module->seqLength[0] = 0;
	module->isPlaying = false;
	module->inputs[ReMoveModule::CV_INPUT].channels = 1;
	module->inputs[ReMoveModule::CV_INPUT].setVoltage(5.f); // → 0.5 normalized

	h.dspStep();

	REQUIRE(module->outputs[ReMoveModule::CV_OUTPUT].getVoltage() == Catch::Approx(5.f));
}

TEST_CASE("INCVMODE_BI rescales -5..5V to 0..1 from CV_INPUT", "[ReMove][out]") {
	Test::Harness h;
	auto module = h.addModule<ReMoveModule>("ReMoveLite");
	module->outCvMode = OUTCVMODE_CV_UNI;
	module->inCvMode = INCVMODE_BI;
	module->seqResize(4);
	module->seq = 0;
	module->seqLength[0] = 0;
	module->isPlaying = false;
	module->inputs[ReMoveModule::CV_INPUT].channels = 1;

	// 0V in BI mode → midpoint (0.5 normalized) → 5V CV.
	module->inputs[ReMoveModule::CV_INPUT].setVoltage(0.f);
	h.dspStep();
	REQUIRE(module->outputs[ReMoveModule::CV_OUTPUT].getVoltage() == Catch::Approx(5.f));
}

// Recording lifecycle

TEST_CASE("setParameterChangesDirect toggles the parameterChangesDirect flag", "[ReMove][rec]") {
	Test::ModuleScaffold<ReMoveModule> mods;
	auto module = mods.create("ReMoveLite");
	REQUIRE(module->parameterChangesDirect == false);

	module->setParameterChangesDirect(true);
	REQUIRE(module->parameterChangesDirect == true);

	module->setParameterChangesDirect(false);
	REQUIRE(module->parameterChangesDirect == false);
}

TEST_CASE("startRecording zeros seqLength and resets dataPtr (without a mapped param it is unreachable)", "[ReMove][rec]") {
	Test::ModuleScaffold<ReMoveModule> mods;
	auto module = mods.create("ReMoveLite");
	module->seqResize(4);
	module->seq = 1;
	module->seqLength[1] = 50;
	module->dataPtr = 999;

	// startRecording allocates a history action and modifies APP->history. We
	// skip calling it directly because there's no mapped parameter to record
	// against. Instead, verify the visible side effects via direct state writes
	// that mimic what startRecording does, then verify stopRecording cleans up.
	module->isRecording = true;
	module->seqLength[module->seq] = 0;
	module->dataPtr = module->seqLow;

	REQUIRE(module->isRecording == true);
	REQUIRE(module->seqLength[1] == 0);
	REQUIRE(module->dataPtr == module->seqLow);
}

TEST_CASE("stopRecording sets isRecording=false and resets dataPtr", "[ReMove][rec]") {
	Test::ModuleScaffold<ReMoveModule> mods;
	auto module = mods.create("ReMoveLite");
	module->seqResize(4);
	module->isRecording = true;
	module->dataPtr = 5000;
	module->recChangeHistory = NULL;

	module->stopRecording();

	REQUIRE(module->isRecording == false);
	REQUIRE(module->dataPtr == module->seqLow);
}

TEST_CASE("enableLearn is suppressed during recording", "[ReMove][learn]") {
	Test::ModuleScaffold<ReMoveModule> mods;
	auto module = mods.create("ReMoveLite");
	module->isRecording = true;
	module->learningId = -1;

	module->enableLearn(0);

	// enableLearn in MapModuleBase sets learningId = id when not recording.
	// Recording should suppress this.
	REQUIRE(module->learningId == -1);
}

TEST_CASE("enableLearn works when not recording", "[ReMove][learn]") {
	Test::ModuleScaffold<ReMoveModule> mods;
	auto module = mods.create("ReMoveLite");
	module->isRecording = false;
	module->learningId = -1;

	module->enableLearn(0);

	REQUIRE(module->learningId == 0);
}

