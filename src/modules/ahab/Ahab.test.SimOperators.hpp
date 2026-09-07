#pragma once
// Test cases for the ORCA operators implemented by AhabSim: the custom vcvin /
// vcvout operators and the E bang propagation. Included by AhabSim.test.cpp.

#include "Ahab.test.hpp"


// custom_vcvin / custom_vcvout are C callbacks registered with the ORCA VM in
// AhabSim.cpp; they live in the plugin dylib the test binary links against.
extern "C" Usz custom_vcvin(void* ptr, Usz port_num, Usz a, Usz b);
extern "C" void custom_vcvout(void* ptr, Usz port_index, Usz a, Usz b, Usz value);

TEST_CASE("Op vcvin ports 1-4 map to voltage range", "[AhabSim]") {
	AhabSim sim;
	size_t in_port = 0.f; float in_voltage = 10.0f;

	// Port 1 maps to dsp input index 0
	sim.setDspInputReader([&](size_t p){ return p == in_port ? in_voltage : 5.0f; });
	void* ptr = (void*)sim.getEvents();
	// port 1: 10v, mapping should saturate to upper bound -> expect 15 for range [5,15]
	REQUIRE(custom_vcvin(ptr, 1, 5, 15) == 15);
	// port 2, 3, 4: 5v input -> expect mid value 12 for range [5,15]
	REQUIRE(custom_vcvin(ptr, 2, 5, 15) == 10);
	REQUIRE(custom_vcvin(ptr, 3, 5, 15) == 10);
	REQUIRE(custom_vcvin(ptr, 4, 5, 15) == 10);

	// With input=0v, expect lower bound
	sim.setDspInputReader([](size_t p){ return 0.0f; });
	REQUIRE(custom_vcvin(ptr, 1, 5, 15) == 5);
	REQUIRE(custom_vcvin(ptr, 2, 5, 15) == 5);
	REQUIRE(custom_vcvin(ptr, 3, 5, 15) == 5);
	REQUIRE(custom_vcvin(ptr, 4, 5, 15) == 5);
}

TEST_CASE("Op vcvin ports A-D map to semitone mod12", "[AhabSim]") {
	AhabSim sim;
	size_t in_port = 0; float in_voltage = 0.75f;

	// Letter port 10 maps to dsp input index 0
	sim.setDspInputReader([&](size_t p){ return p == in_port ? in_voltage : 0.0f; });
	void* ptr = (void*)sim.getEvents();
	// port A (10): 0.75 * 12 = 9 -> rounds to 9
	REQUIRE(custom_vcvin(ptr, 10, 0, 0) == 9);
	// port B, C, D (11-13): 0.0 * 12 = 0
	REQUIRE(custom_vcvin(ptr, 11, 0, 0) == 0);
	REQUIRE(custom_vcvin(ptr, 12, 0, 0) == 0);
	REQUIRE(custom_vcvin(ptr, 13, 0, 0) == 0);
}

TEST_CASE("Op vcvin with missing max defaults to 35 #425", "[AhabSim]") {
	AhabSim sim;

	// Set port 1 to full-scale 10V to exercise 0-35 mapping.
	sim.setDspInputReader([](size_t p){ return 10.0f; });

	Usz out_h, out_w;
	REQUIRE(sim.loadRectFromOrcaRequest(".<.1..\n.*....", 0, 0, out_h, out_w, true) == true);
	REQUIRE(out_h == 2);
	REQUIRE(out_w == 6);
	// Apply the loaded field
	sim.process();

	// Execute one simulation tick (requires step request/process cycle)
	sim.stepRequest();
	sim.process();

	// The operator should interpret missing max as 35, giving output value 35 -> 'z'
	Usz h, w;
	sim.getDisplayBuffer(h, w);
	REQUIRE(h == 2);
	REQUIRE(w == 6);
	Glyph const* buffer = sim.getFieldBuffer();
	REQUIRE(buffer[1 * w + 1] == 'z');
}

