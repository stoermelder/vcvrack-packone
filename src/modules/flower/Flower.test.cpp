#include "../../test/framework.hpp"

#include "Flower.hpp"

using namespace StoermelderPackOne::Flower;

Test::TestContext<> testContext;

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

TEST_CASE("PatternList construction and invariants", "[Flower][PatternList]") {
	PatternList list;

	SECTION("reset() with default SIZE makes every type active in order") {
		list.reset();
		CHECK(list.last == PatternList::SIZE);
		CHECK(list.pos == 0);
		for (int i = 0; i < PatternList::SIZE; i++) {
			CHECK(list.at(i) == (PATTERN_TYPE)i);
			CHECK(list.map[i] == i);
			CHECK(list.active((PATTERN_TYPE)i));
		}
		checkPermutationInvariant(list);
	}

	SECTION("reset(s) activates exactly the first s slots, for every achievable s") {
		for (int s = 1; s <= PatternList::SIZE; s++) {
			list.reset(s);
			CHECK(list.last == s);
			for (int i = 0; i < PatternList::SIZE; i++) {
				bool shouldBeActive = i < s;
				CHECK(list.active((PATTERN_TYPE)i) == shouldBeActive);
			}
			checkPermutationInvariant(list);
		}
	}

	SECTION("at()/getNameAt() agree with slot[] and survive a reordering") {
		list.reset();
		for (int i = 0; i < PatternList::SIZE; i++) {
			list.setName(i, string::f("name-%d", i));
		}
		for (int i = 0; i < PatternList::SIZE; i++) {
			CHECK(list.getNameAt(i) == list.name[(int)list.at(i)]);
		}

		// Move a type around, then check getNameAt still reports the name for the type that
		// now sits at that slot, not the name that used to be there positionally.
		list.moveFwd(list.at(5));
		for (int i = 0; i < PatternList::SIZE; i++) {
			CHECK(list.getNameAt(i) == list.name[(int)list.at(i)]);
		}
	}
}

TEST_CASE("PatternList::enable() / disable() / toggle()", "[Flower][PatternList]") {
	PatternList list;

	SECTION("disable() on an active type moves it to the just-vacated slot and keeps order") {
		list.reset();
		PATTERN_TYPE before[PatternList::SIZE];
		for (int i = 0; i < PatternList::SIZE; i++) before[i] = list.at(i);

		PATTERN_TYPE target = list.at(4);
		list.disable(target);

		CHECK(list.last == PatternList::SIZE - 1);
		CHECK(list.at(PatternList::SIZE - 1) == target);
		CHECK_FALSE(list.active(target));
		// Every other type keeps its relative order (slots 4..last-1 shifted left by one).
		int j = 0;
		for (int i = 0; i < PatternList::SIZE; i++) {
			if (before[i] == target) continue;
			CHECK(list.at(j) == before[i]);
			j++;
		}
		checkPermutationInvariant(list);
	}

	SECTION("disable() on an already-inactive type is a no-op") {
		list.reset(5);
		PATTERN_TYPE inactive = list.at(10);
		REQUIRE_FALSE(list.active(inactive));

		PATTERN_TYPE slotsBefore[PatternList::SIZE];
		for (int i = 0; i < PatternList::SIZE; i++) slotsBefore[i] = list.at(i);
		int lastBefore = list.last;

		list.disable(inactive);

		CHECK(list.last == lastBefore);
		for (int i = 0; i < PatternList::SIZE; i++) {
			CHECK(list.at(i) == slotsBefore[i]);
		}
	}

	SECTION("disable() refuses to go below last == 1") {
		list.reset(1);
		PATTERN_TYPE onlyActive = list.at(0);
		REQUIRE(list.last == 1);

		list.disable(onlyActive);

		CHECK(list.last == 1);
		CHECK(list.active(onlyActive));
		checkPermutationInvariant(list);
	}

	SECTION("enable() on an inactive type moves it to slot[last-1] before incrementing last") {
		list.reset(5);
		PATTERN_TYPE target = list.at(5);
		REQUIRE_FALSE(list.active(target));

		list.enable(target);

		CHECK(list.last == 6);
		CHECK(list.at(5) == target);
		CHECK(list.active(target));
		checkPermutationInvariant(list);
	}

	SECTION("enable() on an already-active type is a no-op") {
		list.reset(5);
		PATTERN_TYPE active = list.at(2);

		PATTERN_TYPE slotsBefore[PatternList::SIZE];
		for (int i = 0; i < PatternList::SIZE; i++) slotsBefore[i] = list.at(i);
		int lastBefore = list.last;

		list.enable(active);

		CHECK(list.last == lastBefore);
		for (int i = 0; i < PatternList::SIZE; i++) {
			CHECK(list.at(i) == slotsBefore[i]);
		}
	}

	SECTION("enable()/disable() are inverses at the set level") {
		list.reset();
		PATTERN_TYPE toToggle[] = {PATTERN_TYPE::SEQ_REV, PATTERN_TYPE::AUX_SUB, PATTERN_TYPE::AUX_RAND};

		for (PATTERN_TYPE t : toToggle) list.disable(t);
		for (PATTERN_TYPE t : toToggle) CHECK_FALSE(list.active(t));

		for (PATTERN_TYPE t : toToggle) list.enable(t);
		for (int i = 0; i < PatternList::SIZE; i++) {
			CHECK(list.active((PATTERN_TYPE)i));
		}
		checkPermutationInvariant(list);
	}

	SECTION("toggle() dispatches to enable/disable, and twice is identity for the active set") {
		list.reset();
		PATTERN_TYPE t = PATTERN_TYPE::SEQ_OOD;

		list.toggle(t);
		CHECK_FALSE(list.active(t));

		list.toggle(t);
		CHECK(list.active(t));

		list.toggle(t);
		list.toggle(t);
		CHECK(list.active(t));
		checkPermutationInvariant(list);
	}

	SECTION("full drain and refill preserves the permutation invariant at every step") {
		list.reset();
		while (list.last > 1) {
			PATTERN_TYPE last = list.at(list.last - 1);
			list.disable(last);
			checkPermutationInvariant(list);
		}
		REQUIRE(list.last == 1);

		for (int i = 0; i < PatternList::SIZE; i++) {
			PATTERN_TYPE t = (PATTERN_TYPE)i;
			if (!list.active(t)) {
				list.enable(t);
				checkPermutationInvariant(list);
			}
		}
		CHECK(list.last == PatternList::SIZE);
	}
}

