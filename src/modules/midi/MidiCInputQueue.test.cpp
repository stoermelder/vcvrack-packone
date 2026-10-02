#include "../../test/framework.hpp"
#include "MidiCInputQueue.hpp"
#include <random>
#include <thread>
#include <utility>

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
}

using namespace StoermelderPackOne;

static rack::midi::Message msgAt(int64_t frame, uint8_t b1 = 0, uint8_t b2 = 0) {
	return Test::makeMidiMessage(0xb, 0, b1, b2, frame);
}

// The queue under test, drained at maxFrame into (frame, b1, b2) tuples.
template <typename Q>
static std::vector<std::vector<int>> drain(Q& q, int64_t maxFrame) {
	std::vector<std::vector<int>> out;
	rack::midi::Message m;
	while (q.tryPop(&m, maxFrame)) {
		out.push_back({int(m.getFrame()), m.bytes[1], m.bytes[2]});
	}
	return out;
}

// The same random pushes and drains, applied to a queue of either type.
template <typename Q>
static std::vector<std::vector<int>> runRandom(int& pushed) {
	std::mt19937 rng(1234);
	Q q;
	std::vector<std::vector<int>> popped;
	int n = 0;
	for (int round = 0; round < 200; round++) {
		int pushes = rng() % 6;
		for (int i = 0; i < pushes; i++) {
			int64_t frame;
			switch (rng() % 4) {
				case 0: frame = int64_t(rng() % 50); break;                 // past/equal
				case 1: frame = 100 + int64_t(rng() % 20); break;           // near future
				case 2: frame = 100000 + int64_t(rng() % 1000); break;      // far future
				default: frame = 100 + int64_t(rng() % 3); break;           // many equal frames
			}
			q.onMessage(msgAt(frame, n & 0x7f, (n >> 7) & 0x7f));
			n++;
		}
		auto got = drain(q, 100 + int64_t(rng() % 30));
		popped.insert(popped.end(), got.begin(), got.end());
	}
	auto rest = drain(q, INT64_MAX);
	popped.insert(popped.end(), rest.begin(), rest.end());
	pushed = n;
	return popped;
}

// The popped sequence of the replacement must be identical to Rack's.
TEST_CASE("MidiCInputQueue releases exactly like InputQueue", "[MidiCInputQueue]") {
	int nRack = 0, nRt = 0;
	auto rack = runRandom<rack::midi::InputQueue>(nRack);
	auto rt = runRandom<MidiCInputQueue<>>(nRt);
	REQUIRE(nRack == nRt);
	REQUIRE(int(rack.size()) == nRack);
	REQUIRE(rt == rack);
}

// Every expectation below runs against Rack's own queue first, then the replacement.
TEMPLATE_TEST_CASE("InputQueue conformance: ordering and gate", "[MidiCInputQueue]", rack::midi::InputQueue, MidiCInputQueue<>) {
	TestType q;

	SECTION("out of order frames are released by frame") {
		q.onMessage(msgAt(200, 1));
		q.onMessage(msgAt(100, 2));
		auto a = drain(q, 150);
		REQUIRE(a.size() == 1);
		REQUIRE(a[0][1] == 2);
		auto b = drain(q, 200);
		REQUIRE(b.size() == 1);
		REQUIRE(b[0][1] == 1);
	}
	SECTION("equal frames keep arrival order") {
		for (int i = 0; i < 4; i++) q.onMessage(msgAt(10, i));
		auto a = drain(q, 10);
		REQUIRE(a.size() == 4);
		for (int i = 0; i < 4; i++) REQUIRE(a[i][1] == i);
	}
	SECTION("a far-future message blocks nothing") {
		q.onMessage(msgAt(1000000, 1));
		q.onMessage(msgAt(5, 2));
		auto a = drain(q, 10);
		REQUIRE(a.size() == 1);
		REQUIRE(a[0][1] == 2);
	}
	SECTION("frame gate edges") {
		q.onMessage(msgAt(50, 1));
		REQUIRE(drain(q, 49).empty());
		REQUIRE(drain(q, 50).size() == 1);
		q.onMessage(msgAt(-1, 2));
		REQUIRE(drain(q, 0).size() == 1);
	}
	SECTION("size counts queued messages, due or not") {
		q.onMessage(msgAt(10));
		q.onMessage(msgAt(1000));
		REQUIRE(q.size() == 2);
		rack::midi::Message m;
		REQUIRE(q.tryPop(&m, 10));
		REQUIRE(q.size() == 1);
		REQUIRE(!q.tryPop(&m, 10));
		REQUIRE(q.size() == 1);
	}
	SECTION("reset deselects and keeps queued messages") {
		q.setDriverId(-1);
		q.setChannel(3);
		q.onMessage(msgAt(1));
		q.reset();
		REQUIRE(q.getDriverId() == -1);
		REQUIRE(q.getChannel() == -1);
		REQUIRE(q.size() == 1);
	}
	SECTION("json round trip") {
		q.setChannel(5);
		json_t* j = q.toJson();
		TestType r;
		r.fromJson(j);
		json_decref(j);
		REQUIRE(r.getChannel() == 5);
	}
	SECTION("usable as a midi::Port") {
		rack::midi::Port* port = &q;
		json_t* j = port->toJson();
		REQUIRE(j != nullptr);
		json_decref(j);
	}
}

