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
	Kit<> kit;
	MidiKitModule* m = kit.m;
	drainLog(m);
	m->timingCurrentFrame.store(5000, std::memory_order_relaxed);
	m->writeLog("late");
	ScriptLog::Entry e;
	REQUIRE(m->log.tryPop(e));
	REQUIRE(std::get<2>(e) == "late");
	REQUIRE(std::get<3>(e) == 5000);
}

TEST_CASE("The log time setting is stored in the patch", "[MidiKit][Log][LogTime]") {
	Kit<> kit;
	MidiKitModule* m = kit.m;
	REQUIRE(m->logTime == LOG_TIME::TIMESTAMP);
	m->logTime = LOG_TIME::FRAME;
	json_t* j = m->dataToJson();
	Kit<> kit2;
	MidiKitModule* other = kit2.m;
	other->dataFromJson(j);
	REQUIRE(other->logTime == LOG_TIME::FRAME);

	// An out-of-range value falls back to the default.
	json_object_set_new(j, "logTime", json_integer(99));
	other->dataFromJson(j);
	REQUIRE(other->logTime == LOG_TIME::TIMESTAMP);
	json_decref(j);
}

// Logging (midiLogMessages)

TEST_CASE("Log queue preserves FIFO order", "[MidiKit][Log]") {
	Kit<> kit;
	MidiKitModule* m = kit.m;
	drainLogEntries(m);  // discard construction-time entries

	for (int i = 0; i < 10; i++) {
		m->log.midiLogMessages.try_push(ScriptLog::Entry(LOG_FORMAT::TEXT, 0.f, std::string("line") + std::to_string(i), 0));
	}

	auto entries = drainLogEntries(m);
	REQUIRE(entries.size() == 10);
	for (int i = 0; i < 10; i++) {
		REQUIRE(std::get<1>(entries[i]) == "line" + std::to_string(i));
	}
}


TEST_CASE("Log accepts entries from multiple producers", "[MidiKit][Log]") {
	Kit<> kit;
	MidiKitModule* m = kit.m;
	drainLogEntries(m);  // discard construction-time entries

	// Producer A: the module's handler writeLog (the worker-thread path).
	m->writeLog("from-engine", true);
	// Producer B: a direct push (the loadScript/onReset path).
	m->log.midiLogMessages.try_push(ScriptLog::Entry(LOG_FORMAT::TEXT, 0.f, std::string("from-direct"), 0));
	// Producer A again.
	m->writeLog("from-engine-2", false);

	auto entries = drainLogEntries(m);
	REQUIRE(entries.size() == 3);
	REQUIRE(std::get<1>(entries[0]) == "from-engine");
	REQUIRE(std::get<1>(entries[1]) == "from-direct");
	REQUIRE(std::get<1>(entries[2]) == "from-engine-2");
	// writeLog(useTimestamp=true) -> TIMESTAMP, writeLog(useTimestamp=false) -> TEXT.
	REQUIRE(std::get<0>(entries[0]) == LOG_FORMAT::TIMESTAMP);
	REQUIRE(std::get<0>(entries[1]) == LOG_FORMAT::TEXT);
	REQUIRE(std::get<0>(entries[2]) == LOG_FORMAT::TEXT);
}


TEST_CASE("LoadScript emits a RESET log entry", "[MidiKit][Log]") {
	// The synchronous worker: the test depends on the load having landed.
	Kit<> kit;
	MidiKitModule* m = kit.m;
	drainLogEntries(m);  // discard construction-time entries

	m->loadScript(QUICKJS_SCRIPT);

	auto entries = drainLogEntries(m);
	REQUIRE(!entries.empty());
	// loadScript() pushes the RESET marker before any script output.
	REQUIRE(std::get<0>(entries[0]) == LOG_FORMAT::RESET);
}


TEST_CASE("Log queue drops entries when full", "[MidiKit][Log]") {
	Kit<> kit;
	MidiKitModule* m = kit.m;
	drainLogEntries(m);  // discard construction-time entries

	// The queue holds exactly 512 entries; pushing more must drop (try_push
	// returns false) rather than block.
	int pushed = 0;
	for (int i = 0; i < 1000; i++) {
		if (m->log.midiLogMessages.try_push(ScriptLog::Entry(LOG_FORMAT::TEXT, 0.f, std::string("x"), 0))) {
			pushed++;
		}
	}
	REQUIRE(pushed == 512);

	// Every accepted entry is still drained out (no loss of accepted entries).
	auto entries = drainLogEntries(m);
	REQUIRE(entries.size() == 512);
}