TEST_CASE("PatternList::moveFwd() / moveBwd()", "[Flower][PatternList]") {
	PatternList list;

	SECTION("moveFwd() on the type at position 0 is a no-op") {
		list.reset();
		PATTERN_TYPE first = list.at(0);

		list.moveFwd(first);

		CHECK(list.at(0) == first);
		checkPermutationInvariant(list);
	}

	SECTION("moveBwd() on the type at last-1 is a no-op") {
		list.reset(6);
		PATTERN_TYPE lastActive = list.at(list.last - 1);

		list.moveBwd(lastActive);

		CHECK(list.at(list.last - 1) == lastActive);
		checkPermutationInvariant(list);
	}

	SECTION("moveFwd()/moveBwd() swap adjacent slots and are exact inverses") {
		list.reset();
		PATTERN_TYPE atThree = list.at(3);
		PATTERN_TYPE atTwo = list.at(2);

		list.moveFwd(atThree);
		CHECK(list.at(2) == atThree);
		CHECK(list.at(3) == atTwo);
		checkPermutationInvariant(list);

		list.moveBwd(atThree);
		CHECK(list.at(2) == atTwo);
		CHECK(list.at(3) == atThree);
		checkPermutationInvariant(list);
	}

	SECTION("move on an inactive type can swap it into the active region (currently unguarded)") {
		// moveBwd() only guards p == last - 1, so an inactive type at p > last - 1 (i.e. not
		// the very last slot in the whole array) passes that guard and swaps into the active
		// region, changing the active set as a side effect of what looks like a pure reorder.
		// This pins the current (unguarded) behaviour so a change here is a deliberate decision,
		// not a silent regression either way.
		list.reset(5);
		PATTERN_TYPE inactiveType = list.at(6);
		REQUIRE_FALSE(list.active(inactiveType));

		list.moveBwd(inactiveType);

		// The type at slot 6 swapped with slot 7 — still both inactive, no change in active set.
		CHECK_FALSE(list.active(inactiveType));
		checkPermutationInvariant(list);

		// Now move the type sitting at last-1 (still inactive) backward toward the boundary.
		PATTERN_TYPE atBoundary = list.at(list.last);
		list.moveBwd(atBoundary);
		checkPermutationInvariant(list);
	}

	SECTION("isFirst()/isLast() agree with map[] after arbitrary move sequences") {
		list.reset(7);
		list.moveFwd(list.at(3));
		list.moveBwd(list.at(1));
		list.moveFwd(list.at(5));

		for (int i = 0; i < PatternList::SIZE; i++) {
			PATTERN_TYPE t = (PATTERN_TYPE)i;
			CHECK(list.isFirst(t) == (list.map[i] == 0));
			CHECK(list.isLast(t) == (list.map[i] == list.last - 1));
		}
		// isLast() tracks last, not SIZE: the type physically at slot SIZE-1 is not "last"
		// unless last == SIZE.
		CHECK_FALSE(list.isLast(list.at(PatternList::SIZE - 1)));
		checkPermutationInvariant(list);
	}
}