TEST_CASE("RtInputQueue peek/pop", "[MidiCInputQueue]") {
	MidiCInputQueue<> q;
	q.onMessage(msgAt(30, 3));
	q.onMessage(msgAt(10, 1));
	q.onMessage(msgAt(20, 2));

	REQUIRE(q.peek(5) == nullptr);
	const rack::midi::Message* a = q.peek(100);
	REQUIRE(a != nullptr);
	// Idempotent without pop().
	REQUIRE(q.peek(100) == a);
	REQUIRE(a->bytes[1] == 1);
	q.pop();
	for (int expected = 2; expected <= 3; expected++) {
		const rack::midi::Message* m = q.peek(100);
		REQUIRE(m != nullptr);
		REQUIRE(m->bytes[1] == expected);
		q.pop();
	}
	REQUIRE(q.peek(100) == nullptr);
	REQUIRE(q.size() == 0);
}

TEST_CASE("MidiCInputQueue clear empties both stages and keeps the free list", "[MidiCInputQueue]") {
	MidiCInputQueue<8, 4> q;
	for (int round = 0; round < 3; round++) {
		// Two in the heap, then more still in the ring.
		q.onMessage(msgAt(1000));
		q.onMessage(msgAt(1001));
		REQUIRE(q.peek(0) == nullptr);
		q.onMessage(msgAt(1002));
		REQUIRE(q.size() == 3);
		q.clear();
		REQUIRE(q.size() == 0);
		REQUIRE(q.freeCount == 4);
		q.overflow.store(false);
		// Fill to capacity again: nothing may be dropped.
		for (int i = 0; i < 4; i++) q.onMessage(msgAt(2000 + i));
		REQUIRE(q.peek(0) == nullptr);
		REQUIRE(q.size() == 4);
		REQUIRE(!q.overflow.load());
		q.clear();
	}
}

TEST_CASE("MidiCInputQueue overflow drops and flags, never overwrites", "[MidiCInputQueue]") {
	SECTION("ring full") {
		MidiCInputQueue<4, 16> q;
		for (int i = 0; i < 4; i++) q.onMessage(msgAt(i, i));
		REQUIRE(!q.overflow.load());
		q.onMessage(msgAt(99, 99));
		REQUIRE(q.overflow.exchange(false));
		auto got = drain(q, INT64_MAX);
		REQUIRE(got.size() == 4);
		for (int i = 0; i < 4; i++) REQUIRE(got[i][1] == i);
	}
	SECTION("heap full") {
		MidiCInputQueue<8, 3> q;
		for (int i = 0; i < 3; i++) q.onMessage(msgAt(1000 + i, i));
		REQUIRE(q.peek(0) == nullptr);
		q.onMessage(msgAt(1003, 3));
		REQUIRE(q.peek(0) == nullptr);
		REQUIRE(q.overflow.exchange(false));
		auto got = drain(q, INT64_MAX);
		REQUIRE(got.size() == 3);
		for (int i = 0; i < 3; i++) REQUIRE(got[i][1] == i);
	}
}

TEST_CASE("MidiCInputQueue consumer does not reallocate", "[MidiCInputQueue]") {
	MidiCInputQueue<8, 8> q;
	rack::midi::Message out;
	out.bytes.reserve(MidiCInputQueue<>::SLOT_BYTES);
	const unsigned char* outData = out.bytes.data();

	std::vector<const unsigned char*> ring, pool;
	for (auto& s : q.arrival.slots) ring.push_back(s.bytes.data());
	for (auto& s : q.pool) pool.push_back(s.bytes.data());

	for (int round = 0; round < 50; round++) {
		for (int i = 0; i < 6; i++) q.onMessage(msgAt(round * 10 + i, i, round & 0x7f));
		while (q.tryPop(&out, INT64_MAX)) {}
	}
	REQUIRE(out.bytes.data() == outData);
	for (size_t i = 0; i < ring.size(); i++) REQUIRE(q.arrival.slots[i].bytes.data() == ring[i]);
	for (size_t i = 0; i < pool.size(); i++) REQUIRE(q.pool[i].bytes.data() == pool[i]);
}

TEST_CASE("MidiCInputQueue two producers one consumer", "[MidiCInputQueue]") {
	MidiCInputQueue<4096, 4096> q;
	// Messages pushed but not yet popped; producers wait above the ring's size so
	// that nothing can be dropped (checking full() themselves would race).
	std::atomic<int> inFlight{0};
	constexpr int PER = 20000;
	// Payload: producer id and a per-producer sequence number. All of a producer's
	// messages carry one frame, so each producer's order must be kept.
	auto producer = [&](int id) {
		for (int i = 0; i < PER; i++) {
			rack::midi::Message m = Test::makeMidiMessage(0xb, 0, id, 0, 7);
			m.bytes = {0xb0, (unsigned char)id, (unsigned char)(i & 0x7f), (unsigned char)((i >> 7) & 0x7f), (unsigned char)((i >> 14) & 0x7f)};
			while (inFlight.load() >= 2048) std::this_thread::yield();
			inFlight.fetch_add(1);
			q.onMessage(m);
		}
	};
	std::thread t0(producer, 0), t1(producer, 1);

	int next[2] = {0, 0};
	int total = 0;
	rack::midi::Message m;
	while (total < 2 * PER) {
		if (q.tryPop(&m, 7)) {
			int id = m.bytes[1];
			int seq = m.bytes[2] | (m.bytes[3] << 7) | (m.bytes[4] << 14);
			REQUIRE(seq == next[id]);
			next[id]++;
			total++;
			inFlight.fetch_sub(1);
		}
		else {
			std::this_thread::yield();
		}
	}
	t0.join();
	t1.join();
	REQUIRE(!q.overflow.load());
	REQUIRE(next[0] == PER);
	REQUIRE(next[1] == PER);
}
