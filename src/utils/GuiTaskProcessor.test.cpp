#include "../test/test_plugin.hpp"
#include "../test/test_context.hpp"
#include "GuiTaskProcessor.hpp"
#include <future>
#include <thread>
#include <chrono>

using namespace StoermelderPackOne;

// ---- helpers ----------------------------------------------------------------

static std::future<void> makePromise(std::shared_ptr<std::promise<void>>& out) {
	out = std::make_shared<std::promise<void>>();
	return out->get_future();
}

// A window pointer is only ever compared against null inside GuiTaskProcessor —
// never dereferenced — so a fake non-null value safely stands in for "a real
// rack::window::Window exists" without constructing one.
static rack::window::Window* const FAKE_WINDOW = reinterpret_cast<rack::window::Window*>(1);

// Calls process(nullptr) — the window-absent path — enough times to start the
// worker. Unlike the old threshold-based design, one call is already enough;
// looping just proves repeated calls with no window stay idempotent.
template <size_t SIZE>
static void starveUiThread(GuiTaskProcessor<SIZE>& gtp, int calls = 3) {
	for (int i = 0; i < calls; i++) gtp.process(nullptr);
}

// ---- tests --------------------------------------------------------------------

TEST_CASE("step() drains tasks when called regularly", "[GuiTaskProcessor]") {
	Test::TestContext<> ctx;
	GuiTaskProcessor<8> gtp;

	int execCount = 0;
	gtp.enqueue([&execCount]() { execCount++; });
	gtp.enqueue([&execCount]() { execCount++; });

	gtp.step();

	REQUIRE(execCount == 2);
}

TEST_CASE("process() with a window present never starts a worker", "[GuiTaskProcessor]") {
	Test::TestContext<> ctx;
	GuiTaskProcessor<8> gtp;

	// Interleave process(window-present) with step(), the way the module's process()
	// and the widget's step() run together in the normal (UI present) case.
	for (int i = 0; i < 50; i++) {
		gtp.process(FAKE_WINDOW);
		gtp.step();
	}

	REQUIRE(gtp.workerState.load() == GuiTaskProcessor<8>::WorkerState::Absent);
}

TEST_CASE("process() defaults to APP->window, which is null in the test context", "[GuiTaskProcessor]") {
	Test::TestContext<> ctx;
	REQUIRE(APP->window == nullptr);

	GuiTaskProcessor<8> gtp;
	gtp.process();  // no argument — must fall back to APP->window, i.e. absent here

	auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (gtp.workerState.load() != GuiTaskProcessor<8>::WorkerState::Running
	       && std::chrono::steady_clock::now() < deadline) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	REQUIRE(gtp.workerState.load() == GuiTaskProcessor<8>::WorkerState::Running);
}

TEST_CASE("Worker starts the instant process() sees no window", "[GuiTaskProcessor]") {
	Test::TestContext<> ctx;
	GuiTaskProcessor<8> gtp;

	starveUiThread(gtp, 1);  // a single no-window call is enough — no staleness threshold

	auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (gtp.workerState.load() != GuiTaskProcessor<8>::WorkerState::Running
	       && std::chrono::steady_clock::now() < deadline) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	REQUIRE(gtp.workerState.load() == GuiTaskProcessor<8>::WorkerState::Running);
}

