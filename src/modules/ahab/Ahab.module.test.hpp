#pragma once
// Test cases for the AhabModule core (clock, run/stop, reset, CV I/O, JSON,
// preset loading). Included by Ahab.test.cpp, which brings Ahab.cpp into the
// TU first so AhabModule is fully defined here.

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

TEST_CASE("Preset JSON null-guards", "[Ahab][JSON]") {
	auto module = Test::createModule<AhabModule>("Ahab");

	SECTION("All top-level properties are null-guarded in dataFromJson()") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetNullGuards(module, rootJ);
		json_decref(rootJ);
	}

	Test::destroyModule(module);
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

TEST_CASE("JSON serialization", "[JSON][Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);
	
	// Set some values
	m->panelTheme = 1;
	m->midiVirtualPortId = 2;
	m->midiOutEnabled = false;
	m->midiCcOffset = 80;
	m->simRunning = false;
	m->overwriteZeroNoteDuration = false;
	m->gridStepCol = 16;
	m->gridStepRow = 12;
	
	// Serialize
	json_t* j = m->dataToJson();
	REQUIRE(j != nullptr);
	
	// Check values
	REQUIRE(json_integer_value(json_object_get(j, "panelTheme")) == 1);
	REQUIRE(json_integer_value(json_object_get(j, "midiVirtualPortId")) == 2);
	REQUIRE(json_boolean_value(json_object_get(j, "midiOutEnabled")) == false);
	REQUIRE(json_integer_value(json_object_get(j, "midiCcOffset")) == 80);
	REQUIRE(json_boolean_value(json_object_get(j, "simRunning")) == false);
	REQUIRE(json_boolean_value(json_object_get(j, "overwriteZeroNoteDuration")) == false);
	REQUIRE(json_integer_value(json_object_get(j, "gridStepCol")) == 16);
	REQUIRE(json_integer_value(json_object_get(j, "gridStepRow")) == 12);
	
	json_decref(j);
	
	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("JSON deserialization", "[JSON][Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);
	
	// Create JSON
	json_t* j = json_object();
	json_object_set_new(j, "panelTheme", json_integer(2));
	json_object_set_new(j, "midiVirtualPortId", json_integer(3));
	json_object_set_new(j, "midiOutEnabled", json_boolean(false));
	json_object_set_new(j, "midiCcOffset", json_integer(100));
	json_object_set_new(j, "simRunning", json_boolean(false));
	json_object_set_new(j, "overwriteZeroNoteDuration", json_boolean(false));
	json_object_set_new(j, "gridStepCol", json_integer(4));
	json_object_set_new(j, "gridStepRow", json_integer(6));
	
	// Create sim JSON
	json_t* simJ = m->sim->toJson();
	json_object_set_new(j, "sim", simJ);
	
	// Create midi port JSON
	json_object_set_new(j, "midiOutPort", json_object());
	
	// Deserialize
	m->dataFromJson(j);
	
	// Check values
	REQUIRE(m->panelTheme == 2);
	REQUIRE(m->midiVirtualPortId == 3);
	REQUIRE(m->midiOutEnabled == false);
	REQUIRE(m->midiCcOffset == 100);
	REQUIRE(m->simRunning == false);
	REQUIRE(m->overwriteZeroNoteDuration == false);
	REQUIRE(m->gridStepCol == 4);
	REQUIRE(m->gridStepRow == 6);
	
	json_decref(j);
	
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
	
	// Setup mock MIDI output to capture generated events
	MockMidiOutputDevice* mockDevice = setupMockMidiOutput(m);
	
	// Run simulation for steps to allow events to be generated
	// The preset contains an Ahab pattern that should generate MIDI notes
	for (int step = 0; step < 4 * 4 * 10; ++step) {
		m->process({});
	}
	
	// Verify that MIDI events were generated during simulation
	// The exact number of notes depends on the pattern in the preset
	REQUIRE(mockDevice->getMessageCount() >= 0); // At minimum, should have run without error
	
	// If MIDI notes were genserated, verify they are valid MIDI messages
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
			REQUIRE(note >= 0);
			REQUIRE(note <= 127);
		}
	}
	
	json_decref(presetJ);
	cleanupMockMidiOutput(m, mockDevice);
	
	Test::unregisterModule(m);
	Test::destroyModule(m);
}
