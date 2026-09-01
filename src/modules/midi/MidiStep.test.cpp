#include "../../test/framework.hpp"
#include "MidiStep.cpp"

using namespace StoermelderPackOne;
using namespace StoermelderPackOne::MidiStep;

SYNC_MODEL(modelMidiStep, "MidiStep");
static Test::TestContext<> testContext;

// Convenience: build a CC message on channel 0.
static rack::midi::Message cc(uint8_t ccNum, uint8_t value) {
	return Test::makeMidiMessage(0xb, 0, ccNum, value);
}

// Drive process() until OUTPUT_INC/OUTPUT_DEC voltage on the given output/channel
// reads high, or give up. Returns true if a high (10V) sample was observed.
static bool pollHigh(MidiStepModule* module, int out, int channel = 0, int frames = 4096) {
	bool high = false;
	for (int i = 0; i < frames; i++) {
		module->process(Test::makeProcessArgs(i + 1));
		if (module->outputs[out].getVoltage(channel) > 5.f) high = true;
	}
	return high;
}


TEST_CASE("Construction and reset", "[MidiStep]") {
	auto module = Test::createModule<MidiStepModule>("MidiStep");

	SECTION("Default mapping after reset") {
		REQUIRE(module->learningId == -1);
		REQUIRE(module->mode == MODE::BEATSTEP_R1);
		REQUIRE(module->polyphonicOutput == false);
		for (int i = 0; i < MidiStepModule::CHANNELS; i++) {
			REQUIRE(module->learnedCcs[i] == i);
			REQUIRE(module->ccs[i] == i);
			REQUIRE(module->incPulseCount[i] == 0);
			REQUIRE(module->decPulseCount[i] == 0);
		}
		// CC numbers above CHANNELS are unmapped.
		for (int i = MidiStepModule::CHANNELS; i < 128; i++) {
			REQUIRE(module->ccs[i] == -1);
		}
	}

	SECTION("Explicit onReset clears learned state") {
		module->learningId = 4;
		module->incPulseCount[2] = 10;
		Module::ResetEvent re;
		module->onReset(re);
		REQUIRE(module->learningId == -1);
		REQUIRE(module->incPulseCount[2] == 0);
		REQUIRE(module->learnedCcs[2] == 2);
	}

	Test::destroyModule(module);
}

TEST_CASE("Preset JSON null-guards", "[MidiStep][JSON]") {
	auto module = Test::createModule<MidiStepModule>("MidiStep");

	SECTION("All top-level properties are null-guarded in dataFromJson()") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetNullGuards(module, rootJ);
		json_decref(rootJ);
	}

	SECTION("All properties tolerate wrong-typed values") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetTypeConfusion(module, rootJ);
		json_decref(rootJ);
	}

	SECTION("All arrays tolerate being oversized") {
		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);
		Test::testPresetOversizedArrays(module, rootJ);
		json_decref(rootJ);
	}

	Test::destroyModule(module);
}