TEST_CASE("Op vcvout ports 1-4 write scaled voltages", "[AhabSim]") {
	AhabSim sim;
	size_t out_port = 99; float out_voltage = 0.0f; int out_gate_ticks = -1;
	void* ptr = (void*)sim.getEvents();
	sim.setDspOutputWriter([&](size_t p, float v, int g){ out_port = p; out_voltage = v; out_gate_ticks = g; });

	// Clamp value=35 in range [0,35] -> voltage should be 10.0f
	custom_vcvout(ptr, 1, 0, 35, 35);
	REQUIRE(out_port == 0);
	REQUIRE(out_voltage == Catch::Approx(10.0f));
	REQUIRE(out_gate_ticks == 0);

	// Test mid value: value=20 in range [10,30] -> voltage should be 5v
	custom_vcvout(ptr, 2, 10, 30, 20);
	REQUIRE(out_port == 1);
	REQUIRE(out_voltage == Catch::Approx(5.0f));
	REQUIRE(out_gate_ticks == 0);

	// Test lower bound: value=0 in range [5,25] -> voltage should be 0v
	custom_vcvout(ptr, 3, 5, 25, 0);
	REQUIRE(out_port == 2);
	REQUIRE(out_voltage == Catch::Approx(0.0f));
	REQUIRE(out_gate_ticks == 0);

	// Test upper bound: value=20 in range [5,15] -> voltage should be 10v
	custom_vcvout(ptr, 4, 5, 15, 20);
	REQUIRE(out_port == 3);
	REQUIRE(out_voltage == Catch::Approx(10.0f));
	REQUIRE(out_gate_ticks == 0);
}

TEST_CASE("Op vcvout ports A-D write v/oct conversion", "[AhabSim]") {
	AhabSim sim;
	size_t out_port = 99; float out_voltage = 0.0f; int out_gate_ticks = -1;
	void* ptr = (void*)sim.getEvents();
	sim.setDspOutputWriter([&](size_t p, float v, int g){ out_port = p; out_voltage = v; out_gate_ticks = g; });
	
	// Letter port A (10) -> port 0. For a=1, value=3 -> (3 + 1*12)/12 = 1.25
	custom_vcvout(ptr, 10, 1, 4, 3);
	REQUIRE(out_port == 0);
	REQUIRE(out_voltage == Catch::Approx(1.f + 3 * 1.f / 12.f));
	REQUIRE(out_gate_ticks == 4);

	// Letter port B (11) -> port 1. For a=0, value=0 -> (0 + 0*12)/12 = 0.0
	custom_vcvout(ptr, 11, 0, 7, 0);
	REQUIRE(out_port == 1);
	REQUIRE(out_voltage == Catch::Approx(0.0f));
	REQUIRE(out_gate_ticks == 7);

	// Letter port C (12) -> port 2. For a=2, value=6 -> (6 + 2*12)/12 = 2.5
	custom_vcvout(ptr, 12, 2, 9, 12);
	REQUIRE(out_port == 2);
	REQUIRE(out_voltage == Catch::Approx(3.f));
	REQUIRE(out_gate_ticks == 9);
}

TEST_CASE("Op vcvout letter-port gate length uses b parameter", "[AhabSim]") {
	AhabSim sim;
	size_t out_port = 99;
	float out_voltage = 0.0f;
	int out_gate_ticks = -1;
	void* ptr = (void*)sim.getEvents();
	sim.setDspOutputWriter([&](size_t p, float v, int g){ out_port = p; out_voltage = v; out_gate_ticks = g; });

	// Letter port D (13) -> output port index 3.
	// a = octave, b = gate ticks.
	custom_vcvout(ptr, 13, 3, 11, 6);
	REQUIRE(out_port == 3);
	REQUIRE(out_voltage == Catch::Approx((6.f + 36.f) / 12.f));
	REQUIRE(out_gate_ticks == 11);
}

