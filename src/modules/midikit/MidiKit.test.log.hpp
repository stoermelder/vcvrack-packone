#include "MidiKit.test.hpp"

// LogDispatcher: fan-out of one queue to several subscribers.

namespace {

struct IntSource {
	std::vector<int> items;
	size_t next = 0;
	bool tryPop(int& out) {
		if (next >= items.size()) return false;
		out = items[next++];
		return true;
	}
};

}

TEST_CASE("LogDispatcher: every listener sees every entry, in subscription order", "[MidiKit][Log]") {
	LogDispatcher<int> d;
	std::vector<std::string> seen;
	d.add([&](const int& e) { seen.push_back("a" + std::to_string(e)); });
	d.add([&](const int& e) { seen.push_back("b" + std::to_string(e)); });
	IntSource src;
	src.items = {1, 2};

	REQUIRE(d.pump(src) == 2);
	REQUIRE(seen == std::vector<std::string>({"a1", "b1", "a2", "b2"}));
	REQUIRE(d.pump(src) == 0);
}

TEST_CASE("LogDispatcher: a removed listener stops receiving, the others carry on", "[MidiKit][Log]") {
	LogDispatcher<int> d;
	int a = 0, b = 0;
	int idA = d.add([&](const int&) { a++; });
	d.add([&](const int&) { b++; });
	REQUIRE(d.size() == 2);

	d.dispatch(1);
	d.remove(idA);
	REQUIRE(d.size() == 1);
	d.dispatch(2);
	REQUIRE(a == 1);
	REQUIRE(b == 2);
}

TEST_CASE("LogDispatcher: a listener may remove itself or another while dispatching", "[MidiKit][Log]") {
	LogDispatcher<int> d;
	int calls = 0, other = 0;
	int selfId = -1, otherId = -1;
	selfId = d.add([&](const int&) { calls++; d.remove(selfId); d.remove(otherId); });
	otherId = d.add([&](const int&) { other++; });

	d.dispatch(1);
	REQUIRE(calls == 1);
	REQUIRE(other == 0);       // removed before its turn
	REQUIRE(d.size() == 0);
	d.dispatch(2);
	REQUIRE(calls == 1);
}

TEST_CASE("LogDispatcher: no listeners drops entries without trouble", "[MidiKit][Log]") {
	LogDispatcher<int> d;
	IntSource src;
	src.items = {1, 2, 3};
	REQUIRE(d.pump(src) == 3);
}

// ── Log time display ─────────────────────────────────────────────────────────

TEST_CASE("A timestamped log line shows seconds, the engine frame, or nothing", "[MidiKit][Log][LogTime]") {
	ScriptLog::Entry e(LOG_FORMAT::TIMESTAMP, 1.5f, "hello", 123456);
	REQUIRE(formatLogEntry(e) == "[   1.5000] hello");
	REQUIRE(formatLogEntry(e, LOG_TIME::TIMESTAMP) == "[   1.5000] hello");
	REQUIRE(formatLogEntry(e, LOG_TIME::FRAME) == "[   123456] hello");
	REQUIRE(formatLogEntry(e, LOG_TIME::OFF) == "hello");

	// Untimed lines are unaffected.
	ScriptLog::Entry text(LOG_FORMAT::TEXT, 0.f, "plain", 0);
	for (LOG_TIME t : {LOG_TIME::TIMESTAMP, LOG_TIME::FRAME, LOG_TIME::OFF}) {
		REQUIRE(formatLogEntry(text, t) == "plain");
	}
}

TEST_CASE("writeLog records the engine frame it ran on", "[MidiKit][Log][LogTime]") {
	ModuleScaffold mods;
	MidiKitModule* m = mods.create();
	drainLog(m);
	m->timingCurrentFrame.store(5000, std::memory_order_relaxed);
	m->writeLog("late");
	ScriptLog::Entry e;
	REQUIRE(m->log.tryPop(e));
	REQUIRE(std::get<2>(e) == "late");
	REQUIRE(std::get<3>(e) == 5000);
}

TEST_CASE("The log time setting is stored in the patch", "[MidiKit][Log][LogTime]") {
	ModuleScaffold mods;
	MidiKitModule* m = mods.create();
	REQUIRE(m->logTime == LOG_TIME::TIMESTAMP);
	m->logTime = LOG_TIME::FRAME;
	json_t* j = m->dataToJson();
	MidiKitModule* other = mods.create();
	other->dataFromJson(j);
	REQUIRE(other->logTime == LOG_TIME::FRAME);

	// An out-of-range value falls back to the default.
	json_object_set_new(j, "logTime", json_integer(99));
	other->dataFromJson(j);
	REQUIRE(other->logTime == LOG_TIME::TIMESTAMP);
	json_decref(j);
}