TEST_CASE("JSON round-trip preserves state", "[MidiStep][JSON]") {
	SECTION("Scalars and remapped channels") {
		auto module = Test::createModule<MidiStepModule>("MidiStep");
		module->mode = MODE::AKAI_MPD218;
		module->polyphonicOutput = true;
		module->panelTheme = 1;
		// Remap a couple of channels.
		module->learningId = 0;
		module->learnCC(100);
		module->learningId = 3;
		module->learnCC(64);

		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);

		auto restored = Test::createModule<MidiStepModule>("MidiStep");
		restored->dataFromJson(rootJ);

		REQUIRE(restored->mode == MODE::AKAI_MPD218);
		REQUIRE(restored->panelTheme == 1);
		REQUIRE(restored->polyphonicOutput == true);
		REQUIRE(restored->learnedCcs[0] == 100);
		REQUIRE(restored->ccs[100] == 0);
		REQUIRE(restored->learnedCcs[3] == 64);
		REQUIRE(restored->ccs[64] == 3);

		json_decref(rootJ);
		Test::destroyModule(module);
		Test::destroyModule(restored);
	}

	SECTION("Entire ccs array including unmapped (-1) channels") {
		auto module = Test::createModule<MidiStepModule>("MidiStep");
		// Remap every channel to a distinct high CC number...
		for (int i = 0; i < MidiStepModule::CHANNELS; i++) {
			module->learningId = i;
			module->learnCC(100 + i);
		}
		// ...then steal channel 1's CC onto channel 2, leaving channel 1 unmapped.
		module->learningId = 2;
		module->learnCC(module->learnedCcs[1]);

		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);

		auto restored = Test::createModule<MidiStepModule>("MidiStep");
		restored->dataFromJson(rootJ);

		// Every slot survives, including the unmapped entry.
		for (int i = 0; i < MidiStepModule::CHANNELS; i++) {
			CATCH_INFO("Channel " << i);
			REQUIRE(restored->learnedCcs[i] == module->learnedCcs[i]);
		}
		// The reverse mapping matches exactly, unmapped CCs included.
		for (int ccNum = 0; ccNum < 128; ccNum++) {
			REQUIRE(restored->ccs[ccNum] == module->ccs[ccNum]);
		}

		// The restored map routes pulses identically to the original.
		module->mode = restored->mode = MODE::BEATSTEP_R1;
		module->processMessage(cc(105, 70));
		restored->processMessage(cc(105, 70));
		for (int i = 0; i < MidiStepModule::CHANNELS; i++) {
			REQUIRE(restored->incPulseCount[i] == module->incPulseCount[i]);
		}
		REQUIRE(restored->incPulseCount[5] == 6);

		json_decref(rootJ);
		Test::destroyModule(module);
		Test::destroyModule(restored);
	}

	SECTION("Nested midi input object") {
		auto module = Test::createModule<MidiStepModule>("MidiStep");
		module->midiInput.channel = 7;

		json_t* rootJ = module->dataToJson();
		REQUIRE(rootJ != nullptr);

		auto restored = Test::createModule<MidiStepModule>("MidiStep");
		restored->dataFromJson(rootJ);

		REQUIRE(restored->midiInput.channel == 7);

		json_decref(rootJ);
		Test::destroyModule(module);
		Test::destroyModule(restored);
	}
}


TEST_CASE("dataFromJson drops out-of-range CC numbers in ccs array", "[MidiStep][JSON]") {
	// ccs entries index the 128-element ccs[] vector, so values from a corrupt
	// preset must be range-checked before use (REQUIRE_NOTHROW alone would not
	// catch an out-of-bounds write: it is UB, not an exception).
	auto module = Test::createModule<MidiStepModule>("MidiStep");

	json_t* rootJ = json_object();
	json_t* ccsJ = json_array();
	for (int i = 0; i < MidiStepModule::CHANNELS; i++) {
		// Entries 0/1 are out-of-range; the rest keep their default mapping.
		json_array_append_new(ccsJ, json_integer(i == 0 ? 9999 : (i == 1 ? -5 : i)));
	}
	json_object_set_new(rootJ, "ccs", ccsJ);

	module->dataFromJson(rootJ);

	// Out-of-range entries are treated as unmapped.
	REQUIRE(module->learnedCcs[0] == -1);
	REQUIRE(module->learnedCcs[1] == -1);
	// Valid entries still route: CC 2 drives channel 2.
	REQUIRE(module->ccs[2] == 2);
	module->mode = MODE::BEATSTEP_R1;
	module->processMessage(cc(2, 70));
	REQUIRE(module->incPulseCount[2] == 6);

	json_decref(rootJ);
	Test::destroyModule(module);
}


TEST_CASE("Relative mode #1 (Beatstep R1 / X-Touch R2)", "[MidiStep]") {
	auto module = Test::createModule<MidiStepModule>("MidiStep");

	auto modeVal = GENERATE(MODE::BEATSTEP_R1, MODE::XTOUCH_R2);
	module->mode = modeVal;

	SECTION("Center value 64 is neutral") {
		module->processMessage(cc(0, 64));
		REQUIRE(module->incPulseCount[0] == 0);
		REQUIRE(module->decPulseCount[0] == 0);
	}

	SECTION("Increment thresholds (65/67/70)") {
		module->processMessage(cc(0, 65));
		REQUIRE(module->incPulseCount[0] == 2);
		module->processMessage(cc(0, 67));
		REQUIRE(module->incPulseCount[0] == 2 + 4);
		module->processMessage(cc(0, 70));
		REQUIRE(module->incPulseCount[0] == 2 + 4 + 6);
		REQUIRE(module->decPulseCount[0] == 0);
	}

	SECTION("Decrement thresholds (63/61/58)") {
		module->processMessage(cc(0, 63));
		REQUIRE(module->decPulseCount[0] == 2);
		module->processMessage(cc(0, 61));
		REQUIRE(module->decPulseCount[0] == 2 + 4);
		module->processMessage(cc(0, 58));
		REQUIRE(module->decPulseCount[0] == 2 + 4 + 6);
		REQUIRE(module->incPulseCount[0] == 0);
	}

	Test::destroyModule(module);
}


