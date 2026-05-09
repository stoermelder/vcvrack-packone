#include "../test/test_plugin.hpp"
#include "digital.hpp"

using namespace StoermelderPackOne;

TEST_CASE("Basic division and wrapping", "[ClockDividerEx]") {
	ClockDividerEx cd;
	cd.setDivision(4);
	// Force deterministic start
	cd.clock = 0;

	// Collect trigger indices across a cycle of 12 samples
	std::vector<int> triggers;
	for (int i = 0; i < 12; i++) {
		if (cd.process()) triggers.push_back(i);
	}

	CATCH_INFO("collected triggers: " << triggers.size());
	REQUIRE(triggers.size() == 3);
	REQUIRE(triggers[0] == 3);
	REQUIRE(triggers[1] == 7);
	REQUIRE(triggers[2] == 11);
}

TEST_CASE("Reset produces clock in [0, division-1]", "[ClockDividerEx]") {
	ClockDividerEx cd;
	cd.setDivision(5);
	int failures = 0;
	for (int i = 0; i < 50; i++) {
		cd.reset();
		if (cd.getClock() >= cd.getDivision()) failures++;
	}

	CATCH_INFO("failures=" << failures);
	REQUIRE(failures == 0);
}

TEST_CASE("Division == 1 always triggers", "[ClockDividerEx]") {
	ClockDividerEx cd;
	cd.setDivision(1);
	// Collect results for a few iterations
	std::vector<bool> results;
	for (int i = 0; i < 5; i++) {
		results.push_back(cd.process());
	}

	CATCH_INFO("results size=" << results.size());
	auto triggers = std::all_of(results.begin(), results.end(), [](bool b){ return b; });
	REQUIRE(triggers == true);
}

TEST_CASE("setDivision updates division and resets clock", "[ClockDividerEx]") {
	ClockDividerEx cd;
	cd.setDivision(7);
	REQUIRE(cd.getDivision() == 7);
	// After setDivision the clock must be valid
	REQUIRE(cd.getClock() < cd.getDivision());
}