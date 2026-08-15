#pragma once
// Test cases for the Ahab MIDI subsystem: MIDI output through AhabModule's
// midiOutPort (rack::midi) and the virtual MIDI driver (Ahab::Midi). Included
// by Ahab.test.cpp, which brings Ahab.cpp into the TU first so AhabModule is
// fully defined.

#include "Ahab.test.hpp"


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

TEST_CASE("MIDI Virtual reset - sends note-off to all notes and channels", "[MIDI][Ahab]") {
	// Enable virtual MIDI driver
	pluginSettings.ahabMidiVirtualEnabled = true;
	Ahab::Midi::init();

	// Subscribe a mock input to port 0 to capture reset messages
	MockMidiVirtualInput* mockVirtualInput = setupMockVirtualMidiInput(0);

	Ahab::Midi::resetMidi(0);

	// Note-off message for every note (0-127) on every channel (0-15)
	REQUIRE(mockVirtualInput->getMessageCount() == 16 * 128);

	for (int ch = 0; ch < 16; ++ch) {
		for (int note = 0; note < 128; ++note) {
			int index = ch * 128 + note;
			const auto& msg = mockVirtualInput->getMessage(index);
			REQUIRE(msg.getStatus() == 0x8);   // Note Off
			REQUIRE(msg.getChannel() == ch);
			REQUIRE(msg.getNote() == note);
			REQUIRE(msg.getValue() == 0);
		}
	}

	cleanupMockVirtualMidiInput(mockVirtualInput);
}

TEST_CASE("MIDI Virtual reset - guards and port isolation", "[MIDI][Ahab]") {
	// Enable virtual MIDI driver
	pluginSettings.ahabMidiVirtualEnabled = true;
	Ahab::Midi::init();

	// Subscribe a mock input to port 1
	MockMidiVirtualInput* mockVirtualInput = setupMockVirtualMidiInput(1);

	// Invalid device IDs are ignored
	Ahab::Midi::resetMidi(-1);
	REQUIRE(mockVirtualInput->getMessageCount() == 0);
	Ahab::Midi::resetMidi(4);
	REQUIRE(mockVirtualInput->getMessageCount() == 0);

	// Resetting another port does not deliver to this one
	Ahab::Midi::resetMidi(0);
	REQUIRE(mockVirtualInput->getMessageCount() == 0);

	// Resetting the subscribed port delivers note-off for all notes and channels
	Ahab::Midi::resetMidi(1);
	REQUIRE(mockVirtualInput->getMessageCount() == 16 * 128);

	cleanupMockVirtualMidiInput(mockVirtualInput);
}