TEST_CASE("Successive E bang separation #426", "[AhabSim]") {
	AhabSim sim;
	
	// Use a wide row to allow E operators to propagate.
	sim.setFieldSizeRequest(1, 10, false);
	sim.process();
	Usz h, w;
	sim.getDisplayBuffer(h, w);
	REQUIRE(h == 1);
	REQUIRE(w == 10);
	Glyph const* buffer = sim.getFieldBuffer();

	// First E injection at the left edge
	sim.setGlyphRequest(0, 9, '#', Mark_flag_input, false);
	sim.setGlyphRequest(0, 0, 'E', Mark_flag_input, false);
	sim.process();
	REQUIRE(buffer[0] == 'E');
	REQUIRE(buffer[9] == '#');

	// Advance two ticks so this E moves to x=2
	for (int i = 0; i < 2; ++i) {
		sim.stepRequest();
		sim.process();
	}
	REQUIRE(buffer[2] == 'E');
	REQUIRE(buffer[0] == '.'); // Original position should be cleared
	REQUIRE(buffer[9] == '#'); // Should still be there

	// Insert a second E at the origin and watch both move with separation.
	sim.setGlyphRequest(0, 0, 'E', Mark_flag_input, false);
	sim.process();
	REQUIRE(buffer[0] == 'E');

	// Step enough ticks to get the two E's at positions 6 and 8.
	for (int i = 0; i < 6; ++i) {
		sim.stepRequest();
		sim.process();
	}
	REQUIRE(buffer[6] == 'E');
	REQUIRE(buffer[8] == 'E');

	// Next step should move them forward.
	sim.stepRequest();
	sim.process();
	REQUIRE(buffer[6] == '.'); // Previous positions should be cleared
	REQUIRE(buffer[7] == 'E'); // First E should have moved to 7
	REQUIRE(buffer[8] == '*'); // Verify bang appears at expected location

	sim.stepRequest();
	sim.process();
	// The bang of the first E triggers the bang of the second E. This behavior
	// seems not to be consistent across different implementations of ORCA. For 
	// now, we are testing how it behaves in ORCA-C and ORCA (JS).
	REQUIRE(buffer[7] == '*');
	REQUIRE(buffer[8] == '.');
}


// End-to-end ORCA → MIDI output tests
// These load an ORCA pattern into the sim, step the VM through the module, and
// assert the resulting MIDI bytes on the mock device — the full VM → Oevent →
// module → midiOutPort path. A bang ('*') is consumed when it fires, so the
// patterns below are one-shot: the first tick emits, later ticks emit nothing.

TEST_CASE("End-to-end MIDI note from ORCA ':' operator", "[MIDI][Ahab]") {
	Test::ModuleScaffold<AhabModule> mods;
	AhabModule* m = mods.create("Ahab");
	Test::registerModule(m);
	MockMidiOutputDevice* mockDevice = setupMockMidiOutput(m);
	m->midiOutEnabled = true;
	m->simRunning = false; // disable BPM auto-step; drive ticks explicitly

	// ':' = MIDI note. Pattern args: channel '0', octave '4', note 'C' (=0),
	// velocity '2' (→ 2*8-1 = 15), length '1'. Banged by the '*' below it.
	Usz h, w;
	REQUIRE(m->sim->loadRectFromOrcaRequest(":04C21\n*.....", 0, 0, h, w, false) == true);
	m->process({}); // drain the paste

	// Tick 1: ':' fires → Note-On C4 + schedules a Note-Off in 1 tick.
	stepSim(m);
	REQUIRE(mockDevice->getMessageCount() == 1);
	const auto& on = mockDevice->getMessage(0);
	REQUIRE(on.getStatus() == 0x9);  // Note On
	REQUIRE(on.getChannel() == 0);
	REQUIRE(on.getNote() == 48);     // C4 = note 0 + octave 4*12
	REQUIRE(on.getValue() == 15);    // velocity = index_of('2')*8 - 1

	// Tick 2: the scheduled Note-Off fires; the consumed bang emits no new note.
	stepSim(m);
	REQUIRE(mockDevice->getMessageCount() == 2);
	const auto& off = mockDevice->getMessage(1);
	REQUIRE(off.getStatus() == 0x8);  // Note Off
	REQUIRE(off.getChannel() == 0);
	REQUIRE(off.getNote() == 48);
	REQUIRE(off.getValue() == 0);

	// Tick 3: nothing further.
	stepSim(m);
	REQUIRE(mockDevice->getMessageCount() == 2);

	cleanupMockMidiOutput(m, mockDevice);
	Test::unregisterModule(m);
}