TEST_CASE("Relative mode #2 (fixed 1..3 / 125..127)", "[MidiStep]") {
	auto module = Test::createModule<MidiStepModule>("MidiStep");

	auto modeVal = GENERATE(MODE::BEATSTEP_R2, MODE::KK_REL, MODE::AKAI_MPD218,
		MODE::HERCULES_DJCONTROL_STARLIGHT, MODE::XTOUCH_R1);
	module->mode = modeVal;

	SECTION("Increment values 1/2/3") {
		module->processMessage(cc(0, 1));
		REQUIRE(module->incPulseCount[0] == 2);
		module->processMessage(cc(0, 2));
		REQUIRE(module->incPulseCount[0] == 2 + 4);
		module->processMessage(cc(0, 3));
		REQUIRE(module->incPulseCount[0] == 2 + 4 + 6);
		REQUIRE(module->decPulseCount[0] == 0);
	}

	SECTION("Decrement values 127/126/125") {
		module->processMessage(cc(0, 127));
		REQUIRE(module->decPulseCount[0] == 2);
		module->processMessage(cc(0, 126));
		REQUIRE(module->decPulseCount[0] == 2 + 4);
		module->processMessage(cc(0, 125));
		REQUIRE(module->decPulseCount[0] == 2 + 4 + 6);
		REQUIRE(module->incPulseCount[0] == 0);
	}

	SECTION("Center value is neutral") {
		module->processMessage(cc(0, 64));
		REQUIRE(module->incPulseCount[0] == 0);
		REQUIRE(module->decPulseCount[0] == 0);
	}

	Test::destroyModule(module);
}


TEST_CASE("Ignores non-CC messages", "[MidiStep]") {
	auto module = Test::createModule<MidiStepModule>("MidiStep");
	// Note-on, status 0x9 — should be ignored entirely.
	module->processMessage(Test::makeMidiMessage(0x9, 0, 60, 100));
	for (int i = 0; i < MidiStepModule::CHANNELS; i++) {
		REQUIRE(module->incPulseCount[i] == 0);
		REQUIRE(module->decPulseCount[i] == 0);
	}
	Test::destroyModule(module);
}


TEST_CASE("Ignores unmapped CC numbers", "[MidiStep]") {
	auto module = Test::createModule<MidiStepModule>("MidiStep");
	module->mode = MODE::BEATSTEP_R1;
	// Only CCs 0..15 are mapped by default; CC 64 has no channel.
	module->processMessage(cc(64, 70));
	for (int i = 0; i < MidiStepModule::CHANNELS; i++) {
		REQUIRE(module->incPulseCount[i] == 0);
		REQUIRE(module->decPulseCount[i] == 0);
	}
	Test::destroyModule(module);
}