TEST_CASE("Sim reset command drops pending note-offs and blasts All Notes Off", "[MIDI][Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);

	// Enable virtual MIDI driver and subscribe mocks on both MIDI outputs
	pluginSettings.ahabMidiVirtualEnabled = true;
	Ahab::Midi::init();
	MockMidiVirtualInput* mockVirtualInput = setupMockVirtualMidiInput(0);
	MockMidiOutputDevice* mockDevice = setupMockMidiOutput(m);
	m->midiVirtualPortId = 0;
	m->midiOutEnabled = true;

	// A couple of pending note-offs
	m->scheduledOffs.push_back({3, 0, 60});
	m->scheduledOffs.push_back({1, 2, 62});

	// Reset command processed on the DSP thread
	m->sim->resetRequest();
	m->process({});

	// Pending note-offs dropped and All Notes Off blasted across both MIDI outputs
	REQUIRE(m->scheduledOffs.empty());
	REQUIRE(mockVirtualInput->getMessageCount() == 16 * 128);
	REQUIRE(mockDevice->getMessageCount() == 16 * 128);

	cleanupMockMidiOutput(m, mockDevice);
	cleanupMockVirtualMidiInput(mockVirtualInput);
	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("Field replace drops pending note-offs and blasts All Notes Off", "[MIDI][Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);

	// Enable virtual MIDI driver and subscribe mocks on both MIDI outputs
	pluginSettings.ahabMidiVirtualEnabled = true;
	Ahab::Midi::init();
	MockMidiVirtualInput* mockVirtualInput = setupMockVirtualMidiInput(0);
	MockMidiOutputDevice* mockDevice = setupMockMidiOutput(m);
	m->midiVirtualPortId = 0;
	m->midiOutEnabled = true;

	m->scheduledOffs.push_back({3, 0, 60});
	m->scheduledOffs.push_back({1, 2, 62});

	// Load a new field (REPLACE_FIELD command)
	Usz h, w;
	REQUIRE(m->sim->loadRectFromOrcaRequest("AB\nCD", 0, 0, h, w, true) == true);
	m->process({});

	// Pending note-offs are dropped and All Notes Off blasted across both MIDI outputs
	REQUIRE(m->scheduledOffs.empty());
	REQUIRE(mockVirtualInput->getMessageCount() == 16 * 128);
	REQUIRE(mockDevice->getMessageCount() == 16 * 128);

	cleanupMockMidiOutput(m, mockDevice);
	cleanupMockVirtualMidiInput(mockVirtualInput);
	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("onReset sends All Notes Off via virtual MIDI", "[MIDI][Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);

	// Enable virtual MIDI driver and subscribe a mock on port 0
	pluginSettings.ahabMidiVirtualEnabled = true;
	Ahab::Midi::init();
	MockMidiVirtualInput* mockVirtualInput = setupMockVirtualMidiInput(0);

	m->scheduledOffs.push_back({3, 0, 60});

	Module::ResetEvent e;
	m->onReset(e);

	// Note: the midiOutPort All Notes Off blast is not asserted here because
	// onReset() resets midiOutPort (detaching any device) before flushNotes()
	// runs via sim->reset(). The midiOutPort blast is covered by the
	// reset-command and field-replace tests above.

	// Pending note-offs dropped and All Notes Off blasted across all channels
	REQUIRE(m->scheduledOffs.empty());
	REQUIRE(mockVirtualInput->getMessageCount() == 16 * 128);
	for (int i = 0; i < 16 * 128; ++i) {
		REQUIRE(mockVirtualInput->getMessage(i).getStatus() == 0x8);
	}

	cleanupMockVirtualMidiInput(mockVirtualInput);
	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("Clear field flushes pending note-offs and blasts All Notes Off", "[MIDI][Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);

	// Enable virtual MIDI driver and subscribe mocks on both MIDI outputs
	pluginSettings.ahabMidiVirtualEnabled = true;
	Ahab::Midi::init();
	MockMidiVirtualInput* mockVirtualInput = setupMockVirtualMidiInput(0);
	MockMidiOutputDevice* mockDevice = setupMockMidiOutput(m);
	m->midiVirtualPortId = 0;
	m->midiOutEnabled = true;

	m->scheduledOffs.push_back({3, 0, 60});
	m->scheduledOffs.push_back({1, 2, 62});

	// Replicate AhabSimWidget::simClear(): a reset that clears the whole field
	// (keeping the field size) and flushes notes.
	Usz fh = m->sim->getFieldHeight();
	Usz fw = m->sim->getFieldWidth();
	m->sim->resetRequest();
	m->process({});

	// Pending note-offs dropped, All Notes Off blasted across both MIDI outputs,
	// and the field size is preserved.
	REQUIRE(m->scheduledOffs.empty());
	REQUIRE(mockVirtualInput->getMessageCount() == 16 * 128);
	REQUIRE(mockDevice->getMessageCount() == 16 * 128);
	REQUIRE(m->sim->getFieldHeight() == fh);
	REQUIRE(m->sim->getFieldWidth() == fw);

	cleanupMockMidiOutput(m, mockDevice);
	cleanupMockVirtualMidiInput(mockVirtualInput);
	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("MIDI output preserves the outgoing message channel", "[MIDI][Ahab]") {
	AhabModule* m = Test::createModule<AhabModule>("Ahab");
	Test::registerModule(m);

	// Simulate the bug: a non-negative port channel (e.g. restored from a preset)
	// would overwrite the channel of every outgoing MIDI message.
	m->midiOutPort.channel = 5;

	// onReset must reset the port channel to -1 so outgoing messages keep their
	// own channel (the fix).
	Module::ResetEvent e;
	m->onReset(e);
	REQUIRE(m->midiOutPort.channel == -1);

	// Attach a mock device WITHOUT touching the channel. (onReset reset the port,
	// detaching any device; setupMockMidiOutput would also set channel = -1 and
	// mask the bug, so attach manually.)
	MockMidiOutputDevice* mockDevice = new MockMidiOutputDevice();
	m->midiOutPort.outputDevice = mockDevice;
	m->midiOutPort.device = mockDevice;
	m->midiOutEnabled = true;

	// A note event on channel 2
	Oevent_list olist;
	oevent_list_init(&olist);
	Oevent* ev = oevent_list_alloc_item(&olist);
	ev->any.oevent_type = Oevent_type_midi_note;
	ev->midi_note.channel = 2;
	ev->midi_note.octave = 4;
	ev->midi_note.note = 0; // C
	ev->midi_note.velocity = 100;
	ev->midi_note.duration = 1;
	m->processEvents(&olist);

	// The outgoing note-on keeps the source channel (not overwritten)
	REQUIRE(mockDevice->getMessageCount() == 1);
	const auto& msg = mockDevice->getMessage(0);
	REQUIRE(msg.getStatus() == 0x9); // Note On
	REQUIRE(msg.getChannel() == 2);
	REQUIRE(msg.getNote() == 48); // C4
	REQUIRE(msg.getValue() == 100);

	oevent_list_deinit(&olist);
	cleanupMockMidiOutput(m, mockDevice);
	Test::unregisterModule(m);
	Test::destroyModule(m);
}
