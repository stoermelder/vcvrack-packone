#include "../../test/framework.hpp"
#include "FlowerSeq.cpp"
#include "FlowerSeqEx.cpp"
#include "FlowerSeqTrig.cpp"

using namespace StoermelderPackOne::Flower;

Test::TestContext<> testContext;

typedef FlowerSeqModule<16, 8, 8> MasterModule;
typedef FlowerSeqExModule<16, 8, 8> OffspringModule;
typedef FlowerTrigModule<16, 8, 8> SeedsModule;


// Asserts the invariant every PatternList mutator must preserve: slot[] is a permutation of
// every PATTERN_TYPE, and map[] is its exact inverse. This is the single property that catches
// an index slip in enable()/disable()/moveFwd()/moveBwd() — call it after every mutation in
// every case below.
static void checkPermutationInvariant(PatternList& list) {
	bool seen[PatternList::SIZE] = {};
	for (int i = 0; i < PatternList::SIZE; i++) {
		PATTERN_TYPE t = list.at(i);
		CATCH_INFO("slot[" << i << "] == " << (int)t);
		REQUIRE((int)t >= 0);
		REQUIRE((int)t < PatternList::SIZE);
		REQUIRE_FALSE(seen[(int)t]);
		seen[(int)t] = true;
		CHECK(list.map[(int)t] == i);
	}
}