TEST_CASE("Worker drains tasks when no window is present", "[GuiTaskProcessor]") {
	Test::TestContext<> ctx;
	GuiTaskProcessor<8> gtp;

	std::atomic<int> execCount{0};
	std::shared_ptr<std::promise<void>> prom;
	auto fut = makePromise(prom);

	starveUiThread(gtp);
	gtp.enqueue([&execCount, prom]() {
		execCount.fetch_add(1);
		prom->set_value();
	});

	// enqueue() posts the worker's semaphore directly, so this should resolve almost
	// immediately; the timeout is generous headroom, not an expected wait.
	REQUIRE(fut.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
	REQUIRE(execCount == 1);
}

TEST_CASE("Tasks are never run inline on the calling thread", "[GuiTaskProcessor]") {
	Test::TestContext<> ctx;
	GuiTaskProcessor<8> gtp;

	std::thread::id callerId = std::this_thread::get_id();
	std::atomic<bool> ranInline{false};
	std::atomic<bool> ran{false};

	gtp.enqueue([&]() {
		if (std::this_thread::get_id() == callerId) ranInline = true;
		ran = true;
	});
	// enqueue() must return without having executed the task itself.
	REQUIRE(ran == false);

	// Drain via the UI path this time — still must not run on the enqueuing thread
	// (it happens to be the same thread calling step() here, which is allowed; the
	// guarantee under test is that enqueue() itself never runs it synchronously).
	gtp.step();
	REQUIRE(ran == true);
	REQUIRE(ranInline == true);  // step() IS the UI thread in this test — expected

	// Now prove the worker path runs on a distinct thread from the one that enqueued.
	std::atomic<bool> ranOnWorkerThread{false};
	std::shared_ptr<std::promise<void>> prom;
	auto fut = makePromise(prom);
	starveUiThread(gtp);
	gtp.enqueue([&ranOnWorkerThread, callerId, prom]() {
		ranOnWorkerThread = (std::this_thread::get_id() != callerId);
		prom->set_value();
	});
	REQUIRE(fut.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
	REQUIRE(ranOnWorkerThread == true);
}

TEST_CASE("The two drainers never run concurrently", "[GuiTaskProcessor]") {
	Test::TestContext<> ctx;
	GuiTaskProcessor<64> gtp;

	starveUiThread(gtp);  // worker is now running, parked on its semaphore

	// Hammer step() from this thread while enqueue()'s post keeps waking the worker to
	// drain concurrently. If drain() ever let both run at once, TaskProcessor's SPSC
	// RingBuffer (non-atomic read-modify-shift under concurrent shift()) would corrupt
	// state — most reliably surfaced as a crash or a task index being served to two
	// executions. Running many tasks under contention makes that likely to trip.
	std::atomic<int> execCount{0};
	constexpr int N = 200;
	for (int i = 0; i < N; i++) {
		gtp.enqueue([&execCount]() { execCount.fetch_add(1); });
		gtp.step();
		std::this_thread::sleep_for(std::chrono::microseconds(50));
	}

	// Drain whatever the last few enqueues left behind.
	auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (execCount.load() < N && std::chrono::steady_clock::now() < deadline) {
		gtp.step();
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}

	REQUIRE(execCount == N);
}

TEST_CASE("A task queued during teardown either runs or is dropped, never crashes", "[GuiTaskProcessor]") {
	Test::TestContext<> ctx;
	{
		GuiTaskProcessor<8> gtp;
		starveUiThread(gtp);
		gtp.enqueue([]() {});
		// Destructor runs here (stopWorker() joins the worker), racing the worker's
		// own drain of the task just enqueued above.
	}
	SUCCEED("destructor completed without crashing");
}

TEST_CASE("Full queue drops tasks rather than blocking (enqueue never blocks)", "[GuiTaskProcessor]") {
	Test::TestContext<> ctx;
	GuiTaskProcessor<4> gtp;

	// Fill the queue without draining it (no step()/worker involved yet).
	for (int i = 0; i < 4; i++) gtp.enqueue([]() {});
	REQUIRE(gtp.internalQueue.queue.size() == 4);

	// One more enqueue on a full queue must be a silent no-op, not a block or crash.
	REQUIRE_NOTHROW(gtp.enqueue([]() {}));
	REQUIRE(gtp.internalQueue.queue.size() == 4);
}

// Waits for the worker to reach `want`, returning whether it got there.
template <size_t SIZE>
static bool waitForState(GuiTaskProcessor<SIZE>& gtp, typename GuiTaskProcessor<SIZE>::WorkerState want,
                         std::chrono::seconds timeout = std::chrono::seconds(5)) {
	auto deadline = std::chrono::steady_clock::now() + timeout;
	while (gtp.workerState.load() != want && std::chrono::steady_clock::now() < deadline) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	return gtp.workerState.load() == want;
}

TEST_CASE("A window reappearing retires the worker without blocking", "[GuiTaskProcessor]") {
	Test::TestContext<> ctx;
	using State = GuiTaskProcessor<8>::WorkerState;
	GuiTaskProcessor<8> gtp;

	starveUiThread(gtp);
	REQUIRE(waitForState(gtp, State::Running));

	// Window is back: process() must ask the worker to exit rather than join it here.
	gtp.process(FAKE_WINDOW);
	REQUIRE(gtp.workerState.load() == State::Retiring);

	// The worker exits on its own; the thread object is left for a later reap. step()
	// keeps draining in the meantime.
	int execCount = 0;
	gtp.enqueue([&execCount]() { execCount++; });
	gtp.step();
	REQUIRE(execCount == 1);
}

TEST_CASE("The window going away again reclaims a retired worker", "[GuiTaskProcessor]") {
	Test::TestContext<> ctx;
	using State = GuiTaskProcessor<8>::WorkerState;
	GuiTaskProcessor<8> gtp;

	starveUiThread(gtp);
	REQUIRE(waitForState(gtp, State::Running));
	gtp.process(FAKE_WINDOW);            // editor reopened -> retire
	REQUIRE(gtp.workerState.load() == State::Retiring);

	gtp.process(nullptr);                // editor closed again -> reclaim and restart
	REQUIRE(waitForState(gtp, State::Running));

	// The restarted worker must actually drain again.
	std::shared_ptr<std::promise<void>> prom;
	auto fut = makePromise(prom);
	gtp.enqueue([prom]() { prom->set_value(); });
	REQUIRE(fut.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
}

TEST_CASE("Destruction joins a worker that was retired but never reaped", "[GuiTaskProcessor]") {
	Test::TestContext<> ctx;
	using State = GuiTaskProcessor<8>::WorkerState;
	{
		GuiTaskProcessor<8> gtp;
		starveUiThread(gtp);
		REQUIRE(waitForState(gtp, State::Running));
		gtp.process(FAKE_WINDOW);
		REQUIRE(gtp.workerState.load() == State::Retiring);
		// Destructor must join the retiring thread rather than returning early on a
		// state it does not recognise, which would leak/detach a running thread.
	}
	SUCCEED("destructor joined a retiring worker without hanging or crashing");
}

TEST_CASE("Repeated window transitions stay stable", "[GuiTaskProcessor]") {
	Test::TestContext<> ctx;
	using State = GuiTaskProcessor<8>::WorkerState;
	GuiTaskProcessor<8> gtp;

	std::atomic<int> execCount{0};
	constexpr int CYCLES = 20;
	for (int i = 0; i < CYCLES; i++) {
		gtp.process(nullptr);
		gtp.process(FAKE_WINDOW);
		gtp.enqueue([&execCount]() { execCount.fetch_add(1); });
		gtp.step();
	}

	// Whatever state the churn left behind, every task must eventually run and the
	// object must tear down cleanly.
	auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (execCount.load() < CYCLES && std::chrono::steady_clock::now() < deadline) {
		gtp.step();
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	REQUIRE(execCount.load() == CYCLES);
	REQUIRE(gtp.workerState.load() != State::Starting);
}