TEST_CASE("End-to-end MIDI CC from ORCA '!' operator", "[MIDI][Ahab]") {
	Test::ModuleScaffold<AhabModule> mods;
	AhabModule* m = mods.create("Ahab");
	Test::registerModule(m);
	MockMidiOutputDevice* mockDevice = setupMockMidiOutput(m);
	m->midiOutEnabled = true;
	m->simRunning = false;

	// '!' = MIDI CC. Pattern args: channel '0', control '4', value '2'
	// (→ 2*127/35 = 7). Banged by the '*' below it.
	Usz h, w;
	REQUIRE(m->sim->loadRectFromOrcaRequest("!042\n*...", 0, 0, h, w, false) == true);
	m->process({});

	stepSim(m);
	REQUIRE(mockDevice->getMessageCount() == 1);
	const auto& cc = mockDevice->getMessage(0);
	REQUIRE(cc.getStatus() == 0xB);      // CC
	REQUIRE(cc.getChannel() == 0);
	REQUIRE(cc.bytes[1] == 68);          // midiCcOffset(64) + control(4)
	REQUIRE(cc.getValue() == 7);         // index_of('2')*127/35

	// One-shot: a second tick emits nothing more.
	stepSim(m);
	REQUIRE(mockDevice->getMessageCount() == 1);

	cleanupMockMidiOutput(m, mockDevice);
	Test::unregisterModule(m);
}

TEST_CASE("End-to-end MIDI pitchbend from ORCA '?' operator", "[MIDI][Ahab]") {
	Test::ModuleScaffold<AhabModule> mods;
	AhabModule* m = mods.create("Ahab");
	Test::registerModule(m);
	MockMidiOutputDevice* mockDevice = setupMockMidiOutput(m);
	m->midiOutEnabled = true;
	m->simRunning = false;

	// '?' = MIDI pitchbend. Pattern args: channel '0', msb '2' (→ 7), lsb '4' (→ 14).
	Usz h, w;
	REQUIRE(m->sim->loadRectFromOrcaRequest("?024\n*...", 0, 0, h, w, false) == true);
	m->process({});

	stepSim(m);
	REQUIRE(mockDevice->getMessageCount() == 1);
	const auto& pb = mockDevice->getMessage(0);
	REQUIRE(pb.getStatus() == 0xE);      // Pitchbend
	REQUIRE(pb.getChannel() == 0);
	REQUIRE(pb.bytes[1] == 14);          // lsb = index_of('4')*127/35
	REQUIRE(pb.bytes[2] == 7);           // msb = index_of('2')*127/35

	cleanupMockMidiOutput(m, mockDevice);
	Test::unregisterModule(m);
}

TEST_CASE("End-to-end MIDI note from monophonic ORCA '%' operator", "[MIDI][Ahab]") {
	Test::ModuleScaffold<AhabModule> mods;
	AhabModule* m = mods.create("Ahab");
	Test::registerModule(m);
	MockMidiOutputDevice* mockDevice = setupMockMidiOutput(m);
	m->midiOutEnabled = true;
	m->simRunning = false;

	// '%' is the monophonic MIDI note operator — same args as ':'.
	Usz h, w;
	REQUIRE(m->sim->loadRectFromOrcaRequest("%04C21\n*.....", 0, 0, h, w, false) == true);
	m->process({});

	stepSim(m);
	REQUIRE(mockDevice->getMessageCount() == 1);
	const auto& on = mockDevice->getMessage(0);
	REQUIRE(on.getStatus() == 0x9);  // Note On
	REQUIRE(on.getChannel() == 0);
	REQUIRE(on.getNote() == 48);     // C4
	REQUIRE(on.getValue() == 15);

	stepSim(m);
	REQUIRE(mockDevice->getMessageCount() == 2);
	const auto& off = mockDevice->getMessage(1);
	REQUIRE(off.getStatus() == 0x8);  // Note Off
	REQUIRE(off.getNote() == 48);
	REQUIRE(off.getValue() == 0);

	cleanupMockMidiOutput(m, mockDevice);
	Test::unregisterModule(m);
}


// End-to-end CV in/out tests through the '<' and '>' operators
// These drive the operators through a loaded field + module jacks (the full
// jack → readDspInput → '<' → field glyph, and field → '>' → writeDspOutput →
// jack path), complementing the direct custom_vcvin/custom_vcvout tests above.

