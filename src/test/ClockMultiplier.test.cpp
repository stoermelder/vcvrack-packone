#include "catch2/plugin.hpp"
#include "../utils/digital.hpp"
using namespace StoermelderPackOne;

TEST_CASE("ClockMultiplier tests")
{
	SECTION("Basic functionality")
	{		
		ClockMultiplier cm;
		// Prepare a clock cycle of 100 samples
		cm.tick();
		for (int i = 0; i < 100; i++) {
			cm.process();				
		}
		cm.tick();
		cm.trigger(4);
		for (int i = 0; i < 100; i++) {
			bool triggered = cm.process();
			CATCH_INFO("i = " << i);
			if (i % 25 == 1)
				REQUIRE(triggered);
			else
				REQUIRE_FALSE(triggered);	
		}
		cm.tick();
		for (int i = 0; i < 100; i++) {
			bool triggered = cm.process();
			CATCH_INFO("i = " << i);
			REQUIRE_FALSE(triggered);
		}
		cm.tick();
	}
}