// Notices raised from the audio thread: a flag set, no string built there. They
// become log lines when the log is drained, and repeats before that are one.
TEST_CASE("Raised log notices become one line each when the log is drained", "[MidiKit][Log]") {
	ScriptLog log;
	ScriptLog::Entry t;

	REQUIRE_FALSE(log.tryPop(t));

	log.raise(ScriptLog::OUTPUT_QUEUE_FULL);
	log.raise(ScriptLog::OUTPUT_QUEUE_FULL);
	log.raise(ScriptLog::TRIGGER_QUEUE_FULL);
	REQUIRE(log.tryPop(t));
	REQUIRE(std::get<2>(t) == "MIDI output queue full, message(s) dropped");
	REQUIRE(log.tryPop(t));
	REQUIRE(std::get<2>(t) == "Trigger output queue full, write(s) dropped");
	REQUIRE_FALSE(log.tryPop(t));

	// Raising again after the drain reports again.
	log.raise(ScriptLog::OUTPUT_QUEUE_FULL);
	REQUIRE(log.tryPop(t));

	log.raise(ScriptLog::TIMING_LATE);
	log.raise(ScriptLog::TIMING_LATE);
	REQUIRE(log.tryPop(t));
	REQUIRE(std::get<2>(t) == "Timing: message(s) reached the output too late");
	REQUIRE_FALSE(log.tryPop(t));

	log.raise(ScriptLog::TIPSY_INPUT_MALFORMED);
	log.raise(ScriptLog::TIPSY_INPUT_QUEUE_FULL);
	REQUIRE(log.tryPop(t));
	REQUIRE(std::get<2>(t) == "Tipsy input: malformed stream");
	REQUIRE(log.tryPop(t));
	REQUIRE(std::get<2>(t) == "Tipsy input queue full, message(s) dropped");

	log.raise(ScriptLog::SCHEDULE_QUEUE_FULL);
	REQUIRE(log.tryPop(t));
	REQUIRE(std::get<2>(t) == "MIDI schedule queue full, message(s) sent at once");
}


// Repeats: the worker logs identical consecutive lines three times, the rest become
// one "… repeated N×" line, so a callback that fails on every clock tick cannot
// push everything else out of the log. Other producers (push()) are not guarded.
TEST_CASE("The log collapses identical consecutive lines after three", "[MidiKit][Log]") {
	ScriptLog log;
	for (int i = 0; i < 10; i++) log.pushText("same", 0.f, true);
	log.pushText("other", 0.f, true);
	log.pushText("same", 0.f, true);

	std::vector<std::string> lines;
	ScriptLog::Entry t;
	while (log.tryPop(t)) lines.push_back(std::get<2>(t));
	REQUIRE(lines == std::vector<std::string>{ "same", "same", "same", "… repeated 7×", "other", "same" });
}

TEST_CASE("A flood stays collapsed while it goes on and is reported once it has stopped", "[MidiKit][Log]") {
	ScriptLog log;
	log.repeats.quietMs = 60000;   // the flood has not stopped
	ScriptLog::Entry t;
	auto drain = [&]() {
		std::vector<std::string> lines;
		while (log.tryPop(t)) lines.push_back(std::get<2>(t));
		return lines;
	};
	for (int i = 0; i < 6; i++) log.pushText("tick", 0.f, true);
	REQUIRE(drain() == std::vector<std::string>{ "tick", "tick", "tick" });
	// Drain after drain, one more repeat each time: still nothing.
	for (int i = 0; i < 5; i++) {
		log.pushText("tick", 0.f, true);
		REQUIRE(drain().empty());
	}

	// Quiet for long enough: the drain reports all 8 (3 + 5) at once.
	log.repeats.quietMs = 0;
	REQUIRE(drain() == std::vector<std::string>{ "… repeated 8×" });
	// And only once; the same line afterwards is still part of the run.
	log.pushText("tick", 0.f, true);
	REQUIRE(drain() == std::vector<std::string>{ "… repeated 1×" });
}

TEST_CASE("A reset ends a run of repeats, and the same line is logged again after it", "[MidiKit][Log]") {
	ScriptLog log;
	for (int i = 0; i < 5; i++) log.pushText("err", 0.f, true);
	log.pushReset();
	for (int i = 0; i < 2; i++) log.pushText("err", 0.f, true);

	std::vector<std::string> lines;
	ScriptLog::Entry t;
	while (log.tryPop(t)) lines.push_back(std::get<LOG_FORMAT>(t) == LOG_FORMAT::RESET ? std::string("<reset>") : std::get<2>(t));
	REQUIRE(lines == std::vector<std::string>{ "err", "err", "err", "… repeated 2×", "<reset>", "err", "err" });
}

TEST_CASE("A callback that fails on every message logs its error three times, then a count", "[MidiKit][Log][CrossEngine]") {
	FOR_EACH_LANG;
	Kit<> kit;
	kit.m->log.repeats.quietMs = 0;   // report the count on the first drain
	kit.load(onMessage(lang, lang == Lang::Js ? "let x = null; x.field;" : "local x = nil; return x.field"));
	for (int i = 0; i < 50; i++) kit.dispatch(msg::noteOn(1, 60, 100));
	std::string log = kit.log();
	CATCH_INFO("log:\n" << log);
	REQUIRE(countOf(log, "onMessage error") == 3);
	REQUIRE(log.find("… repeated 47×") != std::string::npos);

	// The log still takes other lines during the flood.
	kit.m->writeLog("something else", false);
	REQUIRE(kit.log().find("something else") != std::string::npos);
}

TEST_CASE("Lines from the other producers are never collapsed", "[MidiKit][Log]") {
	ScriptLog log;
	for (int i = 0; i < 6; i++) log.pushText("same");
	size_t n = 0;
	ScriptLog::Entry t;
	while (log.tryPop(t)) n++;
	REQUIRE(n == 6);
}