TEST_CASE("End-to-end CV input via ORCA '<' operator", "[AhabSim][Ahab]") {
	Test::ModuleScaffold<AhabModule> mods;
	AhabModule* m = mods.create("Ahab");
	Test::registerModule(m);
	m->simRunning = false; // disable BPM auto-step; drive ticks explicitly

	// '<' port '1' maps to input jack 0. Full-scale 10V over range [0,35]
	// → value 35 → glyph 'z'. Banged by the '*' below it.
	m->inputs[AhabModule::IN_INPUT + 0].channels = 1;
	m->inputs[AhabModule::IN_INPUT + 0].setVoltage(10.0f);

	Usz h, w;
	REQUIRE(m->sim->loadRectFromOrcaRequest(".<.1..\n.*....", 0, 0, h, w, false) == true);
	m->process({});

	stepSim(m);

	Usz fh, fw;
	m->sim->getDisplayBuffer(fh, fw);
	Glyph const* buf = m->sim->getFieldBuffer();
	REQUIRE(buf[1 * fw + 1] == 'z'); // result written directly below '<'

	Test::unregisterModule(m);
}

TEST_CASE("End-to-end CV input via ORCA '<' scales mid-range voltage", "[AhabSim][Ahab]") {
	Test::ModuleScaffold<AhabModule> mods;
	AhabModule* m = mods.create("Ahab");
	Test::registerModule(m);
	m->simRunning = false;

	// 2.5V → round(2.5*3.5) = 9 over [0,35] → glyph '9'.
	m->inputs[AhabModule::IN_INPUT + 0].channels = 1;
	m->inputs[AhabModule::IN_INPUT + 0].setVoltage(2.5f);

	Usz h, w;
	REQUIRE(m->sim->loadRectFromOrcaRequest(".<.1..\n.*....", 0, 0, h, w, false) == true);
	m->process({});

	stepSim(m);

	Usz fh, fw;
	m->sim->getDisplayBuffer(fh, fw);
	Glyph const* buf = m->sim->getFieldBuffer();
	REQUIRE(buf[1 * fw + 1] == '9');

	Test::unregisterModule(m);
}

TEST_CASE("End-to-end CV output via ORCA '>' numeric port", "[AhabSim][Ahab]") {
	Test::ModuleScaffold<AhabModule> mods;
	AhabModule* m = mods.create("Ahab");
	Test::registerModule(m);
	m->simRunning = false;

	// '>' port '1' → output jack 0. value 6 in [0,12] → (6-0)/(12-0)*10 = 5V.
	Usz h, w;
	REQUIRE(m->sim->loadRectFromOrcaRequest(">106C\n*.....", 0, 0, h, w, false) == true);
	m->process({});

	stepSim(m);
	REQUIRE(m->outputs[AhabModule::OUT_OUTPUT + 0].getVoltage() == Catch::Approx(5.0f));

	// Numeric port has no gate: the voltage persists on later ticks.
	stepSim(m);
	stepSim(m);
	REQUIRE(m->outputs[AhabModule::OUT_OUTPUT + 0].getVoltage() == Catch::Approx(5.0f));

	Test::unregisterModule(m);
}

TEST_CASE("End-to-end CV gate via ORCA '>' letter port", "[AhabSim][Ahab]") {
	Test::ModuleScaffold<AhabModule> mods;
	AhabModule* m = mods.create("Ahab");
	Test::registerModule(m);
	m->simRunning = false;

	// '>' port 'a' → output jack 0. Note 'C' (0) at octave 3 → (0+36)/12 = 3V,
	// gate length 4 ticks.
	Usz h, w;
	REQUIRE(m->sim->loadRectFromOrcaRequest(">a3C4\n*.....", 0, 0, h, w, false) == true);
	m->process({});

	// Tick 1: gate fires → 3V.
	stepSim(m);
	REQUIRE(m->outputs[AhabModule::OUT_OUTPUT + 0].getVoltage() == Catch::Approx(3.0f));

	// Gate length 4: stays high for 3 more ticks, then drops to 0V on the 4th.
	for (int i = 0; i < 3; ++i) {
		stepSim(m);
		REQUIRE(m->outputs[AhabModule::OUT_OUTPUT + 0].getVoltage() == Catch::Approx(3.0f));
	}
	stepSim(m);
	REQUIRE(m->outputs[AhabModule::OUT_OUTPUT + 0].getVoltage() == 0.0f);

	Test::unregisterModule(m);
}



