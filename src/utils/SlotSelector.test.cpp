#include "../test/test_plugin.hpp"
#include "SlotSelector.hpp"

using namespace StoermelderPackOne;

namespace {
uint32_t gScript[8];
int gScriptPos = 0;
uint32_t scripted() { return gScript[gScriptPos++ % 8]; }
void script(std::initializer_list<uint32_t> v) {
	gScriptPos = 0;
	int i = 0;
	for (uint32_t x : v) gScript[i++] = x;
}
}

TEST_CASE("Mode classes", "[SlotSelector]") {
	REQUIRE(SlotSelector::isTriggerMode(SlotCvMode::TRIG_FWD));
	REQUIRE(SlotSelector::isTriggerMode(SlotCvMode::TRIG_SHUFFLE));
	REQUIRE_FALSE(SlotSelector::isTriggerMode(SlotCvMode::VOLT));
	REQUIRE_FALSE(SlotSelector::isTriggerMode(SlotCvMode::ARM));
	REQUIRE_FALSE(SlotSelector::isTriggerMode(SlotCvMode::OFF));
	REQUIRE(SlotSelector::isLevelMode(SlotCvMode::VOLT));
	REQUIRE(SlotSelector::isLevelMode(SlotCvMode::C4));
	REQUIRE_FALSE(SlotSelector::isLevelMode(SlotCvMode::TRIG_REV));
}

TEST_CASE("Level modes map the voltage into the range", "[SlotSelector]") {
	REQUIRE(SlotSelector::forLevel(SlotCvMode::VOLT, 0.f, 0, 32) == 0);
	REQUIRE(SlotSelector::forLevel(SlotCvMode::VOLT, 5.f, 0, 32) == 16);
	REQUIRE(SlotSelector::forLevel(SlotCvMode::VOLT, 10.f, 0, 32) == 31);
	REQUIRE(SlotSelector::forLevel(SlotCvMode::VOLT, -3.f, 0, 32) == 0);
	REQUIRE(SlotSelector::forLevel(SlotCvMode::VOLT, 0.f, 4, 12) == 4);       // offset range
	REQUIRE(SlotSelector::forLevel(SlotCvMode::VOLT, 10.f, 4, 12) == 11);
	REQUIRE(SlotSelector::forLevel(SlotCvMode::C4, 5.f / 12.f, 0, 32) == 5);  // 1 V/oct: 5 semitones
	REQUIRE(SlotSelector::forLevel(SlotCvMode::C4, 9.f, 0, 8) == 7);
	REQUIRE(SlotSelector::forLevel(SlotCvMode::C4, 0.f, 3, 8) == 3);
	REQUIRE(SlotSelector::forLevel(SlotCvMode::TRIG_FWD, 5.f, 0, 32) == -1);
}

TEST_CASE("Forward and reverse wrap within the range", "[SlotSelector]") {
	SlotSelector s;
	REQUIRE(s.next(SlotCvMode::TRIG_FWD, 4, 0, 32) == 5);
	REQUIRE(s.next(SlotCvMode::TRIG_FWD, 31, 0, 32) == 0);
	REQUIRE(s.next(SlotCvMode::TRIG_FWD, 7, 3, 8) == 3);
	REQUIRE(s.next(SlotCvMode::TRIG_REV, 4, 0, 32) == 3);
	REQUIRE(s.next(SlotCvMode::TRIG_REV, 0, 0, 32) == 31);
	REQUIRE(s.next(SlotCvMode::TRIG_REV, 3, 3, 8) == 7);
}

TEST_CASE("A non-trigger mode keeps the current slot", "[SlotSelector]") {
	SlotSelector s;
	REQUIRE(s.next(SlotCvMode::OFF, 7, 0, 32) == 7);
	REQUIRE(s.next(SlotCvMode::VOLT, 7, 0, 32) == 7);
	REQUIRE(s.next(SlotCvMode::ARM, 7, 0, 32) == 7);
}