TEST_CASE("CC learning", "[MidiStep]") {
	auto module = Test::createModule<MidiStepModule>("MidiStep");

	SECTION("learnCC remaps a channel to a new CC") {
		module->learningId = 0;
		module->learnCC(20);
		REQUIRE(module->learnedCcs[0] == 20);
		REQUIRE(module->ccs[20] == 0);
		// Old mapping for CC 0 is cleared.
		REQUIRE(module->ccs[0] == -1);
		REQUIRE(module->learningId == -1);

		// CC 20 now drives channel 0.
		module->mode = MODE::BEATSTEP_R1;
		module->processMessage(cc(20, 70));
		REQUIRE(module->incPulseCount[0] == 6);
	}

	SECTION("Learning via incoming CC message") {
		module->learningId = 5;
		// While learning, the value must not generate pulses.
		module->processMessage(cc(42, 70));
		REQUIRE(module->learnedCcs[5] == 42);
		REQUIRE(module->ccs[42] == 5);
		REQUIRE(module->learningId == -1);
		REQUIRE(module->incPulseCount[5] == 0);
	}

	SECTION("Stealing a CC from another channel clears the old channel") {
		// Map CC 7 to channel 1.
		module->learningId = 1;
		module->learnCC(7);
		REQUIRE(module->ccs[7] == 1);
		REQUIRE(module->learnedCcs[1] == 7);

		// Now learn CC 7 on channel 2 — channel 1 must lose it.
		module->learningId = 2;
		module->learnCC(7);
		REQUIRE(module->ccs[7] == 2);
		REQUIRE(module->learnedCcs[2] == 7);
		REQUIRE(module->learnedCcs[1] == -1);
	}

	SECTION("learnCC is a no-op when not learning") {
		module->learningId = -1;
		module->learnCC(30);
		REQUIRE(module->ccs[30] == -1);
	}

	Test::destroyModule(module);
}


TEST_CASE("Produces increment/decrement triggers", "[MidiStep]") {
	auto module = Test::createModule<MidiStepModule>("MidiStep");
	module->mode = MODE::BEATSTEP_R1;

	SECTION("Increment generates a high pulse on OUTPUT_INC") {
		module->processMessage(cc(0, 70)); // inc += 6
		REQUIRE(module->incPulseCount[0] == 6);
		REQUIRE(pollHigh(module, MidiStepModule::OUTPUT_INC + 0));
		// All queued pulses are consumed.
		REQUIRE(module->incPulseCount[0] == 0);
	}

	SECTION("Decrement generates a high pulse on OUTPUT_DEC") {
		module->processMessage(cc(0, 58)); // dec += 6
		REQUIRE(module->decPulseCount[0] == 6);
		REQUIRE(pollHigh(module, MidiStepModule::OUTPUT_DEC + 0));
		REQUIRE(module->decPulseCount[0] == 0);
	}

	Test::destroyModule(module);
}


TEST_CASE("Output routing", "[MidiStep]") {
	auto module = Test::createModule<MidiStepModule>("MidiStep");
	module->mode = MODE::BEATSTEP_R1;

	SECTION("Monophonic: channel N drives its own port") {
		module->polyphonicOutput = false;
		// CC 3 maps to channel 3 -> OUTPUT_INC + 3 (separate port).
		module->processMessage(cc(3, 70));
		REQUIRE(pollHigh(module, MidiStepModule::OUTPUT_INC + 3, 0));
		// Port 0 stays low.
		REQUIRE(module->outputs[MidiStepModule::OUTPUT_INC + 0].getVoltage(0) == 0.f);
	}

	SECTION("Polyphonic: channel N drives port 0 polyphonic channel N") {
		module->polyphonicOutput = true;
		// CC 3 maps to channel 3 -> OUTPUT_INC port 0, poly channel 3.
		module->processMessage(cc(3, 70));
		REQUIRE(pollHigh(module, MidiStepModule::OUTPUT_INC + 0, 3));
	}

	Test::destroyModule(module);
}


TEST_CASE("MIDI queue is processed", "[MidiStep]") {
	auto module = Test::createModule<MidiStepModule>("MidiStep");
	module->mode = MODE::BEATSTEP_R1;
	// Push a CC into the input queue and let process() pop it.
	module->midiInput.onMessage(cc(0, 70));
	module->process(Test::makeProcessArgs(1));
	REQUIRE(module->incPulseCount[0] > 0);
	Test::destroyModule(module);
}


TEST_CASE("processBypass drains the MIDI queue without producing triggers", "[MidiStep]") {
	auto module = Test::createModule<MidiStepModule>("MidiStep");
	module->mode = MODE::BEATSTEP_R1;

	// Push a CC that would normally produce an increment pulse.
	module->midiInput.onMessage(cc(0, 70));

	module->processBypass(Test::makeProcessArgs(1));

	REQUIRE(module->incPulseCount[0] == 0);
	REQUIRE(module->outputs[MidiStepModule::OUTPUT_INC].getVoltage() == 0.f);

	// The queue was drained by processBypass, so a following process() sees nothing.
	module->process(Test::makeProcessArgs(2));
	REQUIRE(module->incPulseCount[0] == 0);

	Test::destroyModule(module);
}