// End-to-end UDP / OSC output tests
// The ';' (UDP string) and '=' (OSC) operators emit events the sim exposes via
// getEvents() (checked deterministically below) and the module routes to its
// AhabOoscOutput. The end-to-end tests drive the operators through a loaded
// field + module and capture the sends with the injected recording fake — no
// sockets, fully deterministic. A bang ('*') is consumed when it fires, so the
// patterns below send exactly one datagram on the first tick.

TEST_CASE("ORCA ';' operator emits a udp_string event", "[AhabSim][UDP]") {
	AhabSim sim;

	// ';' = UDP string operator: sends the glyphs to its right.
	Usz h, w;
	REQUIRE(sim.loadRectFromOrcaRequest(";HELLO\n*.....", 0, 0, h, w, true) == true);
	sim.process();
	sim.stepRequest();
	sim.process();

	REQUIRE(sim.getEventCount() == 1);
	const Oevent* oe = &sim.getEvents()->buffer[0];
	REQUIRE(oe->any.oevent_type == Oevent_type_udp_string);
	REQUIRE(oe->udp_string.count == 5);
	REQUIRE(std::string(oe->udp_string.chars, oe->udp_string.count) == "HELLO");
}

TEST_CASE("ORCA '=' operator emits an osc_ints event", "[AhabSim][UDP]") {
	AhabSim sim;

	// '=' = OSC operator: path glyph 'f', length 2, values 'B'(11) 'C'(12).
	Usz h, w;
	REQUIRE(sim.loadRectFromOrcaRequest("=f2BC\n*....", 0, 0, h, w, true) == true);
	sim.process();
	sim.stepRequest();
	sim.process();

	REQUIRE(sim.getEventCount() == 1);
	const Oevent* oe = &sim.getEvents()->buffer[0];
	REQUIRE(oe->any.oevent_type == Oevent_type_osc_ints);
	REQUIRE(oe->osc_ints.glyph == 'f');
	REQUIRE(oe->osc_ints.count == 2);
	REQUIRE(oe->osc_ints.numbers[0] == 11);
	REQUIRE(oe->osc_ints.numbers[1] == 12);
}

// End-to-end through the module, captured by the injected recording fake: the
// full ORCA ';'/'=' operator → sim event → module → AhabOoscOutput path with no
// sockets (constructor injection — Test::createModule can't inject).
TEST_CASE("End-to-end UDP datagram from ORCA ';' operator", "[Ahab][UDP]") {
	// Two distinct fakes: the module takes ownership of each via unique_ptr, so
	// passing the same object twice would double-free on destruction.
	RecordingUdpOutput* fakeUdp = new RecordingUdpOutput();
	RecordingUdpOutput* fakeOsc = new RecordingUdpOutput();
	Test::ModuleScaffold<AhabModule> mods{[&]{ return createModuleWithOutputs(fakeUdp, fakeOsc); }};
	AhabModule* m = mods.create();
	Test::registerModule(m);
	m->simRunning = false; // disable BPM auto-step; drive ticks explicitly

	// ';' = UDP string operator: sends the glyphs to its right ("HELLO").
	Usz h, w;
	REQUIRE(m->sim->loadRectFromOrcaRequest(";HELLO\n*.....", 0, 0, h, w, false) == true);
	m->process({}); // drain the paste

	// One tick: the ';' operator sends its string once (the bang is consumed).
	stepSim(m);
	REQUIRE(fakeUdp->udpDatagrams.size() == 1);
	REQUIRE(std::string(fakeUdp->udpDatagrams[0].begin(), fakeUdp->udpDatagrams[0].end()) == "HELLO");

	// A second tick emits nothing more.
	stepSim(m);
	REQUIRE(fakeUdp->udpDatagrams.size() == 1);

	Test::unregisterModule(m);
}