TEST_CASE("Pingpong turns around at both ends", "[SlotSelector]") {
	SlotSelector s;
	int b = 30;
	b = s.next(SlotCvMode::TRIG_PINGPONG, b, 0, 32);
	REQUIRE(b == 31);
	REQUIRE(s.direction() == -1);
	b = s.next(SlotCvMode::TRIG_PINGPONG, b, 0, 32);
	REQUIRE(b == 30);
	for (int i = 0; i < 30; i++) b = s.next(SlotCvMode::TRIG_PINGPONG, b, 0, 32);
	REQUIRE(b == 0);
	REQUIRE(s.direction() == 1);
	REQUIRE(s.next(SlotCvMode::TRIG_PINGPONG, b, 0, 32) == 1);
}

TEST_CASE("Alternating returns to the first slot between steps, without repeating it", "[SlotSelector]") {
	SlotSelector s;
	// Range 0..3: up 1, 2, 3, then down 2, 1, then up again, each step separated by a 0.
	int expected[] = {1, 0, 2, 0, 3, 0, 2, 0, 1, 0, 2, 0, 3};
	int b = 0;
	for (int e : expected) {
		b = s.next(SlotCvMode::TRIG_ALT, b, 0, 4);
		REQUIRE(b == e);
	}
}

TEST_CASE("Alternating works on an offset range", "[SlotSelector]") {
	SlotSelector s;
	int expected[] = {5, 4, 6, 4, 7, 4, 6, 4, 5};
	int b = 4;
	for (int e : expected) {
		b = s.next(SlotCvMode::TRIG_ALT, b, 4, 8);
		REQUIRE(b == e);
	}
}

TEST_CASE("Random, without repeat and walk use the injected randomness", "[SlotSelector]") {
	SlotSelector s;
	s.random = &scripted;

	script({37});
	REQUIRE(s.next(SlotCvMode::TRIG_RANDOM, 3, 0, 32) == 37 % 32);
	script({37});
	REQUIRE(s.next(SlotCvMode::TRIG_RANDOM, 3, 10, 18) == 10 + 37 % 8);

	script({4});
	REQUIRE(s.next(SlotCvMode::TRIG_RANDOM_WO_REPEAT, 10, 0, 32) == 4);
	script({10});
	REQUIRE(s.next(SlotCvMode::TRIG_RANDOM_WO_REPEAT, 10, 0, 32) == 11);
	for (uint32_t r = 0; r < 31; r++) {
		script({r});
		REQUIRE(s.next(SlotCvMode::TRIG_RANDOM_WO_REPEAT, 10, 0, 32) != 10);
	}

	script({0});
	REQUIRE(s.next(SlotCvMode::TRIG_RANDOM_WALK, 5, 0, 32) == 4);
	script({1});
	REQUIRE(s.next(SlotCvMode::TRIG_RANDOM_WALK, 5, 0, 32) == 6);
	script({0});
	REQUIRE(s.next(SlotCvMode::TRIG_RANDOM_WALK, 0, 0, 32) == 0);
}

TEST_CASE("Shuffle plays every slot once per deck, then reshuffles", "[SlotSelector]") {
	SlotSelector s;
	std::vector<bool> seen(12, false);
	REQUIRE(s.deckEmpty());
	for (int i = 0; i < 8; i++) {
		int b = s.next(SlotCvMode::TRIG_SHUFFLE, 0, 4, 12);
		REQUIRE(b >= 4);
		REQUIRE(b < 12);
		REQUIRE_FALSE(seen[(size_t) b]);
		seen[(size_t) b] = true;
	}
	REQUIRE(s.deckEmpty());
}

TEST_CASE("reset returns to a fresh state", "[SlotSelector]") {
	SlotSelector s;
	s.next(SlotCvMode::TRIG_PINGPONG, 31, 0, 32); // direction now -1
	s.next(SlotCvMode::TRIG_SHUFFLE, 0, 0, 32);   // deck now partly used
	s.reset();
	REQUIRE(s.direction() == 1);
	REQUIRE(s.deckEmpty());
	REQUIRE(s.alt() == 0);
}
