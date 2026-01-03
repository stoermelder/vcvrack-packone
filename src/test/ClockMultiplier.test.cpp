#include "catch2/plugin.hpp"
#include "../utils/digital.hpp"

using namespace StoermelderPackOne;

TEST_CASE("Division and triggering", "[ClockMultiplier]") {
	ClockMultiplier cm;
	const int clockCycleSamples = 20;
	// Prepare a clock cycle of 20 samples
	cm.tick();
	for (int i = 0; i < clockCycleSamples; i++) {
		cm.process();				
	}
	cm.tick();
	cm.trigger(4);
	std::vector<int> triggers;
	for (int i = 0; i < clockCycleSamples; i++) {
		if (cm.process()) triggers.push_back(i);
	}
	std::string s(triggers.begin(), triggers.end());
	CATCH_INFO("triggers: " << s);
	// Expect triggers at samples 1, 6, 11, 16 (roughly)
	REQUIRE(triggers.size() == 4);
	REQUIRE(triggers[0] == 1);
	REQUIRE(triggers[1] == 6);
	REQUIRE(triggers[2] == 11);
	REQUIRE(triggers[3] == 16);

	cm.tick();
	triggers.clear();
	for (int i = 0; i < clockCycleSamples; i++) {
		if (cm.process()) triggers.push_back(i);
	}
	REQUIRE(triggers.empty());
	cm.tick();
}


TEST_CASE("Non-integer division spacing (div=3)", "[ClockMultiplier]") {
	ClockMultiplier cm;
	const int clockCycleSamples = 20;
	cm.tick();
	for (int i = 0; i < clockCycleSamples; i++) cm.process();
	cm.tick();
	// Trigger 3 subdivisions (20/3 ~= 6.667), expect 3 triggers in the cycle
	cm.trigger(3);
	std::vector<int> triggers;
	for (int i = 0; i < clockCycleSamples; i++) {
		if (cm.process()) triggers.push_back(i);
	}

	std::string s(triggers.begin(), triggers.end());
	CATCH_INFO("triggers: " << s);
	// Expect roughly 3 triggers (indices should be increasing)
	REQUIRE(triggers.size() == 3);
	// Check approximate expected indices (1, 7, 14)
	REQUIRE(triggers[0] == 1);
	REQUIRE(triggers[1] == 7);
	REQUIRE(triggers[2] == 14);
}

TEST_CASE("Dense triggering when division fraction is small (div=32)", "[ClockMultiplier]") {
	ClockMultiplier cm;
	const int clockCycleSamples = 20;
	cm.tick();
	for (int i = 0; i < clockCycleSamples; i++) cm.process();
	cm.tick();
	// Very small division value => many triggers (should trigger almost every sample after the first)
	cm.trigger(32);
	int count = 0;
	for (int i = 0; i < clockCycleSamples; i++) {
		if (cm.process()) count++;
	}
	// Expect triggers on samples 1..19 => 19 triggers
	REQUIRE(count == (clockCycleSamples - 1));
}

TEST_CASE("Reset and zero-division edge cases", "[ClockMultiplier]") {
	ClockMultiplier cm;
	const int clockCycleSamples = 20;
	// Reset clears clock, so trigger should be ignored
	cm.reset();
	cm.tick();
	cm.trigger(4); // should do nothing because clock==0
	std::vector<int> triggers;
	for (int i = 0; i < clockCycleSamples; i++) {
		if (cm.process()) triggers.push_back(i);
	}
	REQUIRE(triggers.empty());

	// Trigger with div=0 should be ignored
	cm.tick();
	for (int i = 0; i < clockCycleSamples; i++) cm.process();
	cm.tick();
	cm.trigger(0);
	triggers.clear();
	for (int i = 0; i < clockCycleSamples; i++) {
		if (cm.process()) triggers.push_back(i);
	}
	REQUIRE(triggers.empty());
}