#pragma once
// Test cases for the AhabModule core (clock, run/stop, reset, CV I/O,
// preset loading). JSON serialization tests live in Ahab.json.test.hpp.
// Included by Ahab.test.cpp, which brings Ahab.cpp into the TU first so
// AhabModule is fully defined here.

#include "Ahab.test.hpp"
#include "Ahab.vcvm.test.hpp"


TEST_CASE("Construction and initialization", "[Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	AhabWidget* mw = Test::createWidget<AhabWidget>("Ahab");

	REQUIRE(m != nullptr);
	REQUIRE(mw != nullptr);
	REQUIRE(mw->module == nullptr);

	Test::destroyWidget(mw);
	Test::destroyModule(m);
}

TEST_CASE("BPM-based clock", "[Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);
	
	// Set BPM to 120 (default)
	m->params[AhabModule::BPM_PARAM].setValue(120.0f);
	m->simRunning = true;
	
	Usz tick_before = m->sim->getTickNumber();
	
	// Process enough samples to trigger multiple clock ticks
	// At 120 BPM with 4x multiplier (internal clock rate), we get 8 Hz clock rate
	// At 44100 sample rate, that's 44100/8 = 5512.5 samples per tick
	int num_samples = 55125; // Approximately 10 ticks worth
	for (int i = 0; i < num_samples; ++i) {
		m->process(Test::makeProcessArgs(i));
	}
	
	Usz tick_after = m->sim->getTickNumber();
	Usz ticks_elapsed = tick_after - tick_before;
	
	// Tick should have incremented
	REQUIRE(ticks_elapsed > 0);
	
	// Check clock accuracy: at 120 BPM with 4x multiplier, expect ~10 ticks
	// Expected: 55125 samples / 5512.5 samples_per_tick ≈ 10 ticks
	// Allow 1% tolerance for accumulation error
	float expected_ticks = num_samples / 5512.5f;
	REQUIRE(ticks_elapsed >= (Usz)(expected_ticks * 0.99f));
	REQUIRE(ticks_elapsed <= (Usz)(expected_ticks * 1.01f));
	
	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("BPM-based clock accuracy at different tempos", "[Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);
	
	// Test at different BPM values
	std::vector<float> bpm_values = {60.0f, 120.0f, 180.0f, 240.0f};
	float sampleRate = 44100.f;
	int64_t frame = 0;
	
	for (float bpm : bpm_values) {
		Module::ResetEvent e;
		m->onReset(e); // Reset to clear any previous state
		m->params[AhabModule::BPM_PARAM].setValue(bpm);
		m->simRunning = true;
			
		Usz expected_ticks = 20;
		Usz tick_before = m->sim->getTickNumber();
		
		// Clock rate = BPM * 4 / 60 Hz
		// Samples per tick = sample_rate / clock_rate
		float clock_rate_hz = bpm * 4.0f / 60.0f;
		float samples_per_tick = sampleRate / clock_rate_hz;
		
		// Process for 20 ticks worth of samples
		int num_samples = (int)(samples_per_tick * expected_ticks + samples_per_tick * 0.5f);
		for (int i = 0; i < num_samples; ++i) {
			m->process(Test::makeProcessArgs(frame++, sampleRate));
		}
		
		Usz tick_after = m->sim->getTickNumber();
		Usz ticks_elapsed = tick_after - tick_before;
		
		REQUIRE(ticks_elapsed == expected_ticks);
	}
	
	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("External clock input", "[Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);
	
	m->simRunning = true;
	
	Usz tick_before = m->sim->getTickNumber();
	
	// Provide external clock signal
	m->inputs[AhabModule::CLK_INPUT].setVoltage(0.0f);
	m->process({});
	
	// Trigger clock
	m->inputs[AhabModule::CLK_INPUT].setVoltage(10.0f);
	m->process({});
	
	// Tick should have incremented
	REQUIRE(m->sim->getTickNumber() > tick_before);
	
	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("Manual clock button", "[Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);
	
	// Process any pending UI updates first
	m->sim->process();
	
	// Initialize Schmitt trigger with a low signal first
	m->params[AhabModule::CLK_PARAM].setValue(0.0f);
	m->process({});
	
	Usz tick_before = m->sim->getTickNumber();
	
	// Press clock button (rising edge)
	m->params[AhabModule::CLK_PARAM].setValue(1.0f);
	m->process({});
	
	// Tick should have incremented
	REQUIRE(m->sim->getTickNumber() > tick_before);
	
	// Release button
	m->params[AhabModule::CLK_PARAM].setValue(0.0f);
	m->process({});
	
	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("Run/stop toggle", "[Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);
	
	REQUIRE(m->simRunning == true);
	
	// Press run button (Schmitt trigger needs low-to-high transition)
	m->params[AhabModule::RUN_PARAM].setValue(0.0f);
	m->process({});
	m->params[AhabModule::RUN_PARAM].setValue(1.0f);
	m->process({});
	
	// Should stop
	REQUIRE(m->simRunning == false);
	
	// Press again
	m->params[AhabModule::RUN_PARAM].setValue(0.0f);
	m->process({});
	m->params[AhabModule::RUN_PARAM].setValue(1.0f);
	m->process({});
	
	// Should start
	REQUIRE(m->simRunning == true);
	
	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("Reset input triggers tick counter reset", "[Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);
	
	m->process({});
	
	// Manually advance the tick counter several times via clock button
	m->params[AhabModule::CLK_PARAM].setValue(0.0f);
	m->process({});
	m->params[AhabModule::CLK_PARAM].setValue(1.0f);
	m->process({});
	m->params[AhabModule::CLK_PARAM].setValue(0.0f);
	m->process({});
	
	// Get tick number after several advances
	Usz ticks_before = m->sim->getTickNumber();
	REQUIRE(ticks_before > 0);
	
	// Trigger reset input with low-to-high transition
	m->inputs[AhabModule::RESET_INPUT].channels = 1;
	m->inputs[AhabModule::RESET_INPUT].setVoltage(0.0f);
	m->process({});
	m->inputs[AhabModule::RESET_INPUT].setVoltage(5.0f);
	m->process({});
	
	// Tick counter should be reset to 0
	REQUIRE(m->sim->getTickNumber() == 0);
	
	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("Reset input responds to edge detection", "[Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);
	
	m->process({});
	
	// Advance tick counter
	m->params[AhabModule::CLK_PARAM].setValue(1.0f);
	m->process({});
	Usz ticks_after_advance = m->sim->getTickNumber();
	REQUIRE(ticks_after_advance > 0);
	
	// Apply reset voltage but don't release (no edge yet)
	m->inputs[AhabModule::RESET_INPUT].channels = 1;
	m->inputs[AhabModule::RESET_INPUT].setVoltage(5.0f);
	m->process({});
	m->process({});
	m->process({});
	
	// Tick counter should remain at 0 (held in reset state)
	REQUIRE(m->sim->getTickNumber() == 0);
	
	// Release reset and advance again
	m->inputs[AhabModule::RESET_INPUT].setVoltage(0.0f);
	m->process({});
	
	// Now trigger clock to advance
	m->params[AhabModule::CLK_PARAM].setValue(0.0f);
	m->process({});
	m->params[AhabModule::CLK_PARAM].setValue(1.0f);
	m->process({});
	
	// Tick should have incremented from 0
	REQUIRE(m->sim->getTickNumber() > 0);
	
	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("Reset input with gate signal (continuous voltage)", "[Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);
	
	m->process({});
	
	// Advance ticks
	for (int i = 0; i < 5; i++) {
		m->params[AhabModule::CLK_PARAM].setValue(1.0f);
		m->process({});
		m->params[AhabModule::CLK_PARAM].setValue(0.0f);
		m->process({});
	}
	Usz ticks_advanced = m->sim->getTickNumber();
	REQUIRE(ticks_advanced > 0);
	
	// Apply reset gate signal (gate voltage held high)
	m->inputs[AhabModule::RESET_INPUT].channels = 1;
	m->inputs[AhabModule::RESET_INPUT].setVoltage(8.0f);
	m->process({});
	
	// Reset should trigger on the rising edge
	REQUIRE(m->sim->getTickNumber() == 0);
	
	// Further processing with gate held high should NOT reset again
	// (edge detector should not trigger on continued high voltage)
	m->process({});
	m->process({});
	REQUIRE(m->sim->getTickNumber() == 0);
	
	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("CV input reading", "[Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);
	
	// Set input voltages and mark as connected (must set channels directly in test environment)
	m->inputs[AhabModule::IN_INPUT + 0].channels = 1;
	m->inputs[AhabModule::IN_INPUT + 0].setVoltage(5.0f);
	m->inputs[AhabModule::IN_INPUT + 1].channels = 1;
	m->inputs[AhabModule::IN_INPUT + 1].setVoltage(10.0f);
	m->inputs[AhabModule::IN_INPUT + 2].channels = 1;
	m->inputs[AhabModule::IN_INPUT + 2].setVoltage(0.0f);
	m->inputs[AhabModule::IN_INPUT + 3].channels = 1;
	m->inputs[AhabModule::IN_INPUT + 3].setVoltage(7.5f);
	
	// Process to update internal state
	m->process({});
	
	// Verify inputs are connected
	REQUIRE(m->inputs[AhabModule::IN_INPUT + 0].isConnected() == true);
	
	// Read values
	REQUIRE(m->readDspInput(0) == 5.0f);
	REQUIRE(m->readDspInput(1) == 10.0f);
	REQUIRE(m->readDspInput(2) == 0.0f);
	REQUIRE(m->readDspInput(3) == 7.5f);
	
	// Test clamping
	m->inputs[AhabModule::IN_INPUT + 0].channels = 1;
	m->inputs[AhabModule::IN_INPUT + 0].setVoltage(15.0f);
	REQUIRE(m->readDspInput(0) == 10.0f);
	
	m->inputs[AhabModule::IN_INPUT + 0].channels = 1;
	m->inputs[AhabModule::IN_INPUT + 0].setVoltage(-5.0f);
	REQUIRE(m->readDspInput(0) == 0.0f);
	
	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("CV output writing", "[Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);
	
	m->process({});
	
	// Write values
	m->writeDspOutput(0, 5.0f);
	m->writeDspOutput(1, 10.0f);
	m->writeDspOutput(2, 0.0f);
	m->writeDspOutput(3, 7.5f);
	
	// Read back
	REQUIRE(m->outputs[AhabModule::OUT_OUTPUT + 0].getVoltage() == 5.0f);
	REQUIRE(m->outputs[AhabModule::OUT_OUTPUT + 1].getVoltage() == 10.0f);
	REQUIRE(m->outputs[AhabModule::OUT_OUTPUT + 2].getVoltage() == 0.0f);
	REQUIRE(m->outputs[AhabModule::OUT_OUTPUT + 3].getVoltage() == 7.5f);
	
	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("CV output gate scheduling with non-zero gateTicks", "[Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);

	m->process({});

	// Write a gated CV output on port 2.
	m->writeDspOutput(2, 8.5f, 3);
	REQUIRE(m->outputs[AhabModule::OUT_OUTPUT + 2].getVoltage() == 8.5f);
	REQUIRE(m->scheduledOffs.size() == 1);
	REQUIRE(m->scheduledOffs[0].remaining_ticks == 3);
	REQUIRE(m->scheduledOffs[0].channel == 2);
	REQUIRE(m->scheduledOffs[0].note == 255);

	Oevent_list emptyEvents;
	oevent_list_init(&emptyEvents);

	// Countdown: 3 -> 2 -> 1 -> 0 (no gate-off yet)
	m->processEvents(&emptyEvents);
	REQUIRE(m->outputs[AhabModule::OUT_OUTPUT + 2].getVoltage() == 8.5f);
	REQUIRE(m->scheduledOffs.size() == 1);
	REQUIRE(m->scheduledOffs[0].remaining_ticks == 2);

	m->processEvents(&emptyEvents);
	REQUIRE(m->outputs[AhabModule::OUT_OUTPUT + 2].getVoltage() == 8.5f);
	REQUIRE(m->scheduledOffs.size() == 1);
	REQUIRE(m->scheduledOffs[0].remaining_ticks == 1);

	m->processEvents(&emptyEvents);
	REQUIRE(m->outputs[AhabModule::OUT_OUTPUT + 2].getVoltage() == 8.5f);
	REQUIRE(m->scheduledOffs.size() == 1);
	REQUIRE(m->scheduledOffs[0].remaining_ticks == 0);

	// Next tick performs the gate-off and removes the scheduled event.
	m->processEvents(&emptyEvents);
	REQUIRE(m->outputs[AhabModule::OUT_OUTPUT + 2].getVoltage() == 0.0f);
	REQUIRE(m->scheduledOffs.empty() == true);

	oevent_list_deinit(&emptyEvents);

	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("CV output gate scheduling clamps output port", "[Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);

	m->process({});

	// Port index > 3 must clamp to output 3 and schedule gate-off there.
	m->writeDspOutput(99, 6.0f, 2);
	REQUIRE(m->outputs[AhabModule::OUT_OUTPUT + 3].getVoltage() == 6.0f);
	REQUIRE(m->scheduledOffs.size() == 1);
	REQUIRE(m->scheduledOffs[0].channel == 3);
	REQUIRE(m->scheduledOffs[0].remaining_ticks == 2);

	Oevent_list emptyEvents;
	oevent_list_init(&emptyEvents);
	m->processEvents(&emptyEvents);
	m->processEvents(&emptyEvents);
	m->processEvents(&emptyEvents);
	REQUIRE(m->outputs[AhabModule::OUT_OUTPUT + 3].getVoltage() == 0.0f);
	REQUIRE(m->scheduledOffs.empty() == true);
	oevent_list_deinit(&emptyEvents);

	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("onReset restores all defaults", "[Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);

	// Corrupt a bunch of state.
	m->midiVirtualPortId = 3;
	m->midiOutEnabled = false;
	m->midiOutPort.channel = 5;
	m->midiCcOffset = 99;
	m->overwriteZeroNoteDuration = false;
	m->gridStepCol = 1;
	m->gridStepRow = 2;
	m->simRunning = false;
	Usz h, w;
	REQUIRE(m->sim->loadRectFromOrcaRequest(":04C21\n*.....", 0, 0, h, w, true) == true);
	m->process({});
	REQUIRE(m->sim->getFieldHeight() != 25); // field was changed away from default

	// Reset restores every default.
	Module::ResetEvent e;
	m->onReset(e);

	REQUIRE(m->midiVirtualPortId == 0);
	REQUIRE(m->midiOutEnabled == true);
	REQUIRE(m->midiOutPort.channel == -1);
	REQUIRE(m->midiCcOffset == 64);
	REQUIRE(m->overwriteZeroNoteDuration == true);
	REQUIRE(m->gridStepCol == 8);
	REQUIRE(m->gridStepRow == 8);
	REQUIRE(m->simRunning == true);
	REQUIRE(m->sim->getFieldHeight() == 25);
	REQUIRE(m->sim->getFieldWidth() == 49);

	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("Clock output pulse", "[Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);
	
	m->simRunning = true;
	
	// Trigger a step (Schmitt trigger needs low-to-high transition)
	m->params[AhabModule::CLK_PARAM].setValue(0.0f);
	m->process({});
	m->params[AhabModule::CLK_PARAM].setValue(1.0f);
	m->process({});
	
	// Clock output should be high
	float v = m->outputs[AhabModule::CLK_OUTPUT].getVoltage();
	REQUIRE(v > 5.0f);
	
	// Process more to let pulse decay
	for (int i = 0; i < 1000; ++i) {
		m->process(Test::makeProcessArgs(i));
	}
	
	// Clock output should be low
	v = m->outputs[AhabModule::CLK_OUTPUT].getVoltage();
	REQUIRE(v < 1.0f);
	
	Test::unregisterModule(m);
	Test::destroyModule(m);
}


// Clocking edge cases: the run/stop, manual clock and external
// clock paths are checked here by their resulting step behaviour (tick counter
// / CLK_OUTPUT pulse), not just by the simRunning flag.

TEST_CASE("Run/stop gates BPM stepping", "[Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);

	// High BPM so a modest sample count produces several ticks (16 Hz at 240).
	m->params[AhabModule::BPM_PARAM].setValue(240.0f);
	float sampleRate = 44100.f;
	int64_t frame = 0;
	int numSamples = (int)(sampleRate / 16.0f * 3); // ~3 ticks worth

	// Stop via the run button (Schmitt trigger low→high).
	m->params[AhabModule::RUN_PARAM].setValue(0.0f);
	m->process(Test::makeProcessArgs(frame++, sampleRate));
	m->params[AhabModule::RUN_PARAM].setValue(1.0f);
	m->process(Test::makeProcessArgs(frame++, sampleRate));
	REQUIRE(m->simRunning == false);

	// ~3 ticks worth of samples while stopped: zero steps.
	Usz tickBefore = m->sim->getTickNumber();
	for (int i = 0; i < numSamples; ++i) {
		m->process(Test::makeProcessArgs(frame++, sampleRate));
	}
	REQUIRE(m->sim->getTickNumber() == tickBefore);

	// Start again; the same sample count advances the tick counter.
	m->params[AhabModule::RUN_PARAM].setValue(0.0f);
	m->process(Test::makeProcessArgs(frame++, sampleRate));
	m->params[AhabModule::RUN_PARAM].setValue(1.0f);
	m->process(Test::makeProcessArgs(frame++, sampleRate));
	REQUIRE(m->simRunning == true);

	tickBefore = m->sim->getTickNumber();
	for (int i = 0; i < numSamples; ++i) {
		m->process(Test::makeProcessArgs(frame++, sampleRate));
	}
	REQUIRE(m->sim->getTickNumber() > tickBefore);

	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("Manual clock steps while stopped", "[Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);

	m->simRunning = false; // stopped

	// Press the clock button (Schmitt trigger low→high).
	m->params[AhabModule::CLK_PARAM].setValue(0.0f);
	m->process({});
	Usz tickBefore = m->sim->getTickNumber();
	m->params[AhabModule::CLK_PARAM].setValue(1.0f);
	m->process({});

	// The manual clock always steps, even while stopped.
	REQUIRE(m->sim->getTickNumber() == tickBefore + 1);

	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("External clock disables internal BPM", "[Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);

	m->params[AhabModule::BPM_PARAM].setValue(240.0f); // 16 Hz
	float sampleRate = 44100.f;
	int64_t frame = 0;

	// Connect the external clock (channels > 0 → isConnected()) and hold it at
	// a DC level: no rising edges, so no external steps occur.
	m->inputs[AhabModule::CLK_INPUT].channels = 1;
	m->inputs[AhabModule::CLK_INPUT].setVoltage(5.0f);

	// ~3 ticks worth of samples. If BPM were active this would step; the
	// connected external clock must disable the BPM accumulator.
	Usz tickBefore = m->sim->getTickNumber();
	int numSamples = (int)(sampleRate / 16.0f * 3);
	for (int i = 0; i < numSamples; ++i) {
		m->process(Test::makeProcessArgs(frame++, sampleRate));
	}
	REQUIRE(m->sim->getTickNumber() == tickBefore);

	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("External clock does not step while stopped", "[Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);

	m->simRunning = false; // stopped

	// Rising edge on the external clock while stopped must not step.
	m->inputs[AhabModule::CLK_INPUT].channels = 1;
	m->inputs[AhabModule::CLK_INPUT].setVoltage(0.0f);
	m->process({}); // initialize the trigger low
	Usz tickBefore = m->sim->getTickNumber();
	m->inputs[AhabModule::CLK_INPUT].setVoltage(10.0f);
	m->process({}); // rising edge — but simRunning is false

	REQUIRE(m->sim->getTickNumber() == tickBefore);

	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("Clock phase resets on run toggle and external clock", "[Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);

	float sampleRate = 44100.f;
	int64_t frame = 0;
	m->params[AhabModule::BPM_PARAM].setValue(240.0f);

	// Accumulate some BPM phase (well under one tick: 1000 * 16/44100 ≈ 0.36).
	for (int i = 0; i < 1000; ++i) {
		m->process(Test::makeProcessArgs(frame++, sampleRate));
	}
	REQUIRE(m->clockPhase > 0.0f);

	// Toggling run off then on resets the phase accumulator. The reset lands at
	// ~0, not exactly 0: the BPM path re-accumulates a single sample's worth in
	// the same process() call that restarts the run (simRunning is already true
	// when the phase-accumulation block runs).
	m->params[AhabModule::RUN_PARAM].setValue(0.0f);
	m->process(Test::makeProcessArgs(frame++, sampleRate));
	m->params[AhabModule::RUN_PARAM].setValue(1.0f);
	m->process(Test::makeProcessArgs(frame++, sampleRate));
	m->params[AhabModule::RUN_PARAM].setValue(0.0f);
	m->process(Test::makeProcessArgs(frame++, sampleRate));
	m->params[AhabModule::RUN_PARAM].setValue(1.0f);
	m->process(Test::makeProcessArgs(frame++, sampleRate));
	REQUIRE(m->clockPhase < 0.001f);

	// An external clock edge also resets the phase accumulator.
	for (int i = 0; i < 1000; ++i) {
		m->process(Test::makeProcessArgs(frame++, sampleRate));
	}
	REQUIRE(m->clockPhase > 0.0f);
	m->inputs[AhabModule::CLK_INPUT].channels = 1;
	m->inputs[AhabModule::CLK_INPUT].setVoltage(0.0f);
	m->process(Test::makeProcessArgs(frame++, sampleRate));
	m->inputs[AhabModule::CLK_INPUT].setVoltage(10.0f);
	m->process(Test::makeProcessArgs(frame++, sampleRate)); // edge → phase = 0
	REQUIRE(m->clockPhase == 0.0f);

	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("Clock output pulse width", "[Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);

	float sampleRate = 44100.f;
	int64_t frame = 0;
	m->simRunning = false; // no BPM retriggers; pulse driven only by manual clock

	// Step once → clkPulseGen.trigger(0.01) → 10ms CLK_OUTPUT pulse.
	m->params[AhabModule::CLK_PARAM].setValue(0.0f);
	m->process(Test::makeProcessArgs(frame++, sampleRate));
	m->params[AhabModule::CLK_PARAM].setValue(1.0f);
	m->process(Test::makeProcessArgs(frame++, sampleRate));
	REQUIRE(m->outputs[AhabModule::CLK_OUTPUT].getVoltage() > 5.0f);

	// ~5ms in, still high (10ms pulse).
	for (int i = 0; i < 220; ++i) {
		m->process(Test::makeProcessArgs(frame++, sampleRate));
	}
	REQUIRE(m->outputs[AhabModule::CLK_OUTPUT].getVoltage() > 5.0f);

	// Past 10ms, the pulse has decayed to 0V.
	for (int i = 0; i < 600; ++i) {
		m->process(Test::makeProcessArgs(frame++, sampleRate));
	}
	REQUIRE(m->outputs[AhabModule::CLK_OUTPUT].getVoltage() < 1.0f);

	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("Integration test - preset loading and simulation", "[Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);
	
	// Parse the preset JSON
	json_error_t jerr;
	json_t* presetJ = json_loads(Ahab_vcvm, 0, &jerr);
	REQUIRE(presetJ != nullptr);
	
	// Extract the data section
	json_t* dataJ = json_object_get(presetJ, "data");
	REQUIRE(dataJ != nullptr);
	
	// Load preset data into module
	m->dataFromJson(dataJ);
	
	// Verify preset data was loaded correctly
	REQUIRE(m->midiOutEnabled == true);
	REQUIRE(m->midiCcOffset == 64);
	REQUIRE(m->overwriteZeroNoteDuration == true);
	REQUIRE(m->gridStepCol == 8);
	REQUIRE(m->gridStepRow == 8);
	REQUIRE(m->simRunning == true);

	// The preset's UDP/OSC keys live inside "sim" and are restored through
	// udpOutput->fromJson(simJ) — pins that the stored JSON format is unchanged.
	REQUIRE(m->udpOutput->getUdpAddress() == "127.0.0.1");
	REQUIRE(m->udpOutput->getUdpPort() == "49161");
	REQUIRE(m->udpOutput->getOscAddress() == "127.0.0.1");
	REQUIRE(m->udpOutput->getOscPort() == "49162");
	
	// Setup mock MIDI output to capture generated events
	MockMidiOutputDevice* mockDevice = setupMockMidiOutput(m);
	
	// Step the VM so the preset's ORCA pattern (a MIDI sequencer of ':'/'%' note
	// operators driven by its clock/delay operators) actually runs and emits
	// MIDI. The previous version pumped m->process({}) ~160 times, which at the
	// default 44.1kHz sample rate never accumulates enough BPM clock phase for a
	// single tick — so no MIDI ever fired and the only assertion
	// (getMessageCount() >= 0) was trivially true.
	for (int step = 0; step < 256; ++step) {
		stepSim(m);
	}
	
	// Stepping the sequencer must produce real MIDI output.
	REQUIRE(mockDevice->getMessageCount() > 0);
	
	// Verify every generated message is a valid note/CC/pitchbend event
	for (size_t i = 0; i < mockDevice->getMessageCount(); ++i) {
		const auto& msg = mockDevice->getMessage(i);
		uint8_t status = msg.getStatus();
		
		// Verify message is a valid note/CC/pitchbend event
		bool isValidMessage = (status == 0x9 || status == 0x8 || // Note On/Off
							   status == 0xB ||                   // CC
							   status == 0xE);                    // Pitchbend
		REQUIRE(isValidMessage);
		
		// Verify basic MIDI structure (3-byte message)
		REQUIRE(msg.getSize() == 3);
		
		// For note messages, verify note number is valid
		if (status == 0x9 || status == 0x8) {
			uint8_t note = msg.getNote();
			REQUIRE(note <= 127);
		}
	}
	
	json_decref(presetJ);
	cleanupMockMidiOutput(m, mockDevice);
	
	Test::unregisterModule(m);
	Test::destroyModule(m);
}
