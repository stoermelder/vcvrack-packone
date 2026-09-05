#include "../test/framework.hpp"
#include "MpmcTaskWorker.hpp"
#include <future>
#include <thread>
#include <chrono>

using namespace StoermelderPackOne;

static std::future<void> makePromise(std::shared_ptr<std::promise<void>>& out) {
	out = std::make_shared<std::promise<void>>();
	return out->get_future();
}

TEST_CASE("Task executes exactly once", "[MpmcTaskWorker]") {
	Test::TestContext<> ctx;
	MpmcTaskWorker worker;

	std::atomic<int> execCount{0};
	std::shared_ptr<std::promise<void>> prom;
	auto fut = makePromise(prom);

	worker.work([&execCount, prom]() {
		execCount.fetch_add(1);
		prom->set_value();
	});

	REQUIRE(fut.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
	REQUIRE(execCount == 1);
}

TEST_CASE("Multiple tasks execute serially in submission order", "[MpmcTaskWorker]") {
	Test::TestContext<> ctx;
	MpmcTaskWorker worker;

	constexpr int N = 8;
	std::vector<int> order;
	std::mutex orderMutex;
	std::atomic<int> doneCount{0};
	std::shared_ptr<std::promise<void>> lastProm;
	auto fut = makePromise(lastProm);

	for (int i = 0; i < N; i++) {
		auto prom = (i == N - 1) ? lastProm : std::make_shared<std::promise<void>>();
		worker.work([i, &order, &orderMutex, &doneCount, prom]() {
			{
				std::lock_guard<std::mutex> lk(orderMutex);
				order.push_back(i);
			}
			doneCount.fetch_add(1);
			if (i == N - 1) prom->set_value();
		});
	}

	REQUIRE(fut.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
	REQUIRE(doneCount == N);

	std::vector<int> expected(N);
	std::iota(expected.begin(), expected.end(), 0);
	REQUIRE(order == expected);
}

// work() uses try_push(): under a concurrent burst that outruns the worker,
// the fixed-capacity queue can be momentarily full and a submission is
// dropped (returns false) rather than blocking the producer or overwriting a
// pending task. This test doesn't assume every submission lands -- it counts
// how many were accepted and asserts exactly that many run, exactly once.
TEST_CASE("Concurrent producers from multiple threads: every accepted task runs exactly once", "[MpmcTaskWorker]") {
	Test::TestContext<> ctx;
	MpmcTaskWorker worker;

	constexpr int PRODUCERS = 8;
	constexpr int PER_PRODUCER = 50;
	std::atomic<int> execCount{0};
	std::atomic<int> acceptedCount{0};

	std::vector<std::thread> threads;
	for (int p = 0; p < PRODUCERS; p++) {
		threads.emplace_back([&worker, &execCount, &acceptedCount]() {
			for (int i = 0; i < PER_PRODUCER; i++) {
				bool ok = worker.work([&execCount]() {
					execCount.fetch_add(1);
				});
				if (ok) acceptedCount.fetch_add(1);
			}
		});
	}
	for (auto& t : threads) t.join();

	int expected = acceptedCount.load();
	auto start = std::chrono::steady_clock::now();
	while (execCount.load() < expected) {
		if (std::chrono::steady_clock::now() - start > std::chrono::seconds(5)) break;
		std::this_thread::yield();
	}

	REQUIRE(expected > 0);
	REQUIRE(execCount == expected);
}

// drain() pops and discards queued tasks without running them. We block the
// worker on a first task so the following tasks stay queued, drain, then
// release the block and assert only the blocking task ran.
TEST_CASE("drain() discards queued tasks without running them", "[MpmcTaskWorker]") {
	Test::TestContext<> ctx;
	MpmcTaskWorker worker;

	std::atomic<int> execCount{0};
	std::atomic<bool> workerBusy{false};
	std::atomic<bool> blockDone{false};
	// Create the promise directly (not via makePromise) so the task can
	// retrieve its future; makePromise already retrieves it.
	auto releaseProm = std::make_shared<std::promise<void>>();

	// Block the worker on the first task so the rest stay queued.
	worker.work([&execCount, &workerBusy, &blockDone, releaseProm]() {
		execCount.fetch_add(1);
		workerBusy.store(true, std::memory_order_release);
		releaseProm->get_future().wait();
		blockDone.store(true, std::memory_order_release);
	});
	while (!workerBusy.load(std::memory_order_acquire)) std::this_thread::yield();

	// Queue 5 more tasks; they must stay queued because the worker is blocked.
	for (int i = 0; i < 5; i++) {
		worker.work([&execCount]() { execCount.fetch_add(1); });
	}
	worker.drain();

	// Release the worker; only the blocking task should have run.
	releaseProm->set_value();
	while (!blockDone.load(std::memory_order_acquire)) std::this_thread::yield();
	REQUIRE(execCount == 1);
}

TEST_CASE("drain() leaves the worker usable and the counter balanced", "[MpmcTaskWorker]") {
	// drain() removes items the worker never popped, so it owns the matching
	// pendingTasks decrements. Getting that wrong is not cosmetic: the worker
	// drains BY COUNTER, so a counter left too high spins it forever on an
	// empty queue (try_pop fails, counter still positive, retry), and one left
	// negative makes it skip the drain loop and ignore queued work.
	//
	// The existing drain() test only asserts that discarded tasks did not run.
	// This asserts the worker still functions afterwards, which is what a
	// mismatched counter actually breaks.
	Test::TestContext<> ctx;
	MpmcTaskWorker worker;

	std::atomic<int> execCount{0};
	std::atomic<bool> workerBusy{false};
	std::atomic<bool> blockDone{false};
	auto releaseProm = std::make_shared<std::promise<void>>();

	// Block the worker so the following pushes stay queued for drain().
	worker.work([&workerBusy, &blockDone, releaseProm]() {
		workerBusy.store(true, std::memory_order_release);
		releaseProm->get_future().wait();
		blockDone.store(true, std::memory_order_release);
	});
	while (!workerBusy.load(std::memory_order_acquire)) std::this_thread::yield();

	for (int i = 0; i < 5; i++) {
		worker.work([&execCount]() { execCount.fetch_add(1); });
	}
	worker.drain();
	REQUIRE(worker.pendingTasks.load() == 0);

	releaseProm->set_value();
	while (!blockDone.load(std::memory_order_acquire)) std::this_thread::yield();
	REQUIRE(execCount == 0);   // all five were discarded

	// The worker must still run new work. A counter corrupted by drain() shows
	// up here as a timeout rather than as a wrong count above.
	std::shared_ptr<std::promise<void>> prom;
	auto fut = makePromise(prom);
	REQUIRE(worker.work([&execCount, prom]() {
		execCount.fetch_add(1);
		prom->set_value();
	}));
	REQUIRE(fut.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
	REQUIRE(execCount == 1);
}

TEST_CASE("Queuing from the task itself does not deadlock", "[MpmcTaskWorker]") {
	Test::TestContext<> ctx;
	MpmcTaskWorker worker;

	std::atomic<int> execCount{0};
	std::shared_ptr<std::promise<void>> prom;
	auto fut = makePromise(prom);

	worker.work([&worker, &execCount, prom]() {
		execCount.fetch_add(1);
		worker.work([&execCount, prom]() {
			execCount.fetch_add(1);
			prom->set_value();
		});
	});

	REQUIRE(fut.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
	REQUIRE(execCount == 2);
}

TEST_CASE("Destruction with tasks still queued completes", "[MpmcTaskWorker]") {
	// The destructor posts exactly ONE signal, but the worker only re-checks
	// workerIsRunning after its drain loop empties the counter. Queued work at
	// destruction therefore has to be drained (each item carries its own post)
	// before the shutdown post is consumed. If that accounting is off, join()
	// never returns and the whole process hangs here rather than failing.
	//
	// Not asserting whether the queued tasks run — that is a policy choice the
	// implementation is free to make. Only that destruction terminates.
	Test::TestContext<> ctx;
	std::atomic<int> execCount{0};

	{
		MpmcTaskWorker worker;
		for (int i = 0; i < 16; i++) {
			worker.work([&execCount]() { execCount.fetch_add(1); });
		}
	}   // ~MpmcTaskWorker must not deadlock in join()

	SUCCEED("destructor returned with work outstanding");
}

TEST_CASE("Destruction drains a deep backlog rather than hanging", "[MpmcTaskWorker]") {
	// The worker drains BY COUNTER and only re-checks workerIsRunning once the
	// counter reaches zero, so destruction with a large backlog must still
	// terminate: every queued item carries its own post, and the destructor's
	// extra post is consumed by the final pass. A miscount here holds join()
	// open forever.
	//
	// Fills the queue to capacity first, so the backlog is as deep as the class
	// allows when the destructor runs.
	//
	// Note this does NOT push concurrently with destruction. Calling work() on a
	// worker another thread is destroying is a use-after-free by construction —
	// the caller must keep it alive — so that is a misuse to avoid, not a
	// behaviour to pin. Production holds a shared_ptr per module, which is
	// exactly that guarantee.
	Test::TestContext<> ctx;
	std::atomic<int> accepted{0};

	{
		MpmcTaskWorker worker("", 64);
		while (worker.work([]() { std::this_thread::yield(); })) {
			if (accepted.fetch_add(1) > 500) break;   // stop if it never fills
		}
	}   // ~MpmcTaskWorker must not deadlock in join()

	REQUIRE(accepted.load() > 0);
	SUCCEED("destructor returned with a backlog outstanding");
}

TEST_CASE("Contended producers then quiescence: no accepted task is stranded", "[MpmcTaskWorker]") {
	// Regression for the MPMCQueue head-slot race: try_pop() fails transiently
	// on a NON-empty queue while the producer owning the head slot is
	// mid-commit. A worker that treats one wake-up as one pop then consumes a
	// signal without removing an item, and once the producers go quiet the
	// stranded item never runs. Concurrency plus quiescence is the trigger:
	// each round hammers the queue from several threads, then stops pushing
	// and requires every accepted task to have run — a stranded item turns the
	// post-round wait into a timeout.
	// The race needs a precise shape: the worker AWAKE and draining (a sleeping
	// worker's µs wake-up latency dwarfs a producer's ns commit window, so it
	// never observes a mid-commit head slot), the queue drained to empty at
	// head with a push still in flight, and a second push's signal already
	// consumed. Producers therefore burst in loose synchrony — released
	// together, then each delayed by a small pseudo-random stagger — so pushes
	// spread across the window where the worker is catching up, instead of
	// landing while it sleeps (all-simultaneous) or piling into a backlog
	// whose head is always long-committed (free-running).
	Test::TestContext<> ctx;
	MpmcTaskWorker worker;

	constexpr int BURSTS = 3000;
	constexpr int PRODUCERS = 8;
	std::atomic<int> execCount{0};
	std::atomic<int> acceptedCount{0};
	std::atomic<int> arrived{0};
	std::atomic<int> burstGen{0};

	std::vector<std::thread> threads;
	for (int p = 0; p < PRODUCERS; p++) {
		threads.emplace_back([&, p]() {
			uint32_t rng = 0x9e3779b9u * (p + 1);
			for (int b = 0; b < BURSTS; b++) {
				// Spin barrier: the last producer to arrive releases the burst.
				if (arrived.fetch_add(1) == PRODUCERS - 1) {
					arrived.store(0);
					burstGen.fetch_add(1);
				}
				else {
					while (burstGen.load(std::memory_order_acquire) <= b) { }
				}
				// Per-producer pseudo-random stagger (~0-4us of spinning) so
				// this burst's pushes overlap the worker's drain of the
				// previous ones rather than all landing on a sleeping worker.
				rng = rng * 1664525u + 1013904223u;
				for (volatile uint32_t d = rng % 4096; d > 0; d = d - 1) { }
				if (worker.work([&execCount]() { execCount.fetch_add(1); })) {
					acceptedCount.fetch_add(1);
				}
			}
		});
	}
	for (auto& t : threads) t.join();

	// Quiescence: nothing pushes anymore, so only already-posted signals can
	// drive the worker. Every accepted task must still land.
	auto start = std::chrono::steady_clock::now();
	while (execCount.load() < acceptedCount.load()) {
		if (std::chrono::steady_clock::now() - start > std::chrono::seconds(5)) break;
		std::this_thread::yield();
	}
	REQUIRE(execCount.load() == acceptedCount.load());
	// The counter is the mechanism under test, so assert it directly rather
	// than only through its symptom: every accepted push incremented it and
	// every pop decremented it, so quiescence must leave it at exactly zero.
	// A drift that happens to net out in execCount would still be a bug.
	REQUIRE(worker.pendingTasks.load() == 0);
}

TEST_CASE("A task pushed as the worker parks is never lost", "[MpmcTaskWorker]") {
	// The lost-wakeup case the semaphore exists to prevent. A condition variable
	// notified without its mutex held can fire between the waiter evaluating its
	// predicate and actually blocking; that notify wakes nobody and is not
	// remembered, so the worker sleeps indefinitely with a task queued. A
	// counting semaphore has no such window — a post arriving before the worker
	// blocks is simply consumed by the next wait().
	//
	// Each iteration waits for the worker to go fully idle (previous task
	// observed complete), then pushes exactly one task, so the push lands while
	// the worker is heading into or already inside wait(). A single lost signal
	// stalls the iteration and trips the timeout.
	Test::TestContext<> ctx;
	MpmcTaskWorker worker;

	for (int i = 0; i < 2000; i++) {
		std::shared_ptr<std::promise<void>> prom;
		auto fut = makePromise(prom);

		REQUIRE(worker.work([prom]() { prom->set_value(); }));
		REQUIRE(fut.wait_for(std::chrono::seconds(5)) == std::future_status::ready);

		// The future is satisfied from inside the task, so the worker may not
		// have looped back to wait() yet. Give it that chance, so the NEXT
		// push races the park rather than arriving at a busy worker.
		std::this_thread::yield();
	}

	// Every signal accounted for: nothing queued, nothing counted.
	REQUIRE(worker.pendingTasks.load() == 0);
}
