#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "Ahab.cpp"

using namespace StoermelderPackOne;
using namespace StoermelderPackOne::Ahab;

// Define the single instance used by tests
static Test::TestContext<> testContext;


TEST_CASE("AhabModule construction and initialization", "[Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	
	REQUIRE(m != nullptr);
	REQUIRE(m->sim != nullptr);
	REQUIRE(m->simRunning == true);
	REQUIRE(m->midiOutEnabled == true);
	REQUIRE(m->midiCcOffset == 64);
	REQUIRE(m->gridStepCol == 8);
	REQUIRE(m->gridStepRow == 8);
	
	Test::destroyModule(m);
}

TEST_CASE("AhabModule reset", "[Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);
	
	// Change some values
	m->simRunning = false;
	m->midiOutEnabled = false;
	m->midiCcOffset = 100;
	
	// Reset
	m->onReset();
	
	// Check that values are back to defaults
	REQUIRE(m->simRunning == true);
	REQUIRE(m->midiOutEnabled == true);
	REQUIRE(m->midiCcOffset == 64);
	REQUIRE(m->sim->getTickNumber() == 0);
	REQUIRE(m->sim->getFieldHeight() == 25);
	REQUIRE(m->sim->getFieldWidth() == 49);
	
	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("AhabModule BPM-based clock", "[Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);
	
	// Set BPM to 120 (default)
	m->params[AhabModule::BPM_PARAM].setValue(120.0f);
	m->simRunning = true;
	
	Usz tick_before = m->sim->getTickNumber();
	
	// Process enough samples to trigger a clock tick
	// At 120 BPM with 4x multiplier, we get 8 Hz clock rate
	// At 44100 sample rate, that's 44100/8 = 5512.5 samples per tick
	Module::ProcessArgs args;
	args.sampleRate = 44100.f;
	args.sampleTime = 1.f / args.sampleRate;
	
	for (int i = 0; i < 6000; ++i) {
		m->process(args);
	}
	
	// Tick should have incremented
	REQUIRE(m->sim->getTickNumber() > tick_before);
	
	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("AhabModule external clock input", "[Ahab]") {
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

TEST_CASE("AhabModule manual clock button", "[Ahab]") {
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

TEST_CASE("AhabModule run/stop toggle", "[Ahab]") {
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

TEST_CASE("AhabModule CV input reading", "[Ahab]") {
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

TEST_CASE("AhabModule CV output writing", "[Ahab]") {
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

TEST_CASE("AhabModule MIDI note event handling", "[Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);
	
	// Create a mock MIDI note event
	Oevent_list olist;
	oevent_list_init(&olist);
	
	Oevent* ev = oevent_list_alloc_item(&olist);
	ev->any.oevent_type = Oevent_type_midi_note;
	ev->midi_note.channel = 0;
	ev->midi_note.octave = 4;
	ev->midi_note.note = 0; // C
	ev->midi_note.velocity = 100;
	ev->midi_note.duration = 5;
	
	// Process events
	m->processEvents(&olist);
	
	// Should have scheduled a note-off (processEvents decrements immediately)
	REQUIRE(m->midiScheduledNotes.size() == 1);
	REQUIRE(m->midiScheduledNotes[0].remaining_ticks == 4); // Decremented from 5 to 4
	REQUIRE(m->midiScheduledNotes[0].note == 48); // C4
	
	oevent_list_deinit(&olist);
	
	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("AhabModule scheduled note-off countdown", "[Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);
	
	// Manually add a scheduled note
	m->midiScheduledNotes.push_back({3, 0, 60});
	
	// Create empty event list to trigger countdown
	Oevent_list olist;
	oevent_list_init(&olist);
	
	// Process 3 times
	m->processEvents(&olist);
	REQUIRE(m->midiScheduledNotes.size() == 1);
	REQUIRE(m->midiScheduledNotes[0].remaining_ticks == 2);
	
	m->processEvents(&olist);
	REQUIRE(m->midiScheduledNotes.size() == 1);
	REQUIRE(m->midiScheduledNotes[0].remaining_ticks == 1);
	
	m->processEvents(&olist);
	REQUIRE(m->midiScheduledNotes.size() == 1);
	REQUIRE(m->midiScheduledNotes[0].remaining_ticks == 0);
	
	// Next call should send note-off and clear
	m->processEvents(&olist);
	REQUIRE(m->midiScheduledNotes.size() == 0);
	
	oevent_list_deinit(&olist);
	
	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("AhabModule MIDI CC event handling", "[Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);
	
	m->midiCcOffset = 64;
	
	// Create a mock MIDI CC event
	Oevent_list olist;
	oevent_list_init(&olist);
	
	Oevent* ev = oevent_list_alloc_item(&olist);
	ev->any.oevent_type = Oevent_type_midi_cc;
	ev->midi_cc.channel = 1;
	ev->midi_cc.control = 10;
	ev->midi_cc.value = 50;
	
	// Process events (should not crash)
	m->processEvents(&olist);
	
	oevent_list_deinit(&olist);
	
	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("AhabModule MIDI pitchbend event handling", "[Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);
	
	// Create a mock MIDI pitchbend event
	Oevent_list olist;
	oevent_list_init(&olist);
	
	Oevent* ev = oevent_list_alloc_item(&olist);
	ev->any.oevent_type = Oevent_type_midi_pb;
	ev->midi_pb.channel = 2;
	ev->midi_pb.lsb = 0x40;
	ev->midi_pb.msb = 0x20;
	
	// Process events (should not crash)
	m->processEvents(&olist);
	
	oevent_list_deinit(&olist);
	
	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("AhabModule zero duration note handling", "[Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);
	
	m->overwriteZeroNoteDuration = true;
	
	// Create a mock MIDI note event with duration 0
	Oevent_list olist;
	oevent_list_init(&olist);
	
	Oevent* ev = oevent_list_alloc_item(&olist);
	ev->any.oevent_type = Oevent_type_midi_note;
	ev->midi_note.channel = 0;
	ev->midi_note.octave = 5;
	ev->midi_note.note = 3;
	ev->midi_note.velocity = 80;
	ev->midi_note.duration = 0;
	
	// Process events
	m->processEvents(&olist);
	
	// Should have scheduled a note-off with 1 tick duration (decremented to 0 immediately)
	REQUIRE(m->midiScheduledNotes.size() == 1);
	REQUIRE(m->midiScheduledNotes[0].remaining_ticks == 0); // Decremented from 1 to 0
	
	oevent_list_deinit(&olist);
	
	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("AhabModule JSON serialization", "[Ahab]") {
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

TEST_CASE("AhabModule JSON deserialization", "[Ahab]") {
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

TEST_CASE("AhabModule clock output pulse", "[Ahab]") {
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
	Module::ProcessArgs args;
	args.sampleTime = 1.f / 44100.f;
	for (int i = 0; i < 1000; ++i) {
		m->process(args);
	}
	
	// Clock output should be low
	v = m->outputs[AhabModule::CLK_OUTPUT].getVoltage();
	REQUIRE(v < 1.0f);
	
	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("AhabModule lights update", "[Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);
	
	m->simRunning = true;
	
	// Process several times to update lights
	Module::ProcessArgs args;
	args.sampleRate = 44100.f;
	args.sampleTime = 1.f / args.sampleRate;
	
	for (int i = 0; i < 1000; ++i) {
		m->process(args);
	}
	
	// Run light should be on
	REQUIRE(m->lights[AhabModule::RUN_LIGHT].getBrightness() > 0.5f);
	
	// Stop running
	m->simRunning = false;
	
	for (int i = 0; i < 1000; ++i) {
		m->process(args);
	}
	
	// Run light should be off
	REQUIRE(m->lights[AhabModule::RUN_LIGHT].getBrightness() < 0.5f);
	
	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("AhabModule sample rate change", "[Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);
	
	// Trigger sample rate change
	m->onSampleRateChange();
	
	// Should not crash and light divider should update
	// (we can't easily test the divider value directly)
	
	Test::unregisterModule(m);
	Test::destroyModule(m);
}
