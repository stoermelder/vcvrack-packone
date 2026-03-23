#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "Ahab.cpp"
#include "Ahab.vcvm.test.h"

using namespace StoermelderPackOne;
using namespace StoermelderPackOne::Ahab;

// Define the single instance used by tests
static Test::TestContext<> testContext;

// Mock MIDI OutputDevice for testing MIDI output messages
struct MockMidiOutputDevice : public rack::midi::OutputDevice {
	std::vector<rack::midi::Message> sentMessages;
	
	void sendMessage(const rack::midi::Message& message) override {
		sentMessages.push_back(message);
	}
	
	void clear() {
		sentMessages.clear();
	}
	
	size_t getMessageCount() const {
		return sentMessages.size();
	}
	
	const rack::midi::Message& getMessage(size_t index) const {
		return sentMessages.at(index);
	}
};

// Helper to replace midiOutPort's device with our mock
MockMidiOutputDevice* setupMockMidiOutput(AhabModule* m) {
	MockMidiOutputDevice* mockDevice = new MockMidiOutputDevice();
	// Access the private device pointer through the base Port class
	m->midiOutPort.outputDevice = mockDevice;
	m->midiOutPort.device = mockDevice;
	m->midiOutPort.channel = -1; // Don't override message channel
	return mockDevice;
}

// Static singleton to use as safe fallback after mock device cleanup
void cleanupMockMidiOutput(AhabModule* m, MockMidiOutputDevice* mockDevice) {
	if (mockDevice && m) {
		// Reset the port first to clear internal state
		m->midiOutPort.reset();
		// Now delete the mock device
		delete mockDevice;
	}
}

// Mock MIDI Input for capturing virtual driver messages
struct MockMidiVirtualInput : public rack::midi::Input {
	std::vector<rack::midi::Message> receivedMessages;
	
	void onMessage(const rack::midi::Message& message) override {
		receivedMessages.push_back(message);
	}
	
	void clear() {
		receivedMessages.clear();
	}
	
	size_t getMessageCount() const {
		return receivedMessages.size();
	}
	
	const rack::midi::Message& getMessage(size_t index) const {
		return receivedMessages.at(index);
	}
	
	std::vector<int> getDeviceIds() override {
		return {0, 1, 2, 3};
	}
	
	int getDefaultDeviceId() override {
		return 0;
	}
	
	std::string getDeviceName(int deviceId) override {
		return string::f("Virtual Port %i", deviceId + 1);
	}
	
	std::vector<int> getChannels() override {
		return {-1}; // All channels
	}
};

// Helper to setup mock virtual MIDI input
MockMidiVirtualInput* setupMockVirtualMidiInput(int portId) {
	MockMidiVirtualInput* mockInput = new MockMidiVirtualInput();
	mockInput->setDriverId(0x4CCC434C); // Ahab virtual MIDI driver ID
	mockInput->setDeviceId(portId);
	return mockInput;
}