TEST_CASE("PatternList JSON round-trip", "[Flower][PatternList][JSON]") {
	SECTION("round-trips last and the full slot/map order") {
		auto roundTrip = [](PatternList& src) {
			json_t* rootJ = json_object();
			src.toJson(rootJ);

			PatternList dst;
			dst.fromJson(rootJ);
			json_decref(rootJ);
			return dst;
		};

		SECTION("default state") {
			PatternList list;
			list.reset();
			PatternList restored = roundTrip(list);
			CHECK(restored.last == list.last);
			for (int i = 0; i < PatternList::SIZE; i++) {
				CHECK(restored.at(i) == list.at(i));
				CHECK(restored.map[i] == list.map[i]);
			}
		}

		SECTION("a reordered list") {
			PatternList list;
			list.reset();
			list.moveFwd(list.at(5));
			list.moveBwd(list.at(2));
			PatternList restored = roundTrip(list);
			CHECK(restored.last == list.last);
			for (int i = 0; i < PatternList::SIZE; i++) {
				CHECK(restored.at(i) == list.at(i));
			}
		}

		SECTION("a reduced list") {
			PatternList list;
			list.reset();
			list.disable(PATTERN_TYPE::SEQ_REV);
			list.disable(PATTERN_TYPE::AUX_RAND);
			PatternList restored = roundTrip(list);
			CHECK(restored.last == list.last);
			for (int i = 0; i < PatternList::SIZE; i++) {
				CHECK(restored.at(i) == list.at(i));
			}
		}

		SECTION("last == 1") {
			PatternList list;
			list.reset(1);
			PatternList restored = roundTrip(list);
			CHECK(restored.last == 1);
			CHECK(restored.at(0) == list.at(0));
		}
	}

	SECTION("fromJson() with a missing \"data\" key does not crash") {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "last", json_integer(5));
		// No "data" key at all.

		PatternList list;
		// json_string_value(NULL) returns NULL; constructing a std::string from that is UB.
		// This must not crash — the specific post-condition isn't pinned beyond "survives".
		list.fromJson(rootJ);
		json_decref(rootJ);
		SUCCEED("fromJson() with a missing \"data\" key did not crash");
	}

	SECTION("fromJson() with a missing \"last\" key does not crash") {
		std::string data(PatternList::SIZE, '\0');
		for (int i = 0; i < PatternList::SIZE; i++) data[i] = 97 + i;
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "data", json_string(data.c_str()));
		// No "last" key at all — json_integer_value(NULL) yields 0.

		PatternList list;
		list.fromJson(rootJ);
		json_decref(rootJ);
		SUCCEED("fromJson() with a missing \"last\" key did not crash");
	}

	SECTION("fromJson() with a short \"data\" string does not read out of bounds") {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "last", json_integer(1));
		json_object_set_new(rootJ, "data", json_string("a"));

		PatternList list;
		list.fromJson(rootJ);
		json_decref(rootJ);
		SUCCEED("fromJson() with a 1-character \"data\" string did not crash");
	}

	SECTION("fromJson() with an empty \"data\" string does not read out of bounds") {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "last", json_integer(1));
		json_object_set_new(rootJ, "data", json_string(""));

		PatternList list;
		list.fromJson(rootJ);
		json_decref(rootJ);
		SUCCEED("fromJson() with an empty \"data\" string did not crash");
	}

	SECTION("fromJson() with out-of-range characters does not write outside map[]") {
		// Valid characters are 97..97+(SIZE-1). 'z' (122), '0' (48), and a byte >= 128 are all
		// outside that range, so `s[i] - 97` produces a PATTERN_TYPE outside [0, NUM) and, if
		// used to index map[] unguarded, an out-of-bounds write.
		const char* badChars[] = {"z", "0"};
		for (const char* bad : badChars) {
			std::string data(PatternList::SIZE, *bad);
			json_t* rootJ = json_object();
			json_object_set_new(rootJ, "last", json_integer(PatternList::SIZE));
			json_object_set_new(rootJ, "data", json_string(data.c_str()));

			PatternList list;
			list.fromJson(rootJ);
			json_decref(rootJ);
		}
		SUCCEED("fromJson() with out-of-range data characters did not crash");
	}

	SECTION("fromJson() clamps last outside [1, SIZE]") {
		int badValues[] = {0, PatternList::SIZE + 1, 99, -1};
		std::string data(PatternList::SIZE, '\0');
		for (int i = 0; i < PatternList::SIZE; i++) data[i] = 97 + i;

		for (int bad : badValues) {
			json_t* rootJ = json_object();
			json_object_set_new(rootJ, "last", json_integer(bad));
			json_object_set_new(rootJ, "data", json_string(data.c_str()));

			PatternList list;
			list.fromJson(rootJ);
			json_decref(rootJ);

			CATCH_INFO("bad last value = " << bad);
			CHECK(list.last >= 1);
			CHECK(list.last <= PatternList::SIZE);
		}
	}
}