TEST_CASE("End-to-end OSC message from ORCA '=' operator", "[Ahab][UDP]") {
	// Two distinct fakes: the module takes ownership of each via unique_ptr, so
	// passing the same object twice would double-free on destruction.
	RecordingUdpOutput* fakeUdp = new RecordingUdpOutput();
	RecordingUdpOutput* fakeOsc = new RecordingUdpOutput();
	Test::ModuleScaffold<AhabModule> mods{[&]{ return createModuleWithOutputs(fakeUdp, fakeOsc); }};
	AhabModule* m = mods.create();
	Test::registerModule(m);
	m->simRunning = false;

	// '=' = OSC operator: path glyph 'f', length 2, values 'B'(11) 'C'(12).
	Usz h, w;
	REQUIRE(m->sim->loadRectFromOrcaRequest("=f2BC\n*....", 0, 0, h, w, false) == true);
	m->process({});

	// One tick: the '=' operator sends "/f" with ints 11, 12 (index_of of
	// 'B'/'C'). The module builds the OSC address as {'/', glyph, '\0'} → "/f",
	// which oosc_send_int32s pads to the 4-byte OSC address "/f\0\0".
	stepSim(m);
	REQUIRE(fakeOsc->oscInts.size() == 1);
	REQUIRE(fakeOsc->oscInts[0].first == std::string("/f"));
	REQUIRE(fakeOsc->oscInts[0].second.size() == 2);
	REQUIRE(fakeOsc->oscInts[0].second[0] == 11);
	REQUIRE(fakeOsc->oscInts[0].second[1] == 12);

	Test::unregisterModule(m);
}

TEST_CASE("UDP/OSC output is safe with no destination configured", "[Ahab][UDP]") {
	// No destination: the module's real output falls back to 127.0.0.1:49161.
	// With nothing listening there the datagram is dropped without crashing.
	Test::ModuleScaffold<AhabModule> mods;
	AhabModule* m = mods.create("Ahab"); // real output
	Test::registerModule(m);
	m->simRunning = false;

	Usz h, w;
	REQUIRE(m->sim->loadRectFromOrcaRequest(";HELLO\n*.....", 0, 0, h, w, false) == true);
	m->process({});
	REQUIRE_NOTHROW([&]{ stepSim(m); }());

	Test::unregisterModule(m);
}


// AhabOoscOutput destination configuration and validation (direct, no sockets).
TEST_CASE("UDP destination configuration", "[AhabUdp]") {
	AhabOoscOutput out(AhabOoscOutput::Kind::UDP);
	
	out.setDestination("192.168.1.1", "8000");
	REQUIRE(out.getAddress() == "192.168.1.1");
	REQUIRE(out.getPort() == "8000");
	
	// Test with whitespace (should be trimmed)
	out.setDestination("  10.0.0.1  ", "  9000  ");
	REQUIRE(out.getAddress() == "10.0.0.1");
	REQUIRE(out.getPort() == "9000");
}

TEST_CASE("OSC destination configuration", "[AhabUdp]") {
	AhabOoscOutput out(AhabOoscOutput::Kind::OSC);
	
	out.setDestination("localhost", "9001");
	REQUIRE(out.getAddress() == "localhost");
	REQUIRE(out.getPort() == "9001");
	
	// Test with whitespace (should be trimmed)
	out.setDestination("  127.0.0.1  ", "  9002  ");
	REQUIRE(out.getAddress() == "127.0.0.1");
	REQUIRE(out.getPort() == "9002");
}

TEST_CASE("Invalid port numbers rejected", "[AhabUdp]") {
	AhabOoscOutput out(AhabOoscOutput::Kind::UDP);
	
	out.setDestination("127.0.0.1", "8000");
	REQUIRE(out.getPort() == "8000");
	
	// Try to set invalid port (should be ignored)
	out.setDestination("127.0.0.1", "invalid");
	REQUIRE(out.getPort() == "8000"); // Should remain unchanged
	
	// Try to set out-of-range port
	out.setDestination("127.0.0.1", "99999");
	REQUIRE(out.getPort() == "8000"); // Should remain unchanged
}