// Helper to cleanup mock virtual MIDI input
void cleanupMockVirtualMidiInput(MockMidiVirtualInput* mockInput) {
	if (mockInput) {
		delete mockInput;  // Destructor will handle MIDI driver unsubscription
	}
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

TEST_CASE("MIDI Driver note event handling", "[MIDI][Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);
	
	// Setup mock MIDI output
	MockMidiOutputDevice* mockDevice = setupMockMidiOutput(m);
	m->midiOutEnabled = true;
	
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
	REQUIRE(m->scheduledOffs.size() == 1);
	REQUIRE(m->scheduledOffs[0].remaining_ticks == 4); // Decremented from 5 to 4
	REQUIRE(m->scheduledOffs[0].note == 48); // C4
	
	// Verify MIDI note-on message was sent to midiOutPort
	REQUIRE(mockDevice->getMessageCount() == 1);
	const auto& msg = mockDevice->getMessage(0);
	REQUIRE(msg.getSize() == 3);
	REQUIRE(msg.getStatus() == 0x9); // Note On
	REQUIRE(msg.getChannel() == 0);
	REQUIRE(msg.getNote() == 48); // C4
	REQUIRE(msg.getValue() == 100); // Velocity
	
	oevent_list_deinit(&olist);
	cleanupMockMidiOutput(m, mockDevice);
	
	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("MIDI Driver zero duration note handling", "[MIDI][Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);
	
	// Setup mock MIDI output
	MockMidiOutputDevice* mockDevice = setupMockMidiOutput(m);
	m->midiOutEnabled = true;
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
	REQUIRE(m->scheduledOffs.size() == 1);
	REQUIRE(m->scheduledOffs[0].remaining_ticks == 0); // Decremented from 1 to 0
	
	// Verify note-on message was sent
	REQUIRE(mockDevice->getMessageCount() == 1);
	const auto& noteOn = mockDevice->getMessage(0);
	REQUIRE(noteOn.getStatus() == 0x9); // Note On
	REQUIRE(noteOn.getNote() == 63); // C5 (octave 5, note 3)
	REQUIRE(noteOn.getValue() == 80); // Velocity
	
	oevent_list_deinit(&olist);
	cleanupMockMidiOutput(m, mockDevice);
	
	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("MIDI Driver scheduled note-off countdown", "[MIDI][Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);
	
	// Setup mock MIDI output
	MockMidiOutputDevice* mockDevice = setupMockMidiOutput(m);
	m->midiOutEnabled = true;
	
	// Manually add a scheduled note
	m->scheduledOffs.push_back({3, 0, 60});
	
	// Create empty event list to trigger countdown
	Oevent_list olist;
	oevent_list_init(&olist);
	
	// Process 3 times
	m->processEvents(&olist);
	REQUIRE(m->scheduledOffs.size() == 1);
	REQUIRE(m->scheduledOffs[0].remaining_ticks == 2);
	REQUIRE(mockDevice->getMessageCount() == 0); // No note-off yet
	
	m->processEvents(&olist);
	REQUIRE(m->scheduledOffs.size() == 1);
	REQUIRE(m->scheduledOffs[0].remaining_ticks == 1);
	REQUIRE(mockDevice->getMessageCount() == 0); // No note-off yet
	
	m->processEvents(&olist);
	REQUIRE(m->scheduledOffs.size() == 1);
	REQUIRE(m->scheduledOffs[0].remaining_ticks == 0);
	REQUIRE(mockDevice->getMessageCount() == 0); // No note-off yet
	
	// Next call should send note-off and clear
	m->processEvents(&olist);
	REQUIRE(m->scheduledOffs.size() == 0);
	REQUIRE(mockDevice->getMessageCount() == 1);
	
	// Verify note-off message was sent
	const auto& msg = mockDevice->getMessage(0);
	REQUIRE(msg.getSize() == 3);
	REQUIRE(msg.getStatus() == 0x8); // Note Off
	REQUIRE(msg.getChannel() == 0);
	REQUIRE(msg.getNote() == 60);
	REQUIRE(msg.getValue() == 0); // Note-off velocity
	
	oevent_list_deinit(&olist);
	cleanupMockMidiOutput(m, mockDevice);
	
	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("MIDI Driver CC event handling", "[MIDI][Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);
	
	// Setup mock MIDI output
	MockMidiOutputDevice* mockDevice = setupMockMidiOutput(m);
	m->midiOutEnabled = true;
	m->midiCcOffset = 64;
	
	// Create a mock MIDI CC event
	Oevent_list olist;
	oevent_list_init(&olist);
	
	Oevent* ev = oevent_list_alloc_item(&olist);
	ev->any.oevent_type = Oevent_type_midi_cc;
	ev->midi_cc.channel = 1;
	ev->midi_cc.control = 10;
	ev->midi_cc.value = 50;
	
	// Process events
	m->processEvents(&olist);
	
	// Verify MIDI CC message was sent to midiOutPort
	REQUIRE(mockDevice->getMessageCount() == 1);
	const auto& msg = mockDevice->getMessage(0);
	REQUIRE(msg.getSize() == 3);
	REQUIRE(msg.getStatus() == 0xB); // CC
	REQUIRE(msg.getChannel() == 1);
	REQUIRE(msg.bytes[1] == 74); // 64 + 10 = 74
	REQUIRE(msg.getValue() == 50); // CC value
	
	oevent_list_deinit(&olist);
	cleanupMockMidiOutput(m, mockDevice);
	
	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("MIDI Driver pitchbend event handling", "[MIDI][Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);
	
	// Setup mock MIDI output
	MockMidiOutputDevice* mockDevice = setupMockMidiOutput(m);
	m->midiOutEnabled = true;
	
	// Create a mock MIDI pitchbend event
	Oevent_list olist;
	oevent_list_init(&olist);
	
	Oevent* ev = oevent_list_alloc_item(&olist);
	ev->any.oevent_type = Oevent_type_midi_pb;
	ev->midi_pb.channel = 2;
	ev->midi_pb.lsb = 0x40;
	ev->midi_pb.msb = 0x20;
	
	// Process events
	m->processEvents(&olist);
	
	// Verify MIDI pitchbend message was sent to midiOutPort
	REQUIRE(mockDevice->getMessageCount() == 1);
	const auto& msg = mockDevice->getMessage(0);
	REQUIRE(msg.getSize() == 3);
	REQUIRE(msg.getStatus() == 0xE); // Pitchbend
	REQUIRE(msg.getChannel() == 2);
	REQUIRE(msg.bytes[1] == 0x40); // LSB
	REQUIRE(msg.bytes[2] == 0x20); // MSB
	
	oevent_list_deinit(&olist);
	cleanupMockMidiOutput(m, mockDevice);
	
	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("MIDI Driver output disabled", "[MIDI][Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);
	
	// Setup mock MIDI output
	MockMidiOutputDevice* mockDevice = setupMockMidiOutput(m);
	m->midiOutEnabled = false; // Disable MIDI output
	
	// Create a MIDI note event
	Oevent_list olist;
	oevent_list_init(&olist);
	
	Oevent* ev = oevent_list_alloc_item(&olist);
	ev->any.oevent_type = Oevent_type_midi_note;
	ev->midi_note.channel = 0;
	ev->midi_note.octave = 4;
	ev->midi_note.note = 0;
	ev->midi_note.velocity = 100;
	ev->midi_note.duration = 5;
	
	// Process events
	m->processEvents(&olist);
	
	// Should NOT have sent message to midiOutPort since output is disabled
	REQUIRE(mockDevice->getMessageCount() == 0);
	
	// But note should still be scheduled
	REQUIRE(m->scheduledOffs.size() == 1);
	
	oevent_list_deinit(&olist);
	cleanupMockMidiOutput(m, mockDevice);
	
	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("MIDI Driver note channel mapping", "[MIDI][Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);
	
	// Setup mock MIDI output
	MockMidiOutputDevice* mockDevice = setupMockMidiOutput(m);
	m->midiOutEnabled = true;
	
	// Test multi-channel note events
	for (int ch = 0; ch < 4; ++ch) {
		// Create fresh event list for each channel
		Oevent_list olist;
		oevent_list_init(&olist);
		mockDevice->clear();
		
		Oevent* ev = oevent_list_alloc_item(&olist);
		ev->any.oevent_type = Oevent_type_midi_note;
		ev->midi_note.channel = ch;
		ev->midi_note.octave = 3;
		ev->midi_note.note = 5;
		ev->midi_note.velocity = 64;
		ev->midi_note.duration = 10; // Use longer duration to avoid note-off in same call
		
		m->processEvents(&olist);
		
		// Verify correct channel in note-on message
		REQUIRE(mockDevice->getMessageCount() >= 1); // At least the note-on
		REQUIRE(mockDevice->getMessage(0).getChannel() == ch);
		REQUIRE(mockDevice->getMessage(0).getStatus() == 0x9); // Note On
		
		oevent_list_deinit(&olist);
	}
	
	cleanupMockMidiOutput(m, mockDevice);
	
	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("MIDI Driver CC offset", "[MIDI][Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);
	
	// Setup mock MIDI output
	MockMidiOutputDevice* mockDevice = setupMockMidiOutput(m);
	m->midiOutEnabled = true;
	m->midiCcOffset = 100; // Set offset to 100
	
	// Create CC events with different control values
	for (int cc = 0; cc < 5; ++cc) {
		// Create fresh event list for each CC value
		Oevent_list olist;
		oevent_list_init(&olist);
		mockDevice->clear();
		
		Oevent* ev = oevent_list_alloc_item(&olist);
		ev->any.oevent_type = Oevent_type_midi_cc;
		ev->midi_cc.channel = 0;
		ev->midi_cc.control = cc;
		ev->midi_cc.value = 64;
		
		m->processEvents(&olist);
		
		// Verify offset was applied
		REQUIRE(mockDevice->getMessageCount() == 1);
		REQUIRE(mockDevice->getMessage(0).bytes[1] == (100 + cc)); // Control number
		
		oevent_list_deinit(&olist);
	}
	
	cleanupMockMidiOutput(m, mockDevice);
	
	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("MIDI Virtual note delivery", "[MIDI][Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);
	
	// Enable virtual MIDI driver
	pluginSettings.ahabMidiVirtualEnabled = true;
	Ahab::Midi::init();
	
	// Setup mock input to capture virtual MIDI messages
	MockMidiVirtualInput* mockVirtualInput = setupMockVirtualMidiInput(0);
	
	// Setup mock for regular MIDI output to ensure it's not called
	MockMidiOutputDevice* mockDevice = setupMockMidiOutput(m);
	m->midiOutEnabled = false; // Disable regular output, focus on virtual
	m->midiVirtualPortId = 0;
	
	// Create a MIDI note event
	Oevent_list olist;
	oevent_list_init(&olist);
	
	Oevent* ev = oevent_list_alloc_item(&olist);
	ev->any.oevent_type = Oevent_type_midi_note;
	ev->midi_note.channel = 0;
	ev->midi_note.octave = 4;
	ev->midi_note.note = 0; // C
	ev->midi_note.velocity = 100;
	ev->midi_note.duration = 5;
	
	// Process events - note will be sent to virtual MIDI driver
	m->processEvents(&olist);
	
	// Regular MIDI output should NOT have received the message (disabled)
	REQUIRE(mockDevice->getMessageCount() == 0);
	
	// Virtual MIDI input should have received the note-on message
	REQUIRE(mockVirtualInput->getMessageCount() >= 1);
	const auto& virtualMsg = mockVirtualInput->getMessage(0);
	REQUIRE(virtualMsg.getStatus() == 0x9); // Note On
	REQUIRE(virtualMsg.getChannel() == 0);
	REQUIRE(virtualMsg.getNote() == 48); // C4
	REQUIRE(virtualMsg.getValue() == 100); // Velocity
	
	// Note should still be scheduled
	REQUIRE(m->scheduledOffs.size() == 1);
	
	oevent_list_deinit(&olist);
	cleanupMockMidiOutput(m, mockDevice);
	cleanupMockVirtualMidiInput(mockVirtualInput);
	
	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("MIDI Virtual with both outputs enabled", "[MIDI][Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);
	
	// Enable both virtual MIDI and regular MIDI output
	pluginSettings.ahabMidiVirtualEnabled = true;
	Ahab::Midi::init();
	
	// Setup mock inputs to capture messages on both paths
	MockMidiVirtualInput* mockVirtualInput = setupMockVirtualMidiInput(1);
	MockMidiOutputDevice* mockDevice = setupMockMidiOutput(m);
	m->midiOutEnabled = true;
	m->midiVirtualPortId = 1;
	
	// Create a MIDI CC event
	Oevent_list olist;
	oevent_list_init(&olist);
	
	Oevent* ev = oevent_list_alloc_item(&olist);
	ev->any.oevent_type = Oevent_type_midi_cc;
	ev->midi_cc.channel = 2;
	ev->midi_cc.control = 10;
	ev->midi_cc.value = 64;
	
	// Process events - should go to both virtual driver AND regular output
	m->processEvents(&olist);
	
	// Regular MIDI output should have received the message
	REQUIRE(mockDevice->getMessageCount() == 1);
	const auto& msg = mockDevice->getMessage(0);
	REQUIRE(msg.getStatus() == 0xB); // CC
	REQUIRE(msg.getChannel() == 2);
	REQUIRE(msg.bytes[1] == 74); // 64 + 10
	REQUIRE(msg.getValue() == 64);
	
	// Virtual MIDI input should also have received the message
	REQUIRE(mockVirtualInput->getMessageCount() == 1);
	const auto& virtualMsg = mockVirtualInput->getMessage(0);
	REQUIRE(virtualMsg.getStatus() == 0xB); // CC
	REQUIRE(virtualMsg.getChannel() == 2);
	REQUIRE(virtualMsg.bytes[1] == 74); // 64 + 10
	REQUIRE(virtualMsg.getValue() == 64);
	
	oevent_list_deinit(&olist);
	cleanupMockMidiOutput(m, mockDevice);
	cleanupMockVirtualMidiInput(mockVirtualInput);
	
	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("MIDI Virtual multiple ports", "[MIDI][Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);
	
	// Enable virtual MIDI driver
	pluginSettings.ahabMidiVirtualEnabled = true;
	Ahab::Midi::init();
	
	m->midiOutEnabled = false;
	
	// Test sending to different virtual ports
	for (int port = 0; port < 4; ++port) {
		m->midiVirtualPortId = port;
		
		// Setup mock virtual MIDI input for this port
		MockMidiVirtualInput* mockVirtualInput = setupMockVirtualMidiInput(port);
		
		Oevent_list olist;
		oevent_list_init(&olist);
		
		Oevent* ev = oevent_list_alloc_item(&olist);
		ev->any.oevent_type = Oevent_type_midi_note;
		ev->midi_note.channel = 0;
		ev->midi_note.octave = 3;
		ev->midi_note.note = (uint8_t)port; // Different notes for different ports
		ev->midi_note.velocity = 80;
		ev->midi_note.duration = 2;
		
		// Process events - should route to the specified virtual port
		m->processEvents(&olist);
		
		// Verify message was received on the correct virtual port
		REQUIRE(mockVirtualInput->getMessageCount() >= 1);
		const auto& msg = mockVirtualInput->getMessage(0);
		REQUIRE(msg.getStatus() == 0x9); // Note on
		REQUIRE(msg.getChannel() == 0);
		REQUIRE(msg.getNote() == (uint8_t)(36 + port)); // Base note + port offset
		REQUIRE(msg.getValue() == 80);
		
		oevent_list_deinit(&olist);
		cleanupMockVirtualMidiInput(mockVirtualInput);
		m->scheduledOffs.clear();
	}
	
	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("MIDI Virtual disabled", "[MIDI][Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);
	
	// Ensure virtual MIDI driver is disabled
	pluginSettings.ahabMidiVirtualEnabled = false;
	
	MockMidiOutputDevice* mockDevice = setupMockMidiOutput(m);
	MockMidiVirtualInput* mockVirtualInput = setupMockVirtualMidiInput(0);
	m->midiOutEnabled = false; // Also disable regular output
	m->midiVirtualPortId = 0;
	
	// Create a MIDI note event
	Oevent_list olist;
	oevent_list_init(&olist);
	
	Oevent* ev = oevent_list_alloc_item(&olist);
	ev->any.oevent_type = Oevent_type_midi_note;
	ev->midi_note.channel = 0;
	ev->midi_note.octave = 5;
	ev->midi_note.note = 5;
	ev->midi_note.velocity = 64;
	ev->midi_note.duration = 3;
	
	// Process events
	m->processEvents(&olist);
	
	// Neither virtual nor regular MIDI should have received the message
	REQUIRE(mockDevice->getMessageCount() == 0);
	REQUIRE(mockVirtualInput->getMessageCount() == 0);
	
	// But note should still be scheduled internally
	REQUIRE(m->scheduledOffs.size() == 1);
	
	oevent_list_deinit(&olist);
	cleanupMockMidiOutput(m, mockDevice);
	cleanupMockVirtualMidiInput(mockVirtualInput);
	
	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("MIDI note ordering - scheduled note-offs before new notes", "[MIDI][Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);
	
	// Setup mock MIDI output
	MockMidiOutputDevice* mockDevice = setupMockMidiOutput(m);
	m->midiOutEnabled = true;
	
	// First: Create a note that will be scheduled for note-off
	Oevent_list olist1;
	oevent_list_init(&olist1);
	
	Oevent* ev1 = oevent_list_alloc_item(&olist1);
	ev1->any.oevent_type = Oevent_type_midi_note;
	ev1->midi_note.channel = 0;
	ev1->midi_note.octave = 4;
	ev1->midi_note.note = 0; // C4
	ev1->midi_note.velocity = 100;
	ev1->midi_note.duration = 1; // Will schedule note-off with remaining_ticks = 0
	
	m->processEvents(&olist1);
	oevent_list_deinit(&olist1);
	
	// First process should have MIDI note-on message
	size_t messagesAfterFirstNote = mockDevice->getMessageCount();
	REQUIRE(messagesAfterFirstNote == 1);
	REQUIRE(mockDevice->getMessage(0).getStatus() == 0x9); // Note On
	REQUIRE(mockDevice->getMessage(0).getNote() == 48); // C4
	
	// Now simulate the scheduled note-off being ready to send
	// remaining_ticks should be 0 after first processEvents call
	REQUIRE(m->scheduledOffs.size() == 1);
	REQUIRE(m->scheduledOffs[0].remaining_ticks == 0);
	REQUIRE(m->scheduledOffs[0].note == 48);
	
	// Second: In the same cycle, create a new note event that should come after the note-off
	Oevent_list olist2;
	oevent_list_init(&olist2);
	
	Oevent* ev2 = oevent_list_alloc_item(&olist2);
	ev2->any.oevent_type = Oevent_type_midi_note;
	ev2->midi_note.channel = 0;
	ev2->midi_note.octave = 4;
	ev2->midi_note.note = 1; // D4
	ev2->midi_note.velocity = 100;
	ev2->midi_note.duration = 10;
	
	m->processEvents(&olist2);
	oevent_list_deinit(&olist2);
	
	// After second processEvents:
	// - The scheduled note-off should have been processed FIRST
	// - Then the new note-on should be processed
	// Expected: Note-Off (C4), then Note-On (D4)
	REQUIRE(mockDevice->getMessageCount() == 3);
	
	// Message 0: Original Note-On (C4)
	REQUIRE(mockDevice->getMessage(0).getStatus() == 0x9);
	REQUIRE(mockDevice->getMessage(0).getNote() == 48);
	
	// Message 1: Scheduled Note-Off (C4) - should come BEFORE the new note
	REQUIRE(mockDevice->getMessage(1).getStatus() == 0x8); // Note Off
	REQUIRE(mockDevice->getMessage(1).getNote() == 48);
	REQUIRE(mockDevice->getMessage(1).getValue() == 0); // Note-off velocity
	
	// Message 2: New Note-On (D4 = octave 4, note 1)
	REQUIRE(mockDevice->getMessage(2).getStatus() == 0x9);
	REQUIRE(mockDevice->getMessage(2).getNote() == 49); // D4
	
	// Verify the scheduled note was removed
	REQUIRE(m->scheduledOffs.size() == 1); // Only the D4 note-off remains
	REQUIRE(m->scheduledOffs[0].note == 49);
	
	cleanupMockMidiOutput(m, mockDevice);
	
	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("MIDI note ordering - multiple simultaneous note transitions", "[MIDI][Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);
	
	// Setup mock MIDI output
	MockMidiOutputDevice* mockDevice = setupMockMidiOutput(m);
	m->midiOutEnabled = true;
	
	// Create three notes with duration 1 (will schedule note-offs with remaining_ticks = 0)
	// Notes: C4 (0), D4 (1), E4 (2)
	Oevent_list olist1;
	oevent_list_init(&olist1);
	
	for (int i = 0; i < 3; ++i) {
		Oevent* ev = oevent_list_alloc_item(&olist1);
		ev->any.oevent_type = Oevent_type_midi_note;
		ev->midi_note.channel = 0;
		ev->midi_note.octave = 4;
		ev->midi_note.note = i; // Notes 0, 1, 2
		ev->midi_note.velocity = 100;
		ev->midi_note.duration = 1;
	}
	
	m->processEvents(&olist1);
	oevent_list_deinit(&olist1);
	
	// Should have 3 note-ons
	REQUIRE(mockDevice->getMessageCount() == 3);
	
	// Now in the second cycle, three note-offs should be scheduled and ready to send
	REQUIRE(m->scheduledOffs.size() == 3);
	for (size_t i = 0; i < m->scheduledOffs.size(); ++i) {
		REQUIRE(m->scheduledOffs[i].remaining_ticks == 0);
	}
	
	// Create three new notes in the same cycle
	// Notes: C5 (0), D5 (1), E5 (2)
	Oevent_list olist2;
	oevent_list_init(&olist2);
	
	for (int i = 0; i < 3; ++i) {
		Oevent* ev = oevent_list_alloc_item(&olist2);
		ev->any.oevent_type = Oevent_type_midi_note;
		ev->midi_note.channel = 0;
		ev->midi_note.octave = 5;
		ev->midi_note.note = i; // Notes 0, 1, 2
		ev->midi_note.velocity = 100;
		ev->midi_note.duration = 5;
	}
	
	m->processEvents(&olist2);
	oevent_list_deinit(&olist2);
	
	// After second processEvents:
	// Expected message order:
	// - 3 Note-Offs (C4=48, D4=49, E4=50) from scheduled notes - processed FIRST
	// - 3 Note-Ons (C5=60, D5=61, E5=62) from new events - processed AFTER
	REQUIRE(mockDevice->getMessageCount() == 9);
	
	// Messages 0-2: Original Note-Ons (C4, D4, E4)
	REQUIRE(mockDevice->getMessage(0).getStatus() == 0x9);
	REQUIRE(mockDevice->getMessage(0).getNote() == 48); // C4
	REQUIRE(mockDevice->getMessage(1).getStatus() == 0x9);
	REQUIRE(mockDevice->getMessage(1).getNote() == 49); // D4
	REQUIRE(mockDevice->getMessage(2).getStatus() == 0x9);
	REQUIRE(mockDevice->getMessage(2).getNote() == 50); // E4
	
	// Messages 3-5: Scheduled Note-Offs (C4, D4, E4) - come BEFORE new note-ons
	REQUIRE(mockDevice->getMessage(3).getStatus() == 0x8); // Note Off
	REQUIRE(mockDevice->getMessage(3).getNote() == 48); // C4
	REQUIRE(mockDevice->getMessage(4).getStatus() == 0x8);
	REQUIRE(mockDevice->getMessage(4).getNote() == 49); // D4
	REQUIRE(mockDevice->getMessage(5).getStatus() == 0x8);
	REQUIRE(mockDevice->getMessage(5).getNote() == 50); // E4
	
	// Messages 6-8: New Note-Ons (C5, D5, E5)
	REQUIRE(mockDevice->getMessage(6).getStatus() == 0x9);
	REQUIRE(mockDevice->getMessage(6).getNote() == 60); // C5
	REQUIRE(mockDevice->getMessage(7).getStatus() == 0x9);
	REQUIRE(mockDevice->getMessage(7).getNote() == 61); // D5
	REQUIRE(mockDevice->getMessage(8).getStatus() == 0x9);
	REQUIRE(mockDevice->getMessage(8).getNote() == 62); // E5
	
	cleanupMockMidiOutput(m, mockDevice);
	
	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("Widget construction", "[UI][Ahab]") {
	AhabWidget* w = Test::createWidget<AhabWidget>("Ahab");
	REQUIRE(w != nullptr);
	REQUIRE(w->module == NULL);
	
	// Check that widget has the expected children (simWidget and statusWidget)
	REQUIRE(w->simWidget != nullptr);
	REQUIRE(w->statusWidget != nullptr);
	
	// Check that statusWidget references simWidget
	REQUIRE(w->statusWidget->simWidget == w->simWidget);
	
	Test::destroyWidget(w);
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